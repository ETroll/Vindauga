#pragma once
#include <QEventLoop>
#include <QObject>
#include <QPointer>

#include "Rdp/CertificatePrompt.h"

class QQuickWindow;

namespace vindauga {

// Qt Quick implementation of CertificatePrompt. Lives in ui/ because it needs Qt
// Quick/QuickControls2, which core/ must not depend on.
//
// Shows the host, fingerprint and issuer/subject of a certificate FreeRDP's own
// verification against its certificate cache could not vouch for (new or changed; see
// CertificateInfo), and lets the user reject it, trust it for this session only, or
// trust and store it (FreeRDP itself persists an "always trusted" decision to its
// certificate cache — there is no separate store here). If setHostWindow() has been
// called (typically by RdpItem with its own window()), the dialog is shown as an overlay
// inside that window; otherwise it falls back to its own temporary top-level window.
class CertificateTrustDialog : public QObject, public CertificatePrompt {
    Q_OBJECT
public:
    explicit CertificateTrustDialog(QObject* parent = nullptr);

    // Call before promptForCertificate (typically from RdpItem::connectToSession, on the
    // GUI thread). Not owned. nullptr (default) means a separate window.
    void setHostWindow(QQuickWindow* window);

    // CertificatePrompt. Called from the RdpSession worker thread; hops to the GUI
    // thread (Qt::BlockingQueuedConnection) and blocks until the result is available.
    Decision promptForCertificate(const CertificateInfo& info) override;

private slots:
    // Connected by name (the QML signal has no C++ type to use &Type::signal syntax
    // against) to the dynamically instantiated dialog's decisionMade signal in
    // promptOnGuiThread.
    void onDecisionMade(int decision);

private:
    // Runs on the GUI thread; see promptForCertificate.
    Q_INVOKABLE int promptOnGuiThread(const CertificateInfo& info);

    // Transient state for one promptOnGuiThread run. Safe without locking: it always
    // runs on the GUI thread, and Qt::BlockingQueuedConnection serialises calls from
    // worker threads, so there are never two concurrent runs.
    QEventLoop* m_requestLoop = nullptr;
    Decision m_result = Decision::Reject;
    QPointer<QQuickWindow> m_hostWindow; // not owned, see setHostWindow
};

} // namespace vindauga

Q_DECLARE_METATYPE(vindauga::CertificateInfo)
