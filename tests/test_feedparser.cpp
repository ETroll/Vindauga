#include <QtTest>

#include "Feed/FeedParser.h"

using namespace vindauga;

namespace {
QByteArray readFixture(const char* name) {
    QFile f(QString::fromUtf8(FIXTURES_DIR "/%1").arg(QString::fromLatin1(name)));
    const bool opened = f.open(QIODevice::ReadOnly);
    Q_ASSERT(opened);
    return f.readAll();
}
} // namespace

// Verifies discovery and web-feed XML parsing against fixtures.
class FeedParserTest : public QObject {
    Q_OBJECT
private slots:
    void parsesDiscoveryTenants() {
        const auto xml = readFixture("discovery.xml");
        const QList<Workspace> workspaces = FeedParser::parseDiscovery(xml);

        QCOMPARE(workspaces.size(), 2);

        const Workspace& first = workspaces.at(0);
        QCOMPARE(first.tenantId, QStringLiteral("11111111-1111-1111-1111-111111111111"));
        QCOMPARE(first.displayName, QStringLiteral("REVD Workspace"));
        QCOMPARE(first.geo, QStringLiteral("EU"));
        QVERIFY(first.conditionalAccessClaims.isEmpty());
        QCOMPARE(first.feedUrl.host(), QStringLiteral("www.wvd.microsoft.com"));
        // Query parameters (including repeated keys) must be preserved unchanged.
        QVERIFY(first.feedUrl.toString().contains(
            QStringLiteral("showInFeedMonikers=CloudPC&showInFeedMonikers=CloudPCReserve")));

        const Workspace& second = workspaces.at(1);
        QCOMPARE(second.tenantId, QStringLiteral("22222222-2222-2222-2222-222222222222"));
        QCOMPARE(second.conditionalAccessClaims, QStringLiteral("claims-challenge-token"));
    }

    void parsesEmptyDiscovery() {
        const auto xml = readFixture("discovery_empty.xml");
        const QList<Workspace> workspaces = FeedParser::parseDiscovery(xml);
        QVERIFY(workspaces.isEmpty());
    }

    void parsesWebFeedResources() {
        const auto xml = readFixture("webfeed.xml");
        const QList<RdpResource> resources =
            FeedParser::parseWebFeed(xml, QStringLiteral("11111111-1111-1111-1111-111111111111"));

        QCOMPARE(resources.size(), 2);

        const RdpResource& desktop = resources.at(0);
        QCOMPARE(desktop.id, QStringLiteral("res-hostpool04"));
        QCOMPARE(desktop.title, QStringLiteral("hostpool04"));
        QCOMPARE(desktop.type, QStringLiteral("Desktop"));
        QCOMPARE(desktop.deviceState, QStringLiteral("Available"));
        QCOMPARE(desktop.workspaceTenantId, QStringLiteral("11111111-1111-1111-1111-111111111111"));
        QVERIFY(!desktop.iconUrl.isEmpty());
        QVERIFY(!desktop.rdpFileUrl.isEmpty());
        // hash and endpointId in the query string must be preserved (required by the feed API).
        QVERIFY(desktop.rdpFileUrl.toString().contains(QStringLiteral("hash=abc123")));
        QVERIFY(desktop.rdpFileUrl.toString().contains(QStringLiteral("endpointId=ep-1")));

        // The second resource lacks optional fields (icon, .rdp file) and must still parse.
        const RdpResource& remoteApp = resources.at(1);
        QCOMPARE(remoteApp.id, QStringLiteral("res-remoteapp01"));
        QCOMPARE(remoteApp.type, QStringLiteral("RemoteApp"));
        QVERIFY(remoteApp.iconUrl.isEmpty());
        QVERIFY(remoteApp.rdpFileUrl.isEmpty());
    }

    void parsesEmptyWebFeed() {
        const auto xml = readFixture("webfeed_empty.xml");
        const QList<RdpResource> resources =
            FeedParser::parseWebFeed(xml, QStringLiteral("11111111-1111-1111-1111-111111111111"));
        QVERIFY(resources.isEmpty());
    }

    void handlesMalformedXmlWithoutCrashing() {
        const QByteArray garbage = "<not><valid";
        QVERIFY(FeedParser::parseDiscovery(garbage).isEmpty());
        QVERIFY(FeedParser::parseWebFeed(garbage, QStringLiteral("t")).isEmpty());
    }
};

QTEST_MAIN(FeedParserTest)
#include "test_feedparser.moc"
