#pragma once
#include <QString>
#include <QUrl>

// Abstract interface; no Qt Gui/WebEngine dependency in core/. The concrete
// QtWebEngine-based implementation is ui/backend/AadWebAuthenticator.
namespace vindauga {

class AadInteractiveAuth {
public:
    virtual ~AadInteractiveAuth() = default;

    // Opens authUrl in a browser view, waits until it navigates to a URL starting with
    // redirectPrefix and returns the "code" query parameter from it. Returns an empty
    // string on error, cancellation or timeout.
    //
    // Called synchronously from RdpSession's worker thread (FreeRDP's GetCommonAccessToken
    // callback blocks on it). Implementations must hop to the GUI thread themselves
    // (e.g. QMetaObject::invokeMethod with Qt::BlockingQueuedConnection) and block until
    // the result is available.
    virtual QString captureAuthorizationCode(const QUrl& authUrl,
                                             const QString& redirectPrefix) = 0;
};

} // namespace vindauga
