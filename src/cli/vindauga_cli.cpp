#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonObject>
#include <QStringList>
#include <cstdio>

#include "Auth/AuthManager.h"
#include "Feed/FeedClient.h"
#include "Logging.h"

#ifdef VINDAUGA_WITH_RDP
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "AadWebAuthenticator.h"
#include "RdpItem.h"
#include "X11KeymapGuard.h"
#endif

// Headless CLI for exercising core/ components without the GUI. Feed discovery commands:
//   login [connection-id]  interactive Entra sign-in (web view); stores the refresh token
//   list                   silent sign-in + feed discovery, prints resources
//   refresh-test           silent sign-in, then mints FreeRDP's AVD gateway token scope
//   connect <N> [conn-id]  downloads resource N's .rdp and connects with silent tokens;
//                          a saved-connection id enables the RDSTLS keychain credentials
//   logout                 forgets the refresh token
// Secrets are never printed; only lengths and expiry times.

namespace {

QString fmtExpiry(const QDateTime& at) {
    const qint64 minutes = QDateTime::currentDateTime().secsTo(at) / 60;
    return QStringLiteral("%1 (in %2 min)").arg(at.toString(Qt::ISODate)).arg(minutes);
}

// Shared "silent sign-in or explain" step for list/refresh-test/connect.
bool ensureSignedIn(vindauga::AuthManager& auth) {
    QString error;
    if (auth.signInSilently(&error)) {
        std::printf("silent sign-in OK: user %s, feed token expires %s\n",
                    qUtf8Printable(auth.userPrincipalName()),
                    qUtf8Printable(fmtExpiry(auth.feedTokenExpiresAt())));
        return true;
    }
    std::fprintf(stderr, "silent sign-in failed: %s\n(run `vindauga_cli login` first)\n",
                 qUtf8Printable(error));
    return false;
}

vindauga::FeedClient::Enumeration listResources(vindauga::AuthManager& auth) {
    vindauga::FeedClient feed;
    const auto result = feed.enumerate(auth.feedAccessToken());
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qUtf8Printable(result.error));
        return result;
    }
    std::printf("%lld workspace(s), %lld resource(s):\n",
                static_cast<long long>(result.workspaces.size()),
                static_cast<long long>(result.resources.size()));
    for (const auto& w : result.workspaces)
        std::printf("  workspace %s  geo=%s  tenant=%s\n", qUtf8Printable(w.displayName),
                    qUtf8Printable(w.geo), qUtf8Printable(w.tenantId));
    for (qsizetype i = 0; i < result.resources.size(); ++i) {
        const auto& r = result.resources.at(i);
        std::printf("  [%lld] %-30s %-10s %-12s rdp=%s icon=%s\n", static_cast<long long>(i),
                    qUtf8Printable(r.title), qUtf8Printable(r.type), qUtf8Printable(r.deviceState),
                    r.rdpFileUrl.isEmpty() ? "no" : "yes", r.iconPngUrl.isEmpty() ? "no" : "png");
    }
    return result;
}

int runList(vindauga::AuthManager& auth) {
    if (!ensureSignedIn(auth))
        return 1;
    return listResources(auth).ok ? 0 : 1;
}

// Test 2 of the feed-discovery plan: can the feed refresh token mint the token FreeRDP
// asks for at connect time (same client ID, FreeRDP's own AVD scope) without a browser?
int runRefreshTest(vindauga::AuthManager& auth) {
    if (!ensureSignedIn(auth))
        return 1;
    // FreeRDP_GatewayAvdScope's default, verbatim (percent-encoded) as libfreerdp passes it.
    const QString avdScope = QStringLiteral(
        "https%3A%2F%2Fwww.wvd.microsoft.com%2F.default%20openid%20profile%20offline_access");
    const QString token = auth.acquireAccessToken(avdScope, QString());
    if (token.isEmpty()) {
        std::fprintf(stderr, "refresh grant for the AVD gateway scope FAILED (see log)\n");
        return 1;
    }
    // Tokens for wvd.microsoft.com are opaque to the client (encrypted, not a readable
    // JWT), so claims are only shown when the token happens to be decodable.
    const QJsonObject claims = vindauga::AuthManager::decodeJwtPayload(token);
    std::printf("AVD gateway token OK: length %lld%s\n", static_cast<long long>(token.size()),
                claims.isEmpty() ? " (opaque token, no readable claims)" : "");
    if (!claims.isEmpty())
        std::printf("  aud=%s scp=%s\n",
                    qUtf8Printable(claims.value(QStringLiteral("aud")).toString()),
                    qUtf8Printable(claims.value(QStringLiteral("scp")).toString()));
    return 0;
}

int runLogout(vindauga::AuthManager& auth) {
    auth.signOut();
    std::printf("refresh token removed from the keychain\n");
    return 0;
}

#ifdef VINDAUGA_WITH_RDP
// Test 1: interactive auth-code + PKCE sign-in through the same web view the app uses
// for FreeRDP's AAD step. An optional saved-connection id enables the keychain autofill.
int runLogin(vindauga::AuthManager& auth, const QString& connectionKey) {
    vindauga::AadWebAuthenticator webAuth;
    webAuth.setConnectionKey(connectionKey);
    std::printf("Opening the Entra ID sign-in window (MFA if required)...\n");
    std::fflush(stdout);
    QString error;
    if (!auth.signInInteractive(webAuth, &error)) {
        std::fprintf(stderr, "sign-in failed: %s\n", qUtf8Printable(error));
        return 1;
    }
    std::printf("signed in as %s; feed token expires %s; refresh token stored: %s\n",
                qUtf8Printable(auth.userPrincipalName()),
                qUtf8Printable(fmtExpiry(auth.feedTokenExpiresAt())),
                auth.hasStoredRefreshToken() ? "yes" : "NO");
    return 0;
}

// Connects through the embedded RdpSession (libfreerdp) and shows a real RdpItem in an
// interactive window; mouse/keyboard/resize go through RdpItem's event handlers. With a
// token provider, the AAD/AVD tokens are minted silently and the web view only appears
// as a fallback.
int runRdpConnect(const QString& rdpText, vindauga::AadTokenProvider* tokenProvider,
                  const QString& connectionKey = QString()) {
    // Built directly in C++: vindauga_cli links only the static symbols in vindauga_ui,
    // not the "Vindauga" QML module plugin, so a QQmlEngine could not import RdpItem.
    QQuickWindow window;
    window.resize(1280, 800);
    window.setTitle(QStringLiteral("Vindauga — RDP test (interactive)"));
    window.setColor(Qt::black);

    vindauga::RdpItem rdpItem;
    rdpItem.setAadTokenProvider(tokenProvider);
    rdpItem.setParentItem(window.contentItem());
    rdpItem.setSize(QSizeF(window.width(), window.height()));
    QObject::connect(&window, &QWindow::widthChanged, &rdpItem,
                      [&rdpItem](int w) { rdpItem.setWidth(w); });
    QObject::connect(&window, &QWindow::heightChanged, &rdpItem,
                      [&rdpItem](int h) { rdpItem.setHeight(h); });

    QEventLoop loop;
    int exitCode = 1;
    QObject::connect(&rdpItem, &vindauga::RdpItem::sessionConnected, &loop, [&]() {
        std::printf("Connected! The window is interactive — mouse/keyboard drive the session, "
                    "resizing the window adjusts the resolution.\n");
        std::fflush(stdout);
    });
    QObject::connect(&rdpItem, &vindauga::RdpItem::sessionDisconnected, &loop,
                      [&](const QString& reason) {
                          std::printf("Disconnected: %s\n", reason.isEmpty()
                                                              ? "(unknown reason)"
                                                              : qUtf8Printable(reason));
                          exitCode = 0;
                          loop.quit();
                      });
    QObject::connect(&rdpItem, &vindauga::RdpItem::sessionReconnecting, &loop,
                      [&](const QString& what, int attempt, int maxAttempts) {
                          std::printf("Waiting (%s) — attempt %d of %d...\n",
                                      qUtf8Printable(what), attempt, maxAttempts);
                          std::fflush(stdout);
                      });
    QObject::connect(&window, &QWindow::visibleChanged, &loop, [&](bool visible) {
        if (!visible) {
            std::printf("Window closed by user.\n");
            exitCode = 0;
            loop.quit();
        }
    });

    std::printf("Connecting%s...\n", tokenProvider
                                         ? " (tokens from the stored sign-in; a browser only "
                                           "appears if that fails)"
                                         : " — a small browser window opens for the Entra ID "
                                           "sign-in (MFA if required)");
    std::fflush(stdout);
    window.show();
    rdpItem.connectToSession(rdpText, connectionKey);
    loop.exec();

    window.close();
    return exitCode;
}

int runRdpConnectTest(const QString& rdpPath) {
    QFile file(rdpPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "Could not open: %s\n", qUtf8Printable(rdpPath));
        return 1;
    }
    return runRdpConnect(QString::fromUtf8(file.readAll()), nullptr);
}

// Test 3: end-to-end connect where both the gateway token and the RDSTLS
// proof-of-possession token come from the refresh token. The log shows "obtained
// silently" per token type, or the fallback to the web view.
int runConnect(vindauga::AuthManager& auth, const QString& indexArg, const QString& connectionKey) {
    if (!ensureSignedIn(auth))
        return 1;
    const auto listing = listResources(auth);
    if (!listing.ok)
        return 1;
    bool ok = false;
    const int index = indexArg.toInt(&ok);
    if (!ok || index < 0 || index >= listing.resources.size()) {
        std::fprintf(stderr, "usage: vindauga_cli connect <index 0..%lld>\n",
                     static_cast<long long>(listing.resources.size() - 1));
        return 1;
    }
    const vindauga::RdpResource& resource = listing.resources.at(index);
    if (resource.rdpFileUrl.isEmpty()) {
        std::fprintf(stderr, "resource has no .rdp file URL\n");
        return 1;
    }
    vindauga::FeedClient feed;
    QString error;
    const QByteArray rdp = feed.download(resource.rdpFileUrl, auth.feedAccessToken(), &error);
    if (rdp.isEmpty()) {
        std::fprintf(stderr, ".rdp download failed: %s\n", qUtf8Printable(error));
        return 1;
    }
    std::printf("downloaded .rdp for %s (%lld bytes)\n", qUtf8Printable(resource.title),
                static_cast<long long>(rdp.size()));
    return runRdpConnect(QString::fromUtf8(rdp), &auth, connectionKey);
}
#endif

} // namespace

int main(int argc, char* argv[]) {
#ifdef VINDAUGA_WITH_RDP
    // QtWebEngineQuick::initialize() must be called before QGuiApplication is constructed.
    // QGuiApplication (not QCoreApplication) is required for QQuickWindow/WebEngineView.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    // Same XKB pre-check as the application's main(): this entry point also reaches
    // AadWebAuthenticator/QtWebEngine.
    vindauga::X11KeymapGuard::apply();
#else
    QCoreApplication app(argc, argv);
#endif
    vindauga::initLogging(vindauga::LogLevel::Debug, true);

#ifdef VINDAUGA_VERSION
    std::printf("vindauga_cli %s\n", VINDAUGA_VERSION);
#else
    std::printf("vindauga_cli (unknown version)\n");
#endif

    const QStringList args = QCoreApplication::arguments();
    const QString cmd = args.value(1);
    if (cmd.isEmpty()) {
        std::printf("usage: vindauga_cli <login [connection-id]|list|refresh-test|connect <N> [connection-id]|"
                    "logout|rdp-connect-test <file.rdp>>\n");
        return 0;
    }

    vindauga::AuthManager auth;
    if (cmd == QStringLiteral("list"))
        return runList(auth);
    if (cmd == QStringLiteral("refresh-test"))
        return runRefreshTest(auth);
    if (cmd == QStringLiteral("logout"))
        return runLogout(auth);
#ifdef VINDAUGA_WITH_RDP
    if (cmd == QStringLiteral("login"))
        return runLogin(auth, args.value(2));
    if (cmd == QStringLiteral("connect"))
        return runConnect(auth, args.value(2), args.value(3));
    if (cmd == QStringLiteral("rdp-connect-test")) {
        const QString rdpPath = args.value(2);
        if (rdpPath.isEmpty()) {
            std::printf("usage: vindauga_cli rdp-connect-test <path-to-.rdp-file>\n");
            return 1;
        }
        return runRdpConnectTest(rdpPath);
    }
#endif

    std::printf("unknown command: %s\n", qUtf8Printable(cmd));
    return 1;
}
