#include <QtTest>

#include <QUrlQuery>

#include "Auth/AuthManager.h"

using namespace vindauga;

// Pure logic of AuthManager (PKCE, JWT decoding, authorize URL); no network, no keychain.
class AuthManagerTest : public QObject {
    Q_OBJECT
private slots:
    void pkceChallengeMatchesRfc7636Vector() {
        // RFC 7636 appendix B.
        QCOMPARE(AuthManager::pkceChallenge(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")),
                 QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void decodesJwtPayload() {
        // header.payload.signature with payload {"preferred_username":"user@example.com","aud":"x"}
        const QByteArray payload = QByteArray(R"({"preferred_username":"user@example.com","aud":"x"})")
                                       .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        const QString jwt = QStringLiteral("eyJhbGciOiJub25lIn0.%1.sig").arg(QString::fromLatin1(payload));
        const QJsonObject claims = AuthManager::decodeJwtPayload(jwt);
        QCOMPARE(claims.value(QStringLiteral("preferred_username")).toString(),
                 QStringLiteral("user@example.com"));
        QCOMPARE(claims.value(QStringLiteral("aud")).toString(), QStringLiteral("x"));
    }

    void decodeToleratesGarbage() {
        QVERIFY(AuthManager::decodeJwtPayload(QString()).isEmpty());
        QVERIFY(AuthManager::decodeJwtPayload(QStringLiteral("not-a-jwt")).isEmpty());
        QVERIFY(AuthManager::decodeJwtPayload(QStringLiteral("a.!!!.c")).isEmpty());
    }

    void buildsAuthorizeUrl() {
        AuthManager auth;
        const QUrl url = auth.buildAuthorizeUrl(QStringLiteral("chal"), QStringLiteral("st"),
                                                QStringLiteral("user@example.com"));
        QCOMPARE(url.host(), QStringLiteral("login.microsoftonline.com"));
        QVERIFY(url.path().endsWith(QStringLiteral("/oauth2/v2.0/authorize")));

        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("client_id")), auth.clientId());
        QCOMPARE(query.queryItemValue(QStringLiteral("response_type")), QStringLiteral("code"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")), QStringLiteral("chal"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QCOMPARE(query.queryItemValue(QStringLiteral("state")), QStringLiteral("st"));
        QCOMPARE(query.queryItemValue(QStringLiteral("prompt")), QStringLiteral("select_account"));
        QCOMPARE(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded),
                 auth.redirectPrefix());
        QCOMPARE(query.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded),
                 QStringLiteral("https://www.wvd.microsoft.com/.default offline_access openid profile"));
        QCOMPARE(query.queryItemValue(QStringLiteral("login_hint"), QUrl::FullyDecoded),
                 QStringLiteral("user@example.com"));
        // Spaces in the scope must be %20 in the wire form, never "+".
        QVERIFY(!url.toString(QUrl::FullyEncoded).contains(QLatin1Char('+')));
    }

    void feedScopeIsPercentEncoded() {
        QCOMPARE(AuthManager::feedScopeEncoded(),
                 QStringLiteral("https%3A%2F%2Fwww.wvd.microsoft.com%2F.default%20offline_access%20openid%20profile"));
    }
};

QTEST_MAIN(AuthManagerTest)
#include "test_authmanager.moc"
