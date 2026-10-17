#include <QtTest>

#include "Feed/FeedClient.h"

using namespace vindauga;

// Trust check for URLs taken from feed XML; no network.
class FeedClientTest : public QObject {
    Q_OBJECT
private slots:
    void acceptsMicrosoftHttpsHosts() {
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://rdweb.wvd.microsoft.com/api/arm/feeddiscovery"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://www.wvd.microsoft.com/x.rdp?hash=1"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://login.microsoftonline.com/"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://MICROSOFT.com/"))));
        // Sovereign clouds.
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://rdweb.wvd.azure.us/api/arm/feeddiscovery"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://rdweb.wvd.azure.cn/api/arm/feeddiscovery"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://login.microsoftonline.us/"))));
        QVERIFY(FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://login.chinacloudapi.cn/"))));
    }

    void rejectsLookalikesAndPlainHttp() {
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://evilmicrosoft.com/"))));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://microsoft.com.evil.io/"))));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://notazure.us/"))));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("https://azure.us.evil.io/"))));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("http://rdweb.wvd.microsoft.com/"))));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl()));
        QVERIFY(!FeedClient::isTrustedUrl(QUrl(QStringLiteral("not a url"))));
    }

    void refusesToDownloadFromUntrustedHost() {
        FeedClient client;
        QString error;
        const QByteArray data = client.download(QUrl(QStringLiteral("https://evil.example/x.rdp")),
                                                QStringLiteral("token"), &error);
        QVERIFY(data.isEmpty());
        QVERIFY(error.contains(QStringLiteral("untrusted")));
    }
};

QTEST_MAIN(FeedClientTest)
#include "test_feedclient.moc"
