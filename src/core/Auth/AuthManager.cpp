#include "AuthManager.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>

#include "../Logging.h"
#include "../Rdp/AadInteractiveAuth.h"

namespace vindauga {

namespace {

// Microsoft's public "Microsoft Remote Desktop" client ID, the same one libfreerdp uses
// for its AVD gateway token (FreeRDP_GatewayAvdClientID). One refresh token from this
// client can therefore mint every token the connection needs.
constexpr auto kDefaultClientId = "a85cf173-4192-42f8-81fa-777a763e6e2c";

// "organizations": any work/school account; the feed then lists every workspace the
// account is entitled to across tenants. Override with VINDAUGA_TENANT to pin one tenant.
constexpr auto kDefaultTenant = "organizations";

constexpr auto kLoginHost = "https://login.microsoftonline.com";

// Registered redirect for the public client: the browser lands on a blank page with
// ?code=... that the web view intercepts.
constexpr auto kRedirect = "https://login.microsoftonline.com/common/oauth2/nativeclient";

constexpr auto kFeedScope = "https://www.wvd.microsoft.com/.default offline_access openid profile";

// Keychain key for the refresh token (CredentialStore service "vindauga"; saved
// connections use UUID keys so there is no collision).
constexpr auto kRefreshTokenKey = "entra-refresh-token";

constexpr int kHttpTimeoutMs = 30000;

QString envOr(const char* name, const char* fallback) {
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? QString::fromLatin1(fallback) : QString::fromUtf8(value);
}

QString base64Url(const QByteArray& bytes) {
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString randomUrlSafeString(int bytes) {
    QByteArray buffer(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(buffer.data()),
                                          static_cast<qsizetype>(buffer.size() / 4));
    return base64Url(buffer);
}

} // namespace

AuthManager::AuthManager(QObject* parent)
    : QObject(parent),
      m_clientId(envOr("VINDAUGA_CLIENT_ID", kDefaultClientId)),
      m_tenant(envOr("VINDAUGA_TENANT", kDefaultTenant)) {}

AuthManager::~AuthManager() = default;

QString AuthManager::clientId() const {
    return m_clientId;
}

QString AuthManager::redirectPrefix() const {
    return QString::fromLatin1(kRedirect);
}

QString AuthManager::feedScopeEncoded() {
    return QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(kFeedScope)));
}

QUrl AuthManager::tokenEndpoint() const {
    return QUrl(QStringLiteral("%1/%2/oauth2/v2.0/token").arg(QString::fromLatin1(kLoginHost), m_tenant));
}

QString AuthManager::pkceChallenge(const QString& verifier) {
    return base64Url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
}

QJsonObject AuthManager::decodeJwtPayload(const QString& jwt) {
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return {};
    const QByteArray payload =
        QByteArray::fromBase64(parts.at(1).toLatin1(), QByteArray::Base64UrlEncoding);
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    return doc.isObject() ? doc.object() : QJsonObject();
}

QUrl AuthManager::buildAuthorizeUrl(const QString& codeChallenge, const QString& state,
                                    const QString& loginHint) const {
    QUrl url(QStringLiteral("%1/%2/oauth2/v2.0/authorize").arg(QString::fromLatin1(kLoginHost), m_tenant));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), QString::fromLatin1(kRedirect));
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kFeedScope));
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), state);
    // Always the account picker. Without a prompt, tenants with Seamless SSO can hang the
    // web view on "Trying to sign you in" (it needs a Windows Kerberos ticket), and
    // prompt=login forces a full re-login that triggers "Keep your account secure"
    // interrupts. The picker is one click when SSO cookies are present.
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("select_account"));
    if (!loginHint.isEmpty())
        query.addQueryItem(QStringLiteral("login_hint"), loginHint);
    // QUrlQuery leaves spaces as "+" only in toString(FullyEncoded); encode explicitly so
    // the scope's spaces become %20.
    url.setQuery(query.query(QUrl::FullyEncoded).replace(QLatin1Char('+'), QStringLiteral("%2B")));
    return url;
}

AuthManager::TokenResponse AuthManager::postTokenRequest(const QByteArray& formBody) const {
    TokenResponse result;

    QNetworkAccessManager nam;
    QNetworkRequest request(tokenEndpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(kHttpTimeoutMs);

    QEventLoop loop;
    QNetworkReply* reply = nam.post(request, formBody);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QJsonObject json = QJsonDocument::fromJson(body).object();

    if (reply->error() != QNetworkReply::NoError && status == 0) {
        result.error = QStringLiteral("network error: %1").arg(reply->errorString());
        return result;
    }
    if (status != 200 || !json.contains(QStringLiteral("access_token"))) {
        result.error = QStringLiteral("%1: %2")
                           .arg(json.value(QStringLiteral("error")).toString(QStringLiteral("HTTP %1").arg(status)),
                                json.value(QStringLiteral("error_description")).toString());
        return result;
    }
    result.ok = true;
    result.accessToken = json.value(QStringLiteral("access_token")).toString();
    result.refreshToken = json.value(QStringLiteral("refresh_token")).toString();
    result.idToken = json.value(QStringLiteral("id_token")).toString();
    result.expiresIn = json.value(QStringLiteral("expires_in")).toInt();
    return result;
}

quint64 AuthManager::currentGeneration() const {
    QMutexLocker lock(&m_mutex);
    return m_generation;
}

bool AuthManager::adoptTokens(const TokenResponse& response, bool forFeed, quint64 generation) {
    QMutexLocker opLock(&m_opMutex); // generation check + persistence are one step vs signOut()
    bool rotated = false;
    {
        QMutexLocker lock(&m_mutex);
        if (generation != m_generation) {
            qCInfo(lcAuth) << "Discarding token response: signed out while the request was in flight";
            return false;
        }
        if (!response.refreshToken.isEmpty() && response.refreshToken != m_refreshToken) {
            m_refreshToken = response.refreshToken;
            rotated = true;
        }
        if (forFeed) {
            m_feedAccessToken = response.accessToken;
            m_feedExpiresAt = QDateTime::currentDateTime().addSecs(response.expiresIn);
        }
        if (!response.idToken.isEmpty()) {
            const QJsonObject claims = decodeJwtPayload(response.idToken);
            for (const auto* key : {"preferred_username", "upn", "unique_name", "email"}) {
                const QString value = claims.value(QLatin1String(key)).toString();
                if (!value.isEmpty()) {
                    m_upn = value;
                    break;
                }
            }
        }
    }
    // Keychain I/O under m_opMutex only (m_mutex is already released): signOut() takes
    // m_opMutex too, so it cannot delete the entry between the check above and this write.
    if (rotated) {
        m_store.saveText(QString::fromLatin1(kRefreshTokenKey), response.refreshToken);
        // saveText spins a nested QEventLoop; on the GUI thread a signOut() can run from
        // inside it (m_opMutex is recursive for exactly that reason). Re-check, and undo
        // the write if a sign-out happened meanwhile.
        bool signedOutMeanwhile = false;
        {
            QMutexLocker lock(&m_mutex);
            signedOutMeanwhile = generation != m_generation;
        }
        if (signedOutMeanwhile) {
            qCInfo(lcAuth) << "Signed out while persisting the refresh token; removing it again";
            m_store.remove(QString::fromLatin1(kRefreshTokenKey));
            return false;
        }
    }
    qCDebug(lcAuth) << "Token response adopted: access token length" << response.accessToken.size()
                    << "expires in" << response.expiresIn << "s, refresh token rotated:" << rotated;
    return true;
}

bool AuthManager::signInInteractive(AadInteractiveAuth& auth, QString* error) {
    const quint64 generation = currentGeneration();
    const QString verifier = randomUrlSafeString(64);
    const QString state = randomUrlSafeString(16);
    const QUrl authorizeUrl = buildAuthorizeUrl(pkceChallenge(verifier), state, userPrincipalName());

    qCDebug(lcAuth) << "Starting interactive sign-in (auth code + PKCE), tenant" << m_tenant;
    // The code is bound to `verifier` by PKCE: a code not issued for this request cannot
    // be exchanged, which is the protection `state` would otherwise provide.
    const QString code = auth.captureAuthorizationCode(authorizeUrl, redirectPrefix());
    if (code.isEmpty()) {
        if (error)
            *error = QStringLiteral("sign-in cancelled or failed");
        return false;
    }

    QByteArray body;
    body += "grant_type=authorization_code";
    body += "&client_id=" + QUrl::toPercentEncoding(m_clientId);
    body += "&code=" + QUrl::toPercentEncoding(code);
    body += "&redirect_uri=" + QUrl::toPercentEncoding(QString::fromLatin1(kRedirect));
    body += "&scope=" + feedScopeEncoded().toLatin1();
    body += "&code_verifier=" + verifier.toLatin1();

    const TokenResponse response = postTokenRequest(body);
    if (!response.ok) {
        qCWarning(lcAuth) << "Token exchange failed:" << response.error;
        if (error)
            *error = response.error;
        return false;
    }
    if (response.refreshToken.isEmpty()) {
        // The account that just signed in has no refresh token, so the one stored (possibly
        // another account's) must not be reused for it: clear it and leave only the
        // in-memory feed token.
        qCWarning(lcAuth) << "Token response carried no refresh token; silent sign-in will not work";
        QMutexLocker opLock(&m_opMutex);
        {
            QMutexLocker lock(&m_mutex);
            m_refreshToken.clear();
        }
        m_store.remove(QString::fromLatin1(kRefreshTokenKey));
    }
    if (!adoptTokens(response, true, generation)) {
        if (error)
            *error = QStringLiteral("signed out during sign-in");
        return false;
    }
    emit signedIn();
    return true;
}

bool AuthManager::hasStoredRefreshToken() const {
    {
        QMutexLocker lock(&m_mutex);
        if (!m_refreshToken.isEmpty())
            return true;
    }
    return !m_store.loadText(QString::fromLatin1(kRefreshTokenKey)).isEmpty();
}

AuthManager::TokenResponse AuthManager::refreshGrant(const QString& scopeEncoded,
                                                     const QString& reqCnf) {
    QString refreshToken;
    quint64 generation = 0;
    {
        // Under m_opMutex so a signOut() cannot run between reading the keychain and
        // caching its value in memory (which would resurrect a just-removed token).
        QMutexLocker opLock(&m_opMutex);
        {
            QMutexLocker lock(&m_mutex);
            refreshToken = m_refreshToken;
            generation = m_generation;
        }
        if (refreshToken.isEmpty()) {
            refreshToken = m_store.loadText(QString::fromLatin1(kRefreshTokenKey));
            if (refreshToken.isEmpty()) {
                TokenResponse none;
                none.error = QStringLiteral("no refresh token stored; interactive sign-in required");
                return none;
            }
            QMutexLocker lock(&m_mutex);
            if (generation != m_generation) {
                // A signOut() ran from inside loadText's nested event loop; the value just
                // read is the one it removed and must not be cached.
                TokenResponse none;
                none.error = QStringLiteral("signed out while loading the refresh token");
                return none;
            }
            m_refreshToken = refreshToken;
        }
    }

    QByteArray body;
    body += "grant_type=refresh_token";
    body += "&client_id=" + QUrl::toPercentEncoding(m_clientId);
    body += "&refresh_token=" + QUrl::toPercentEncoding(refreshToken);
    body += "&scope=" + scopeEncoded.toLatin1(); // already percent-encoded by the caller
    if (!reqCnf.isEmpty())
        body += "&req_cnf=" + reqCnf.toLatin1(); // base64url from FreeRDP, URL-safe as is

    TokenResponse response = postTokenRequest(body);
    if (!response.ok) {
        qCWarning(lcAuth) << "Refresh grant failed:" << response.error;
    } else if (!adoptTokens(response, scopeEncoded == feedScopeEncoded(), generation)) {
        response = TokenResponse();
        response.error = QStringLiteral("signed out while the token request was in flight");
    }
    return response;
}

bool AuthManager::signInSilently(QString* error) {
    const TokenResponse response = refreshGrant(feedScopeEncoded(), QString());
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    emit signedIn();
    return true;
}

void AuthManager::signOut() {
    QMutexLocker opLock(&m_opMutex);
    {
        QMutexLocker lock(&m_mutex);
        ++m_generation; // invalidates any grant currently in flight (see adoptTokens)
        m_refreshToken.clear();
        m_feedAccessToken.clear();
        m_feedExpiresAt = QDateTime();
        m_upn.clear();
    }
    m_store.remove(QString::fromLatin1(kRefreshTokenKey));
    emit signedOut();
}

QString AuthManager::feedAccessToken() {
    {
        QMutexLocker lock(&m_mutex);
        if (!m_feedAccessToken.isEmpty() &&
            QDateTime::currentDateTime().secsTo(m_feedExpiresAt) > 60)
            return m_feedAccessToken;
    }
    if (!signInSilently())
        return {};
    QMutexLocker lock(&m_mutex);
    return m_feedAccessToken;
}

QDateTime AuthManager::feedTokenExpiresAt() const {
    QMutexLocker lock(&m_mutex);
    return m_feedExpiresAt;
}

QString AuthManager::userPrincipalName() const {
    QMutexLocker lock(&m_mutex);
    return m_upn;
}

QString AuthManager::acquireAccessToken(const QString& scopeEncoded, const QString& reqCnf) {
    qCDebug(lcAuth) << "Silent token request, scope" << scopeEncoded << "req_cnf present:"
                    << !reqCnf.isEmpty();
    const TokenResponse response = refreshGrant(scopeEncoded, reqCnf);
    return response.ok ? response.accessToken : QString();
}

} // namespace vindauga
