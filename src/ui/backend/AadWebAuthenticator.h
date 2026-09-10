#pragma once
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "Auth/CredentialStore.h"
#include "Rdp/AadInteractiveAuth.h"

class QQuickWindow;

namespace vindauga {

// QtWebEngine-based AadInteractiveAuth. Lives in ui/ because it needs
// Qt6::WebEngineQuick, which core/ must not depend on.
//
// Shows the web view only for the Microsoft sign-in page itself; the redirect navigation
// is intercepted (the redirect page is never loaded) and the view is removed as soon as
// the code has been captured.
//
// If setHostWindow() has been called (typically by RdpItem with its own window()), the
// view is shown as an overlay inside that window; otherwise it falls back to its own
// temporary top-level QQuickWindow.
//
// Security note: unlike a loopback/device-code flow, the sign-in page (password/MFA) runs
// inside our process here (Chromium via QtWebEngine). This is a deliberate, narrowly
// scoped trade-off for this one use.
//
// Keychain autofill: with a connection key set (shared with RdstlsCredentialDialog, see
// CredentialStore), injected JavaScript tries to fill in and submit Microsoft's sign-in
// form so that only the MFA step remains manual. Best effort: it targets Microsoft's
// historically stable field IDs (i0116/i0118/idSIButton9); if they are not found nothing
// happens and the user signs in manually.
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
    QPointer<QQuickWindow> m_hostWindow; // not owned, see setHostWindow
    QString m_connectionKey;
    CredentialStore m_credentialStore;
};

} // namespace vindauga
