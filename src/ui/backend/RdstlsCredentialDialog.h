#pragma once
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "Auth/CredentialStore.h"
#include "Rdp/RdstlsCredentialPrompt.h"

class QQuickWindow;

namespace vindauga {

// Qt Quick implementation of RdstlsCredentialPrompt. Lives in ui/ because it needs
// Qt Quick/QuickControls2, which core/ must not depend on.
//
// Shows username/domain/password fields (password masked). If setHostWindow() has been
// called (typically by RdpItem with its own window()), the dialog is shown as an overlay
// inside that window; otherwise it falls back to its own temporary top-level window.
//
// Keychain: with a connection key set (typically SavedConnection::id, via
// setConnectionKey before requestCredentials), CredentialStore is consulted first. If
// credentials exist there the dialog is not shown at all and the stored values are
// returned directly. New input is written to CredentialStore on "Connect". RdpItem calls
// clearCachedCredentials() if a session that used cached credentials never reaches
// connected() (presumably stale or wrong credentials).
class RdstlsCredentialDialog : public QObject, public RdstlsCredentialPrompt {
    Q_OBJECT
public:
    explicit RdstlsCredentialDialog(QObject* parent = nullptr);

    // Call before requestCredentials (typically from RdpItem::connectToSession, on the
    // GUI thread). Not owned. nullptr (default) means a separate window.
    void setHostWindow(QQuickWindow* window);

    // Key for keychain lookups (typically SavedConnection::id). Empty (default) disables
    // caching: the dialog is always shown and nothing is stored.
    void setConnectionKey(const QString& key);

    // True if the last requestCredentials call was answered from the keychain without
    // showing the dialog. Used by RdpItem to decide whether to call
    // clearCachedCredentials() after a failed session.
    bool usedCachedCredentialsLastAttempt() const { return m_usedCachedCredentials; }
    void clearCachedCredentials();

    // RdstlsCredentialPrompt. Called from the RdpSession worker thread; hops to the GUI
    // thread (Qt::BlockingQueuedConnection) and blocks until the result is available.
    QStringList requestCredentials(const QString& suggestedUsername,
                                   const QString& suggestedDomain) override;

private slots:
    // Connected by name (the QML signals have no C++ type to use &Type::signal syntax
    // against) to the dynamically instantiated dialog's credentialsAccepted/
    // credentialsCancelled signals in requestOnGuiThread.
    void onCredentialsAccepted(const QString& username, const QString& domain,
                               const QString& password);
    void onCredentialsCancelled();

private:
    // Runs on the GUI thread; see requestCredentials.
    Q_INVOKABLE QStringList requestOnGuiThread(const QString& suggestedUsername,
                                               const QString& suggestedDomain);

    // Transient state for one requestOnGuiThread run. Safe without locking: it always
    // runs on the GUI thread, and Qt::BlockingQueuedConnection serialises calls from
    // worker threads, so there are never two concurrent runs.
    QEventLoop* m_requestLoop = nullptr;
    QStringList m_result;
    QPointer<QQuickWindow> m_hostWindow; // not owned, see setHostWindow
    QString m_connectionKey;
    CredentialStore m_credentialStore;
    bool m_usedCachedCredentials = false;
};

} // namespace vindauga
