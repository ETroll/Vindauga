#include <QtTest>

#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "AadWebAuthenticator.h"
#include "X11KeymapGuard.h"

using namespace vindauga;

namespace {
// Builds a data: URL that, via JS after a short delay, navigates to a local "redirect"
// URL with the given query string. No real network traffic; this exercises only the
// capture mechanism (onNavigationRequested/IgnoreRequest/query parsing), which is the
// same kind of navigation a real Microsoft redirect performs.
QUrl makeRedirectingDataUrl(const QString& targetUrl) {
    const QString html = QStringLiteral("<html><body><script>setTimeout(function(){"
                                        "window.location.href='%1';"
                                        "}, 200);</script></body></html>")
                             .arg(targetUrl);
    return QUrl(QStringLiteral("data:text/html,") +
               QString::fromUtf8(QUrl::toPercentEncoding(html)));
}

// Calls captureAuthorizationCode from a thread other than the test's (as the RdpSession
// worker thread would) while the main thread runs a QEventLoop. Required for the
// Qt::BlockingQueuedConnection inside AadWebAuthenticator to be delivered; without a
// running event loop on the receiving thread it would deadlock.
QString captureFromWorkerThread(AadWebAuthenticator& auth, const QUrl& authUrl,
                                const QString& redirectPrefix) {
    QString result;
    QEventLoop loop;
    QThread* worker = QThread::create([&]() {
        result = auth.captureAuthorizationCode(authUrl, redirectPrefix);
    });
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();

    QTimer safety;
    safety.setSingleShot(true);
    QObject::connect(&safety, &QTimer::timeout, &loop, &QEventLoop::quit);
    safety.start(20000);

    loop.exec();
    worker->wait();
    delete worker;
    return result;
}
} // namespace

// Verifies that AadWebAuthenticator captures an OAuth redirect (code or error) from a
// locally simulated redirect, without real network access or MFA.
class AadWebAuthenticatorTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QtWebEngineQuick::initialize();
        // Same pre-check as main(): without it this test crashes (SIGTRAP, "Keymap file
        // failed to load") on X11 hosts with an invalid XKB variant in _XKB_RULES_NAMES.
        X11KeymapGuard::apply();
    }

    void capturesCodeFromRedirect() {
        AadWebAuthenticator auth;
        const QUrl authUrl =
            makeRedirectingDataUrl(QStringLiteral("http://127.0.0.1:1/redirect?code=test-code-123"));

        const QString code =
            captureFromWorkerThread(auth, authUrl, QStringLiteral("http://127.0.0.1:1/redirect"));

        QCOMPARE(code, QStringLiteral("test-code-123"));
    }

    void returnsEmptyOnAuthError() {
        AadWebAuthenticator auth;
        const QUrl authUrl = makeRedirectingDataUrl(
            QStringLiteral("http://127.0.0.1:1/redirect?error=access_denied"));

        const QString code =
            captureFromWorkerThread(auth, authUrl, QStringLiteral("http://127.0.0.1:1/redirect"));

        QVERIFY(code.isEmpty());
    }
};

QTEST_MAIN(AadWebAuthenticatorTest)
#include "test_aadwebauthenticator.moc"
