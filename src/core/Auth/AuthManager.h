#pragma once
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QRecursiveMutex>
#include <QString>
#include <QUrl>

#include "AadTokenProvider.h"
#include "CredentialStore.h"

// Entra ID sign-in for feed discovery, and silent token minting for the RDP connection.
//
// Flow (the same one Microsoft's own web client and avd_feed_connect use):
//  1. signInInteractive(): authorization code + PKCE in an AadInteractiveAuth web view
//     (Conditional Access policies commonly block the device-code flow but allow this
//     one), redirecting to the public client's "nativeclient" page. The code is exchanged
//     for an access token (feed), an id_token (UPN) and a refresh token (offline_access).
//  2. The refresh token is kept in the OS keychain (CredentialStore). Later runs call
//     signInSilently(), a refresh_token grant that needs no browser.
//  3. As an AadTokenProvider the same refresh token mints the tokens FreeRDP asks for at
//     connect time: the AVD gateway token uses the same client ID and scope as the feed
//     sign-in, and the RDSTLS session-host token is a different scope on the same client
//     (Entra refresh tokens are multi-resource), with FreeRDP's proof-of-possession key
//     passed through as req_cnf.
//
// Access tokens are cached per scope (and per req_cnf key, since a proof-of-possession
// token is bound to one key) until a minute before expiry, so a reconnect or a second
// connection does not cost another refresh grant. Every refresh grant rotates the
// refresh token (one keychain write), which is the real cost being avoided.
//
// Network calls are synchronous (a nested QEventLoop around QNetworkAccessManager, the
// same pattern as CredentialStore) and may be made from any thread with an event
// dispatcher; token state is guarded by a mutex. Never logs tokens, only lengths.
namespace vindauga {

class AadInteractiveAuth;

class AuthManager : public QObject, public AadTokenProvider {
    Q_OBJECT
public:
    explicit AuthManager(QObject* parent = nullptr);
    ~AuthManager() override;

    // Opens the Microsoft sign-in in `auth` and blocks until the token exchange is done.
    // On success the refresh token is stored in the keychain. The UPN from a previous
    // sign-in is passed as login_hint so the account picker is skipped.
    bool signInInteractive(AadInteractiveAuth& auth, QString* error = nullptr);

    // Refresh-token grant from the keychain-stored token. False (with `error`) when no
    // token is stored or the tenant refuses the refresh (e.g. AADSTS70043, a Conditional
    // Access sign-in frequency policy), in which case signInInteractive() is required.
    // `error` is the raw "<code>: <description>" text; see explainAuthError().
    bool signInSilently(QString* error = nullptr);

    bool hasStoredRefreshToken() const;
    void signOut(); // forgets the keychain entry and in-memory tokens

    // Access token for the feed (refreshed automatically when within a minute of expiry).
    // Empty if not signed in.
    QString feedAccessToken();
    QDateTime feedTokenExpiresAt() const;
    QString userPrincipalName() const; // from the id_token; empty until signed in

    // Optional User-Agent for the token endpoint requests. Some Conditional Access
    // policies evaluate the client platform from it; the same string should then be set
    // on the sign-in web view (AadWebAuthenticator) so both halves of a sign-in agree.
    // Empty (default) leaves Qt's default.
    void setUserAgent(const QString& userAgent);

    // AadTokenProvider
    QString acquireAccessToken(const QString& scopeEncoded, const QString& reqCnf) override;

    // Turns a token-endpoint error ("AADSTS70043: ...", "invalid_grant: ...", "network
    // error: ...") into a sentence a user can act on. Unknown errors are returned as is.
    static QString explainAuthError(const QString& error);

    // Building blocks, public for tests.
    // Cache key for an access token: the scope words sorted (order is irrelevant to
    // Entra) plus the req_cnf key, if any.
    static QString cacheKey(const QString& scopeEncoded, const QString& reqCnf);
    static QJsonObject decodeJwtPayload(const QString& jwt); // empty object on malformed input
    static QString pkceChallenge(const QString& verifier);  // S256, base64url without padding
    QUrl buildAuthorizeUrl(const QString& codeChallenge, const QString& state,
                           const QString& loginHint) const;
    QString redirectPrefix() const;
    QString clientId() const;
    // The feed scope, percent-encoded as placed in the form body.
    static QString feedScopeEncoded();

signals:
    void signedIn();
    void signedOut();

private:
    struct TokenResponse {
        bool ok = false;
        QString error; // "<error>: <error_description>" from the token endpoint, or transport error
        QString accessToken;
        QString refreshToken; // may be empty when the server did not rotate it
        QString idToken;
        int expiresIn = 0;
    };
    struct CachedToken {
        QString token;
        QDateTime expiresAt;
    };
    QString cachedToken(const QString& key) const; // empty when absent or about to expire
    QUrl tokenEndpoint() const;
    TokenResponse postTokenRequest(const QByteArray& formBody) const; // blocking
    // refresh_token grant with the current refresh token; stores a rotated refresh token
    // and caches the access token under cacheKey(scopeEncoded, reqCnf).
    TokenResponse refreshGrant(const QString& scopeEncoded, const QString& reqCnf);
    // Stores the tokens from a successful response. `generation` is the value of
    // m_generation read before the network request; if signOut() has bumped it since,
    // the response is discarded and nothing is persisted (a sign-out must not be undone by
    // a grant that was in flight). Returns whether the tokens were adopted.
    bool adoptTokens(const TokenResponse& response, const QString& cacheKey, quint64 generation);
    quint64 currentGeneration() const;

    QString m_clientId;
    QString m_tenant;
    QString m_loginHost; // "https://login.microsoftonline.com" unless overridden
    CredentialStore m_store;

    // Two locks. m_mutex is the cheap field lock (never held across I/O). m_opMutex
    // serialises every state transition that involves the keychain (adoptTokens,
    // refreshGrant's keychain load, signInInteractive's clear, signOut) so that a
    // sign-out cannot interleave with a grant's persistence step; network requests run
    // outside both. Recursive because CredentialStore spins a nested QEventLoop, which on
    // the GUI thread could re-enter this object from a timer/slot.
    mutable QMutex m_mutex;
    mutable QRecursiveMutex m_opMutex;
    quint64 m_generation = 0; // bumped by signOut(); see adoptTokens
    QString m_refreshToken;
    QHash<QString, CachedToken> m_tokens; // access tokens by cacheKey()
    QString m_upn;
    QString m_userAgent;
};

} // namespace vindauga
