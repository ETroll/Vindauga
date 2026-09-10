#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QStringList>
#include <cstdio>

#include "Auth/AuthManager.h"

#ifdef VINDAUGA_WITH_RDP
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "RdpItem.h"
#include "X11KeymapGuard.h"
#endif

// Headless CLI for exercising core/ components without the GUI.

namespace {

// Runs the interactive device-code login and reports only token length/expiry, never the token.
int runAuthTest() {
    vindauga::AuthManager auth;
    QEventLoop loop;
    int exitCode = 1;

    QObject::connect(&auth, &vindauga::AuthManager::verificationCodeReady, &loop,
                      [&](const QUrl& verificationUrl, const QString& userCode) {
                          std::printf("Go to %s and enter the code: %s\n",
                                      qUtf8Printable(verificationUrl.toString()),
                                      qUtf8Printable(userCode));
                          std::printf(
                              "(the system browser opens automatically with the code pre-filled)\n");
                          std::fflush(stdout);
                      });
    QObject::connect(&auth, &vindauga::AuthManager::feedTokenAcquired, &loop,
                      [&](const QString& token) {
                          const qint64 minutesLeft =
                              QDateTime::currentDateTime().secsTo(auth.expiresAt()) / 60;
                          std::printf("token OK, length %lld, expires in %lld min\n",
                                      static_cast<long long>(token.size()),
                                      static_cast<long long>(minutesLeft));
                          exitCode = 0;
                          loop.quit();
                      });
    QObject::connect(&auth, &vindauga::AuthManager::loginFailed, &loop,
                      [&](const QString& message) {
                          std::fprintf(stderr, "login failed: %s\n", qUtf8Printable(message));
                          exitCode = 1;
                          loop.quit();
                      });

    std::printf("Starting sign-in (device-code flow, MFA if required)...\n");
    std::printf("(client_id from VINDAUGA_CLIENT_ID if set, otherwise the default candidate)\n");
    std::fflush(stdout);
    auth.login();
    loop.exec();
    return exitCode;
}

#ifdef VINDAUGA_WITH_RDP
// Connects through the embedded RdpSession (libfreerdp) and shows a real RdpItem in an
// interactive window; mouse/keyboard/resize go through RdpItem's event handlers. The AAD
// step is captured by an embedded QtWebEngine view (AadWebAuthenticator).
int runRdpConnectTest(const QString& rdpPath) {
    QFile file(rdpPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "Could not open: %s\n", qUtf8Printable(rdpPath));
        return 1;
    }
    const QString rdpText = QString::fromUtf8(file.readAll());

    // Built directly in C++: vindauga_cli links only the static symbols in vindauga_ui,
    // not the "Vindauga" QML module plugin, so a QQmlEngine could not import RdpItem.
    QQuickWindow window;
    window.resize(1280, 800);
    window.setTitle(QStringLiteral("Vindauga — RDP test (interactive)"));
    window.setColor(Qt::black);

    vindauga::RdpItem rdpItem;
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

    std::printf("Connecting — a small browser window opens for the Entra ID sign-in "
                "(MFA if required)...\n");
    std::fflush(stdout);
    window.show();
    rdpItem.connectToSession(rdpText);
    loop.exec();

    window.close();
    return exitCode;
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

#ifdef VINDAUGA_VERSION
    std::printf("vindauga_cli %s\n", VINDAUGA_VERSION);
#else
    std::printf("vindauga_cli (unknown version)\n");
#endif

    const QStringList args = QCoreApplication::arguments();
    const QString cmd = args.value(1);
    if (cmd.isEmpty()) {
        std::printf("usage: vindauga_cli <auth-test|list|connect|rdp-connect-test>\n");
        std::printf("(auth-test=device-code login test, list/connect=unimplemented, "
                    "rdp-connect-test=interactive RDP session test)\n");
        return 0;
    }
    if (cmd == QStringLiteral("auth-test"))
        return runAuthTest();
#ifdef VINDAUGA_WITH_RDP
    if (cmd == QStringLiteral("rdp-connect-test")) {
        const QString rdpPath = args.value(2);
        if (rdpPath.isEmpty()) {
            std::printf("usage: vindauga_cli rdp-connect-test <path-to-.rdp-file>\n");
            return 1;
        }
        return runRdpConnectTest(rdpPath);
    }
#endif

    std::printf("unimplemented command: %s\n", qUtf8Printable(cmd));
    return 1;
}
