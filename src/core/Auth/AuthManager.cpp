#include "AuthManager.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>
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

// Sign-in authority. VINDAUGA_LOGIN_HOST overrides it for sovereign clouds
// (https://login.microsoftonline.us, https://login.chinacloudapi.cn); the redirect below
// follows the same host. Not verified against a sovereign tenant.
constexpr auto kDefaultLoginHost = "https://login.microsoftonline.com";

// Registered redirect for the public client: the browser lands on a blank page with
// ?code=... that the web view intercepts.
constexpr auto kRedirectPath = "/common/oauth2/nativeclient";

constexpr auto kFeedScope = "https://www.wvd.microsoft.com/.default offline_access openid profile";

// Keychain key for the refresh token (CredentialStore service "vindauga"; saved
// connections use UUID keys so there is no collision).
constexpr auto kRefreshTokenKey = "entra-refresh-token";

constexpr int kHttpTimeoutMs = 30000;

// A cached access token is reused only while it has at least this long left; FreeRDP
// needs the token to survive the whole gateway/session-host handshake.
constexpr int kExpiryMarginSecs = 60;

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
      m_tenant(envOr("VINDAUGA_TENANT", kDefaultTenant)),
      m_loginHost(envOr("VINDAUGA_LOGIN_HOST", kDefaultLoginHost)) {
    while (m_loginHost.endsWith(QLatin1Char('/')))
        m_loginHost.chop(1);
}

AuthManager::~AuthManager() {
    // A rotated refresh token adopted on a worker thread is persisted from the main
    // thread's event loop; if the application is quitting before that ran, write it now
    // (the old token may already be invalid server-side, so losing it means a fresh
    // interactive sign-in next start).
    flushPendingPersist();
}

QString AuthManager::clientId() const {
    return m_clientId;
}

QString AuthManager::redirectPrefix() const {
    return m_loginHost + QLatin1String(kRedirectPath);
}

QString AuthManager::feedScopeEncoded() {
    return QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(kFeedScope)));
}

void AuthManager::setUserAgent(const QString& userAgent) {
    QMutexLocker lock(&m_mutex);
    m_userAgent = userAgent;
}

QUrl AuthManager::tokenEndpoint() const {
    return QUrl(QStringLiteral("%1/%2/oauth2/v2.0/token").arg(m_loginHost, m_tenant));
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

QString AuthManager::explainAuthError(const QString& error) {
    if (error.isEmpty())
        return {};
    static const QRegularExpression codePattern(QStringLiteral("AADSTS(\\d+)"));
    const QRegularExpressionMatch match = codePattern.match(error);
    const QString code = match.hasMatch() ? match.captured(1) : QString();
    const QString suffix = code.isEmpty() ? QString() : QStringLiteral(" (AADSTS%1)").arg(code);

    if (error.startsWith(QLatin1String("network error")))
        return QStringLiteral("Could not reach Microsoft's sign-in service. Check the network "
                              "connection and try again.");
    if (error.startsWith(QLatin1String("no refresh token stored")))
        return QStringLiteral("Not signed in yet.");
    if (code == QLatin1String("70043") || code == QLatin1String("50173") ||
        code == QLatin1String("50078") || code == QLatin1String("50076") ||
        code == QLatin1String("50079"))
        return QStringLiteral("Your organisation requires you to sign in again (a sign-in "
                              "frequency or multi-factor policy).") + suffix;
    if (code == QLatin1String("700082") || code == QLatin1String("70008") ||
        code == QLatin1String("50089"))
        return QStringLiteral("The saved sign-in expired through inactivity. Sign in again.") +
               suffix;
    if (code == QLatin1String("53003") || code == QLatin1String("530003") ||
        code == QLatin1String("53000") || code == QLatin1String("53001"))
        return QStringLiteral("Blocked by a Conditional Access policy: your organisation only "
                              "allows managed or compliant devices for this app.") + suffix;
    if (code == QLatin1String("50020") || code == QLatin1String("50034") ||
        code == QLatin1String("90072"))
        return QStringLiteral("This account is not allowed to use Azure Virtual Desktop in "
                              "this tenant.") + suffix;
    if (error.startsWith(QLatin1String("invalid_grant")) ||
        error.startsWith(QLatin1String("interaction_required")))
        return QStringLiteral("The saved sign-in is no longer valid. Sign in again.") + suffix;
    return error;
}

QUrl AuthManager::buildAuthorizeUrl(const QString& codeChallenge, const QString& state,
                                    const QString& loginHint) const {
    QUrl url(QStringLiteral("%1/%2/oauth2/v2.0/authorize").arg(m_loginHost, m_tenant));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectPrefix());
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kFeedScope));
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), state);
    // Always the account picker. Without a prompt, tenants with Seamless SSO can hang the
    // web view on "Trying to sign you in" (it needs a Windows Kerberos ticket), and
    // prompt=login forces a full re-login that triggers "Keep your account secure"
    // interrupts. The picker is one click when SSO cookies are present, and skipped
    // entirely when login_hint names the account.
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
    {
        QMutexLocker lock(&m_mutex);
        if (!m_userAgent.isEmpty())
            request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    }

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

QString AuthManager::cacheKey(const QString& scopeEncoded, const QString& reqCnf) {
    // The same scope set spelled in a different order is the same token request (Entra
    // ignores the order): the feed sign-in asks for ".default offline_access openid
    // profile" and FreeRDP's gateway scope for ".default openid profile offline_access",
    // so the words are sorted to make the feed token serve the first connection too.
    QStringList words = QUrl::fromPercentEncoding(scopeEncoded.toLatin1())
                            .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    words.sort();
    const QString normalised = words.join(QLatin1Char(' '));
    // A proof-of-possession token is bound to the key in req_cnf, so a different key
    // must never get a token minted for another one.
    return reqCnf.isEmpty() ? normalised : normalised + QLatin1Char('|') + reqCnf;
}

QString AuthManager::cachedToken(const QString& key) const {
    QMutexLocker lock(&m_mutex);
    const auto it = m_tokens.constFind(key);
    if (it == m_tokens.constEnd())
        return {};
    if (QDateTime::currentDateTime().secsTo(it->expiresAt) <= kExpiryMarginSecs)
        return {};
    return it->token;
}

bool AuthManager::adoptTokens(const TokenResponse& response, const QString& cacheKey,
                              quint64 generation) {
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
        m_tokens.insert(cacheKey, { response.accessToken,
                                    QDateTime::currentDateTime().addSecs(response.expiresIn) });
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
    if (rotated) {
        if (QThread::currentThread() == thread()) {
            if (!persistRefreshToken(response.refreshToken, generation))
                return false;
        } else {
            // Grants from the feed worker and from FreeRDP's connection thread must not
            // block on the keychain: it lives on the main thread (see CredentialStore),
            // and the main thread may be about to join this very thread (~RdpSession).
            // Record it as pending and flush it from the main thread instead. Queued calls
            // run in order there, so a later signOut() still ends up with the entry
            // removed, and a sign-out that ran before the flush is caught by the
            // generation check. The pending record (not just a queued lambda) is what
            // lets the destructor flush a write that the event loop never got to.
            {
                QMutexLocker lock(&m_mutex);
                m_pendingPersist = { response.refreshToken, generation, true };
            }
            QMetaObject::invokeMethod(this, &AuthManager::flushPendingPersist,
                                      Qt::QueuedConnection);
        }
    }
    qCDebug(lcAuth) << "Token response adopted: access token length" << response.accessToken.size()
                    << "expires in" << response.expiresIn << "s, refresh token rotated:" << rotated;
    return true;
}

void AuthManager::flushPendingPersist() {
    PendingPersist pending;
    {
        QMutexLocker lock(&m_mutex);
        pending = m_pendingPersist;
        m_pendingPersist = {};
    }
    if (pending.set)
        persistRefreshToken(pending.refreshToken, pending.generation);
}

bool AuthManager::persistRefreshToken(const QString& refreshToken, quint64 generation) {
    // Keychain I/O under m_opMutex only (m_mutex is never held across I/O): signOut()
    // takes m_opMutex too, so it cannot delete the entry between the check and the write.
    QMutexLocker opLock(&m_opMutex);
    {
        QMutexLocker lock(&m_mutex);
        if (generation != m_generation) {
            qCInfo(lcAuth) << "Not persisting the refresh token: signed out meanwhile";
            return false;
        }
    }
    m_store.saveText(QString::fromLatin1(kRefreshTokenKey), refreshToken);
    // saveText spins a nested QEventLoop; a signOut() can run from inside it (m_opMutex is
    // recursive for exactly that reason). Re-check, and undo the write if so.
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
    body += "&redirect_uri=" + QUrl::toPercentEncoding(redirectPrefix());
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
        {
            QMutexLocker lock(&m_mutex);
            m_refreshToken.clear();
            m_tokens.clear();
            m_pendingPersist = {};
        }
        // Not under m_opMutex: this runs on the sign-in worker, and remove() waits for
        // the main thread (see refreshGrant for the deadlock this avoids).
        m_store.remove(QString::fromLatin1(kRefreshTokenKey));
    }
    if (!adoptTokens(response, cacheKey(feedScopeEncoded(), QString()), generation)) {
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
        QMutexLocker lock(&m_mutex);
        refreshToken = m_refreshToken;
        generation = m_generation;
    }
    if (refreshToken.isEmpty()) {
        // No lock across the keychain read. On a worker thread loadText blocks until the
        // main thread has run the job, and the main thread may dispatch signOut() in the
        // meantime; holding m_opMutex here would deadlock that click. The generation check
        // below is what keeps a sign-out from being undone: a value read before the
        // sign-out removed it is discarded, and one read after it does not exist.
        refreshToken = m_store.loadText(QString::fromLatin1(kRefreshTokenKey));
        if (refreshToken.isEmpty()) {
            TokenResponse none;
            none.error = QStringLiteral("no refresh token stored; interactive sign-in required");
            return none;
        }
        QMutexLocker lock(&m_mutex);
        if (generation != m_generation) {
            TokenResponse none;
            none.error = QStringLiteral("signed out while loading the refresh token");
            return none;
        }
        m_refreshToken = refreshToken;
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
    } else if (!adoptTokens(response, cacheKey(scopeEncoded, reqCnf), generation)) {
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
        m_tokens.clear();
        m_upn.clear();
        m_pendingPersist = {}; // a not-yet-flushed rotation belongs to the old sign-in
    }
    m_store.remove(QString::fromLatin1(kRefreshTokenKey));
    emit signedOut();
}

QString AuthManager::feedAccessToken() {
    const QString key = cacheKey(feedScopeEncoded(), QString());
    const QString cached = cachedToken(key);
    if (!cached.isEmpty())
        return cached;
    if (!signInSilently())
        return {};
    return cachedToken(key);
}

QDateTime AuthManager::feedTokenExpiresAt() const {
    QMutexLocker lock(&m_mutex);
    return m_tokens.value(cacheKey(feedScopeEncoded(), QString())).expiresAt;
}

QString AuthManager::userPrincipalName() const {
    QMutexLocker lock(&m_mutex);
    return m_upn;
}

QString AuthManager::acquireAccessToken(const QString& scopeEncoded, const QString& reqCnf) {
    const QString cached = cachedToken(cacheKey(scopeEncoded, reqCnf));
    if (!cached.isEmpty()) {
        qCDebug(lcAuth) << "Silent token request served from cache, scope" << scopeEncoded
                        << "length" << cached.size();
        return cached;
    }
    qCDebug(lcAuth) << "Silent token request, scope" << scopeEncoded << "req_cnf present:"
                    << !reqCnf.isEmpty();
    const TokenResponse response = refreshGrant(scopeEncoded, reqCnf);
    return response.ok ? response.accessToken : QString();
}

} // namespace vindauga
