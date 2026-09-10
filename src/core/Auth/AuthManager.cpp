#include "AuthManager.h"

#include <QOAuth2DeviceAuthorizationFlow>
#include <QProcess>
#include <QUrl>
#include <QUrlQuery>

#include "../Logging.h"

namespace vindauga {

namespace {

// Microsoft's own default client ID for libfreerdp's AAD gateway flow
// (FreeRDP_GatewayAvdClientID in libfreerdp/common/settings.c). The device-code flow needs
// no redirect_uri, so the same client ID works here.
constexpr auto kDefaultClientId = "a85cf173-4192-42f8-81fa-777a763e6e2c";

// Override with the VINDAUGA_CLIENT_ID environment variable to test other client IDs without rebuilding.
QString resolveClientId() {
    const QByteArray override = qgetenv("VINDAUGA_CLIENT_ID");
    if (!override.isEmpty())
        return QString::fromUtf8(override);
    return QString::fromLatin1(kDefaultClientId);
}

constexpr auto kDeviceCodeUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/devicecode";
constexpr auto kTokenUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
constexpr auto kScope = "https://www.wvd.microsoft.com/User.Access offline_access openid profile";

} // namespace

AuthManager::AuthManager(QObject* parent) : QObject(parent) {
    m_flow = new QOAuth2DeviceAuthorizationFlow(this);
    m_flow->setAuthorizationUrl(QUrl(QString::fromLatin1(kDeviceCodeUrl)));
    m_flow->setTokenUrl(QUrl(QString::fromLatin1(kTokenUrl)));
    m_flow->setClientIdentifier(resolveClientId());
    // setScope is deprecated since Qt 6.13 in favour of requestedScopeTokens, which the
    // project's Qt 6.9 minimum does not provide.
    m_flow->setScope(QString::fromLatin1(kScope));

    connect(m_flow, &QOAuth2DeviceAuthorizationFlow::authorizeWithUserCode, this,
            [this](const QUrl& verificationUrl, const QString& userCode,
                   const QUrl& completeVerificationUrl) {
                qCDebug(lcAuth) << "Device code received, length" << userCode.size()
                                 << "verification URL" << verificationUrl;
                emit verificationCodeReady(verificationUrl, userCode);

                // completeVerificationUrl is an optional RFC 8628 extension that Microsoft's
                // /devicecode endpoint does not provide. Microsoft's own tools build the URL
                // with ?otc=<code>, which survives the redirect chain to the login page and
                // saves the user from typing the code manually.
                QUrl openUrl = completeVerificationUrl.isEmpty() ? verificationUrl
                                                                  : completeVerificationUrl;
                if (completeVerificationUrl.isEmpty()) {
                    QUrlQuery query(openUrl);
                    query.addQueryItem(QStringLiteral("otc"), userCode);
                    openUrl.setQuery(query);
                }
                // QDesktopServices requires Qt6::Gui, which core/ must not depend on, so
                // xdg-open is used directly.
                if (!QProcess::startDetached(QStringLiteral("xdg-open"), {openUrl.toString()})) {
                    qCWarning(lcAuth) << "xdg-open failed — open the URL manually from the terminal";
                }

                if (!m_flow->isPolling())
                    m_flow->startTokenPolling();
            });
    connect(m_flow, &QOAuth2DeviceAuthorizationFlow::granted, this, [this]() {
        m_accessToken = m_flow->token();
        qCDebug(lcAuth) << "Token received, length" << m_accessToken.size() << "expires"
                         << m_flow->expirationAt();
        emit feedTokenAcquired(m_accessToken);
    });
    connect(m_flow, &QOAuth2DeviceAuthorizationFlow::serverReportedErrorOccurred, this,
            [this](const QString& error, const QString& errorDescription, const QUrl&) {
                qCWarning(lcAuth) << "Login failed (server):" << error;
                emit loginFailed(QStringLiteral("%1: %2").arg(error, errorDescription));
            });
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        qCWarning(lcAuth) << "Login failed (network/client):" << static_cast<int>(error);
        emit loginFailed(QStringLiteral("OAuth request failed (code %1)")
                              .arg(static_cast<int>(error)));
    });
}

AuthManager::~AuthManager() = default;

void AuthManager::login() {
    // TODO: attempt silent renewal via a stored refresh token before interactive login.
    qCDebug(lcAuth) << "Starting device-code login, client_id length" << resolveClientId().size();
    m_flow->grant();
}

void AuthManager::logout() {
    m_accessToken.clear();
    emit loggedOut();
}

bool AuthManager::hasValidToken() const {
    return !m_accessToken.isEmpty() && QDateTime::currentDateTime() < m_flow->expirationAt();
}

QString AuthManager::feedToken() const {
    return m_accessToken;
}

QDateTime AuthManager::expiresAt() const {
    return m_flow->expirationAt();
}

} // namespace vindauga
