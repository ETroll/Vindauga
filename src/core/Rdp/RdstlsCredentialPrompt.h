#pragma once
#include <QString>
#include <QStringList>

// Abstract interface; no Qt Quick/GUI dependency in core/. The concrete Qt Quick
// implementation is ui/backend/RdstlsCredentialDialog.
namespace vindauga {

// Prompts for real RDSTLS credentials against an Entra-joined session host. A Linux client
// has no WAM broker and therefore cannot do the passwordless SSO Windows clients get for
// RDSTLS; FreeRDP's own fallback is to ask for username/domain/password.
class RdstlsCredentialPrompt {
public:
    virtual ~RdstlsCredentialPrompt() = default;

    // suggestedUsername/suggestedDomain are FreeRDP's own suggestions (domain is typically
    // "AzureAD", username often empty). Returns [username, domain, password] on success,
    // or an empty list if the user cancelled.
    //
    // Called synchronously from RdpSession's worker thread. Implementations must hop to
    // the GUI thread themselves (e.g. QMetaObject::invokeMethod with
    // Qt::BlockingQueuedConnection) and block until the result is available.
    virtual QStringList requestCredentials(const QString& suggestedUsername,
                                           const QString& suggestedDomain) = 0;
};

} // namespace vindauga
