#pragma once
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "Auth/CredentialStore.h"
#include "Rdp/AadInteractiveAuth.h"

class QQuickWindow;
class QQuickWebEngineProfile;

namespace vindauga {

// QtWebEngine-based AadInteractiveAuth. Lives in ui/ because it needs
// Qt6::WebEngineQuick, which core/ must not depend on.
//
// Shows the web view only for the Microsoft sign-in page itself; the redirect navigation
// is intercepted (the redirect page is never loaded) and the view is removed as soon as
// the code has been captured.
//
// Persistent profile: every view uses one shared, on-disk QQuickWebEngineProfile
// (signInProfile()) rather than QtWebEngine's default profile, which is off-the-record
// and forgets Microsoft's SSO cookies the moment the view is destroyed. With the cookies
// kept, a later sign-in is either fully silent (login_hint plus a valid session: Microsoft
// redirects straight back with a code) or a single click on the account tile; the
// password and MFA steps only reappear when the tenant's policy demands them.
//
// Delayed display: the view starts hidden and only becomes visible after a short grace
// period (kShowDelayMs). A sign-in that Microsoft completes on its own therefore never
// flashes a browser on screen; the user sees the page only when their input is needed.
//
// Popups are blocked (the sign-in flow is single-window; a popup would be an orphaned
// window outside the overlay).
//
// If setHostWindow() has been called (typically by RdpItem with its own window()), the
// view is shown as an overlay inside that window; otherwise it falls back to its own
// temporary top-level QQuickWindow.
//
// Security note: unlike a loopback/device-code flow, the sign-in page (password/MFA) runs
// inside our process here (Chromium via QtWebEngine). This is a deliberate, narrowly
// scoped trade-off for this one use.
//
// Keychain autofill (fallback, when the cookies did not carry the sign-in through): with
// a connection key set (shared with RdstlsCredentialDialog, see CredentialStore),
// injected JavaScript tries to pick the stored account on the picker and to fill in and
// submit Microsoft's sign-in form so that only the MFA step remains manual. Best effort:
// it targets Microsoft's historically stable field IDs (i0116/i0118/idSIButton9); if they
// are not found nothing happens and the user signs in manually.
class AadWebAuthenticator : public QObject, public AadInteractiveAuth {
    Q_OBJECT
public:
    explicit AadWebAuthenticator(QObject* parent = nullptr);

    // Call before captureAuthorizationCode (typically from RdpItem::connectToSession, on
    // the GUI thread). Not owned. nullptr (default) means a separate window.
    void setHostWindow(QQuickWindow* window);

    // Key for keychain lookups (typically SavedConnection::id, the same key given to
    // RdstlsCredentialDialog::setConnectionKey). Empty (default) disables autofill.
    void setConnectionKey(const QString& key);

    // The shared persistent profile (created on first use, owned by the application).
    // GUI thread only.
    static QQuickWebEngineProfile* signInProfile();
    // User-Agent for the sign-in pages; empty restores Chromium's default. See
    // AppSettings::signInUserAgent. GUI thread only; applies to subsequent loads.
    static void setSignInUserAgent(const QString& userAgent);
    // Deletes the profile's cookies so the next sign-in starts from a clean account
    // picker (sign-out). GUI thread only.
    static void clearSignInState();

    // Ends the capture currently showing (if any) with an empty result. GUI thread only.
    void cancelCapture();
    // cancelCapture() plus: every later capture returns an empty result at once without
    // creating a view. For application shutdown, so a worker thread blocked on a capture
    // hop (see captureAuthorizationCode) can be joined. GUI thread only.
    void shutdown();

    // AadInteractiveAuth. Called from the RdpSession worker thread; hops to the GUI
    // thread (Qt::BlockingQueuedConnection) and blocks until the result is available.
    QString captureAuthorizationCode(const QUrl& authUrl, const QString& redirectPrefix) override;

private slots:
    // Connected by name (the QML signals have no C++ type to use &Type::signal syntax
    // against) to the dynamically instantiated view's codeCaptured/authFailed signals in
    // captureOnGuiThread.
    void onCodeCaptured(const QString& code);
    void onAuthFailed(const QString& reason);
    // Relays console.log() lines from the autofill script (see kCaptureQml) to our own
    // log for diagnostics.
    void onAutofillLog(const QString& message);

private:
    // Runs on the GUI thread; see captureAuthorizationCode.
    Q_INVOKABLE QString captureOnGuiThread(const QUrl& authUrl, const QString& redirectPrefix);

    // Transient state for one captureOnGuiThread run. Safe without locking: it always
    // runs on the GUI thread, and Qt::BlockingQueuedConnection serialises calls from
    // worker threads, so there are never two concurrent runs.
    QEventLoop* m_captureLoop = nullptr;
    QString m_capturedCode;
    QString m_captureError;
    bool m_shutDown = false; // see shutdown()
    QPointer<QQuickWindow> m_hostWindow; // not owned, see setHostWindow
    QString m_connectionKey;
    CredentialStore m_credentialStore;
};

} // namespace vindauga
