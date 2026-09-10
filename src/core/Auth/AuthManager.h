#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

class QOAuth2DeviceAuthorizationFlow;

// Device-code OAuth login for the feed-discovery flow (unused by the application).
// The device-code flow is used because Microsoft's client IDs reject loopback
// http://localhost:<port> redirects (AADSTS50011 / AADSTS900971); it avoids redirect_uri
// matching entirely at the cost of the user entering a short code on a web page.
namespace vindauga {

class AuthManager : public QObject {
    Q_OBJECT
public:
    explicit AuthManager(QObject* parent = nullptr);
    ~AuthManager() override;

    // Starts login: requests a device code, opens the system browser on Microsoft's code
    // page (code pre-filled in the URL) and polls until the user approves or the code expires.
    void login();
    void logout();
    bool hasValidToken() const;
    QString feedToken() const;

    // Expiry time of feedToken(); invalid QDateTime before the first successful login.
    QDateTime expiresAt() const;

signals:
    void feedTokenAcquired(const QString& token);
    void loginFailed(const QString& message);
    void loggedOut();

    // The UI should show the user the URL and code; the URL is also opened automatically.
    void verificationCodeReady(const QUrl& verificationUrl, const QString& userCode);

private:
    QOAuth2DeviceAuthorizationFlow* m_flow = nullptr;
    QString m_accessToken;
};

} // namespace vindauga
