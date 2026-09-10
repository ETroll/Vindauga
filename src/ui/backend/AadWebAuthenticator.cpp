#include "AadWebAuthenticator.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <memory>

#include "Logging.h"

namespace vindauga {

namespace {

// Single-use QML view: loads authUrl, intercepts the navigation to redirectPrefix in
// onNavigationRequested (rejecting it, so the redirect page is never loaded), extracts
// the code/error query parameters in JS and reports them back as Qt signals.
constexpr auto kCaptureQml = R"QML(
import QtQuick
import QtWebEngine

WebEngineView {
    id: webView
    anchors.fill: parent
    property string redirectPrefix: ""
    property string storedUsername: ""
    property string storedPassword: ""
    // Two separate flags rather than one "autofillDone": Microsoft's sign-in is a
    // two-step form (email page, then a separate password page), often as an SPA
    // transition without a new onLoadingChanged event. A single "done" flag set after
    // step 1 would stop polling before the password field ever appeared. usernameFilled
    // prevents clicking "Next" again on every poll.
    property bool usernameFilled: false
    property bool passwordFilled: false
    signal codeCaptured(string code)
    signal authFailed(string reason)
    // Diagnostics only. Separates our own console.log() lines (prefixed
    // "[vindauga-autofill]") from Microsoft's console noise.
    signal autofillLog(string message)

    // Best-effort autofill of Microsoft's sign-in form. Tries several known field
    // selectors and retries over an extended period (retryTimer below), since the
    // transition from the email page to the password page usually happens without a new
    // load event. Errors are swallowed; the user simply signs in manually, and
    // console.log() reports what happened via autofillLog.
    function attemptAutofill() {
        if (passwordFilled)
            return
        if (storedUsername.length === 0 && storedPassword.length === 0)
            return
        const script = "(function(){" +
            "function tag(s){console.log('[vindauga-autofill] '+s);}" +
            "try{" +
            "function setVal(el,v){var d=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(el),'value')||" +
            "Object.getOwnPropertyDescriptor(el,'value');" +
            "if(d&&d.set)d.set.call(el,v);else el.value=v;" +
            "el.dispatchEvent(new Event('input',{bubbles:true}));" +
            "el.dispatchEvent(new Event('change',{bubbles:true}));}" +
            "function first(sels){for(var i=0;i<sels.length;i++){var e=document.querySelector(sels[i]);if(e)return e;}return null;}" +
            "var alreadyUser=" + (usernameFilled ? "true" : "false") + ";" +
            "if(!alreadyUser){" +
            "var u=first(['#i0116','input[name=\"loginfmt\"]','input[type=\"email\"]']);" +
            "if(u){tag('found username field, filling in');setVal(u," + JSON.stringify(storedUsername) + ");" +
            "var n=first(['#idSIButton9','input[type=\"submit\"]','button[type=\"submit\"]']);" +
            "if(n){tag('clicking Next');n.click();}else{tag('did NOT find Next button');}" +
            "return 'filled-username';}" +
            "}" +
            "var p=first(['#i0118','input[name=\"passwd\"]','input[type=\"password\"]']);" +
            "if(p){tag('found password field, filling in');setVal(p," + JSON.stringify(storedPassword) + ");" +
            "var s=first(['#idSIButton9','button[type=\"submit\"]']);" +
            "if(s){tag('clicking Sign in');s.click();}else{tag('did NOT find Sign in button');}" +
            "return 'filled-password';}" +
            "tag('no fields found yet (username already filled='+alreadyUser+') at '+location.href);" +
            "return alreadyUser?'waiting-for-password-field':'no-fields-found';" +
            "}catch(e){tag('error during autofill: '+e.message);return 'error';}" +
            "})();"
        webView.runJavaScript(script, function(result) {
            if (result === "filled-username")
                usernameFilled = true
            if (result === "filled-password")
                passwordFilled = true
            webView.autofillLog("result: " + result)
        })
    }

    onLoadingChanged: function(loadRequest) {
        if (loadRequest.status !== WebEngineView.LoadSucceededStatus)
            return
        attemptAutofill()
        // The transition from the email page to the password page is usually an SPA
        // transition without a new load event, so retryTimer is the main driver for
        // catching the password field, not just a fallback for slow rendering of the
        // first page. ~12 s in total (20 x 600 ms) after each load event.
        retryTimer.attemptsLeft = 20
        retryTimer.restart()
    }

    onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
        if (message.indexOf("[vindauga-autofill]") === 0)
            webView.autofillLog(message)
    }

    Timer {
        id: retryTimer
        interval: 600
        property int attemptsLeft: 0
        onTriggered: {
            if (passwordFilled || attemptsLeft <= 0)
                return
            attemptsLeft -= 1
            attemptAutofill()
            if (attemptsLeft > 0)
                restart()
        }
    }

    onNavigationRequested: function(request) {
        const target = request.url.toString()
        if (redirectPrefix.length > 0 && target.indexOf(redirectPrefix) === 0) {
            request.reject()
            const qIdx = target.indexOf("?")
            const query = qIdx >= 0 ? target.substring(qIdx + 1) : ""
            let code = ""
            let error = ""
            query.split("&").forEach(function(pair) {
                if (!pair) return
                const eq = pair.indexOf("=")
                const k = eq >= 0 ? pair.substring(0, eq) : pair
                const v = eq >= 0 ? decodeURIComponent(pair.substring(eq + 1)) : ""
                if (k === "code") code = v
                if (k === "error") error = v
            })
            if (code.length > 0) webView.codeCaptured(code)
            else webView.authFailed(error.length > 0 ? error : "Redirect without code or error")
        }
    }
}
)QML";

constexpr int kTimeoutMs = 5 * 60 * 1000; // plenty of time for MFA

} // namespace

AadWebAuthenticator::AadWebAuthenticator(QObject* parent) : QObject(parent) {}

void AadWebAuthenticator::setHostWindow(QQuickWindow* window) {
    m_hostWindow = window;
}

void AadWebAuthenticator::setConnectionKey(const QString& key) {
    m_connectionKey = key;
}

QString AadWebAuthenticator::captureAuthorizationCode(const QUrl& authUrl,
                                                       const QString& redirectPrefix) {
    QString result;
    if (QThread::currentThread() == thread()) {
        // Called directly on the GUI thread; should not normally happen since RdpSession
        // runs on its own worker thread, but be robust.
        result = captureOnGuiThread(authUrl, redirectPrefix);
    } else {
        QMetaObject::invokeMethod(this, "captureOnGuiThread", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QString, result), Q_ARG(QUrl, authUrl),
                                  Q_ARG(QString, redirectPrefix));
    }
    return result;
}

QString AadWebAuthenticator::captureOnGuiThread(const QUrl& authUrl,
                                                const QString& redirectPrefix) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArray(kCaptureQml), QUrl());

    std::unique_ptr<QObject> rootObj(component.create());
    if (!rootObj) {
        qCWarning(lcAuth) << "Could not instantiate AAD web view:"
                          << component.errorString();
        return {};
    }
    auto* webView = qobject_cast<QQuickItem*>(rootObj.get());
    if (!webView) {
        qCWarning(lcAuth) << "Unexpected root type for AAD web view";
        return {};
    }

    // With a host window (typically RdpItem::window()) the view is an overlay inside it
    // rather than a separate top-level window. "anchors.fill: parent" in kCaptureQml
    // binds to whatever the parent turns out to be, so the same QML serves both cases.
    std::unique_ptr<QQuickWindow> ownWindow;
    if (m_hostWindow) {
        webView->setParentItem(m_hostWindow->contentItem());
    } else {
        ownWindow = std::make_unique<QQuickWindow>();
        webView->setParentItem(ownWindow->contentItem());
        ownWindow->resize(480, 640);
        ownWindow->setTitle(tr("Vindauga — Entra ID sign-in"));
    }

    connect(webView, SIGNAL(codeCaptured(QString)), this, SLOT(onCodeCaptured(QString)));
    connect(webView, SIGNAL(authFailed(QString)), this, SLOT(onAuthFailed(QString)));
    connect(webView, SIGNAL(autofillLog(QString)), this, SLOT(onAutofillLog(QString)));

    webView->setProperty("redirectPrefix", redirectPrefix);
    webView->setProperty("url", authUrl);
    if (!m_connectionKey.isEmpty()) {
        const CredentialStore::Credentials cached = m_credentialStore.load(m_connectionKey);
        if (cached.found) {
            webView->setProperty("storedUsername", cached.username);
            webView->setProperty("storedPassword", cached.password);
        }
    }

    m_capturedCode.clear();
    m_captureError.clear();
    QEventLoop loop;
    m_captureLoop = &loop;

    if (ownWindow)
        ownWindow->show();

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(kTimeoutMs);

    loop.exec();
    m_captureLoop = nullptr;
    if (ownWindow)
        ownWindow->close();
    else
        webView->setParentItem(nullptr); // remove the overlay from the host window

    if (!m_captureError.isEmpty()) {
        qCWarning(lcAuth) << "AAD sign-in failed:" << m_captureError;
        return {};
    }
    if (m_capturedCode.isEmpty())
        qCWarning(lcAuth) << "AAD sign-in timed out or closed without a result";
    return m_capturedCode;
}

void AadWebAuthenticator::onCodeCaptured(const QString& code) {
    m_capturedCode = code;
    if (m_captureLoop)
        m_captureLoop->quit();
}

void AadWebAuthenticator::onAuthFailed(const QString& reason) {
    m_captureError = reason;
    if (m_captureLoop)
        m_captureLoop->quit();
}

void AadWebAuthenticator::onAutofillLog(const QString& message) {
    // Never the credentials themselves: the script only logs status/error messages, not
    // the username/password values.
    qCDebug(lcAuth) << "Autofill:" << message;
}

} // namespace vindauga
