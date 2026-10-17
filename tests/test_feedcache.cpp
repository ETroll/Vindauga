#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Feed/FeedCache.h"

using namespace vindauga;

namespace {

Workspace makeWorkspace(const QString& tenantId, const QString& name) {
    Workspace w;
    w.tenantId = tenantId;
    w.displayName = name;
    w.geo = QStringLiteral("EU");
    w.feedUrl = QUrl(QStringLiteral("https://rdweb.wvd.microsoft.com/api/arm/feed/") + tenantId);
    return w;
}

RdpResource makeResource(const QString& id, const QString& title, const QString& tenantId) {
    RdpResource r;
    r.id = id;
    r.title = title;
    r.type = QStringLiteral("Desktop");
    r.deviceState = QStringLiteral("Available");
    r.rdpFileUrl = QUrl(QStringLiteral("https://rdweb.wvd.microsoft.com/rdp/") + id);
    r.iconPngUrl = QUrl(QStringLiteral("https://rdweb.wvd.microsoft.com/icon/") + id);
    r.workspaceTenantId = tenantId;
    return r;
}

} // namespace

// FeedCache round-trips through feed.json, keeps icons/.rdp text across refreshes for
// resources that persist, and drops those of resources that vanished.
class FeedCacheTest : public QObject {
    Q_OBJECT
private slots:
    void startsEmpty() {
        QTemporaryDir dir;
        FeedCache cache(dir.path());
        QVERIFY(!cache.isValid());
        QVERIFY(cache.resources().isEmpty());
        QVERIFY(cache.resourceById(QStringLiteral("x")).resource.id.isEmpty());
    }

    void persistsAcrossInstances() {
        QTemporaryDir dir;
        {
            FeedCache cache(dir.path());
            cache.update({ makeWorkspace(QStringLiteral("t1"), QStringLiteral("Workspace One")) },
                         { makeResource(QStringLiteral("a"), QStringLiteral("Desktop A"), QStringLiteral("t1")),
                           makeResource(QStringLiteral("b"), QStringLiteral("Desktop B"), QStringLiteral("t1")) });
            QVERIFY(cache.isValid());
            QVERIFY(cache.refreshedAt().isValid());
            cache.storeIcon(QStringLiteral("a"), QByteArray("\x89PNG-not-really"));
            cache.storeRdpText(QStringLiteral("b"), QStringLiteral("full address:s:host\n"));
        }
        FeedCache reloaded(dir.path());
        QVERIFY(reloaded.isValid());
        QCOMPARE(reloaded.workspaces().size(), 1);
        QCOMPARE(reloaded.resources().size(), 2);
        const FeedCache::CachedResource a = reloaded.resourceById(QStringLiteral("a"));
        QCOMPARE(a.resource.title, QStringLiteral("Desktop A"));
        QCOMPARE(a.workspaceName, QStringLiteral("Workspace One"));
        QVERIFY(!a.iconFile.isEmpty());
        QVERIFY(QFile::exists(a.iconFile));
        QVERIFY(a.iconFile.startsWith(dir.path()));
        const FeedCache::CachedResource b = reloaded.resourceById(QStringLiteral("b"));
        QCOMPARE(b.rdpText, QStringLiteral("full address:s:host\n"));
        QVERIFY(b.iconFile.isEmpty());
        // Only b still needs its icon.
        const QList<RdpResource> missing = reloaded.resourcesMissingIcons();
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing.first().id, QStringLiteral("b"));
    }

    void refreshKeepsSurvivorsAndDropsVanished() {
        QTemporaryDir dir;
        FeedCache cache(dir.path());
        cache.update({ makeWorkspace(QStringLiteral("t1"), QStringLiteral("W")) },
                     { makeResource(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("t1")),
                       makeResource(QStringLiteral("gone"), QStringLiteral("Gone"), QStringLiteral("t1")) });
        cache.storeIcon(QStringLiteral("a"), QByteArray("png-a"));
        cache.storeIcon(QStringLiteral("gone"), QByteArray("png-gone"));
        cache.storeRdpText(QStringLiteral("a"), QStringLiteral("rdp-a"));
        const QString goneIcon = cache.resourceById(QStringLiteral("gone")).iconFile;
        QVERIFY(QFile::exists(goneIcon));

        // Same id, new title: icon and .rdp text carried over, title updated.
        RdpResource renamed = makeResource(QStringLiteral("a"), QStringLiteral("A renamed"), QStringLiteral("t1"));
        cache.update({ makeWorkspace(QStringLiteral("t1"), QStringLiteral("W")) }, { renamed });
        QCOMPARE(cache.resources().size(), 1);
        const FeedCache::CachedResource a = cache.resourceById(QStringLiteral("a"));
        QCOMPARE(a.resource.title, QStringLiteral("A renamed"));
        QVERIFY(!a.iconFile.isEmpty());
        QCOMPARE(a.rdpText, QStringLiteral("rdp-a"));
        QVERIFY(!QFile::exists(goneIcon));
        QVERIFY(cache.resourcesMissingIcons().isEmpty());

        // A changed icon URL invalidates the stored icon and removes its file.
        const QString oldIcon = a.iconFile;
        RdpResource newIcon = renamed;
        newIcon.iconPngUrl = QUrl(QStringLiteral("https://rdweb.wvd.microsoft.com/icon/a-v2"));
        cache.update({ makeWorkspace(QStringLiteral("t1"), QStringLiteral("W")) }, { newIcon });
        QVERIFY(cache.resourceById(QStringLiteral("a")).iconFile.isEmpty());
        QVERIFY(!QFile::exists(oldIcon));
        QCOMPARE(cache.resourcesMissingIcons().size(), 1);
    }

    void clearRemovesEverything() {
        QTemporaryDir dir;
        FeedCache cache(dir.path());
        cache.update({ makeWorkspace(QStringLiteral("t1"), QStringLiteral("W")) },
                     { makeResource(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("t1")) });
        cache.storeIcon(QStringLiteral("a"), QByteArray("png"));
        const QString icon = cache.resourceById(QStringLiteral("a")).iconFile;
        cache.clear();
        QVERIFY(!cache.isValid());
        QVERIFY(cache.resources().isEmpty());
        QVERIFY(!QFile::exists(icon));
        FeedCache reloaded(dir.path());
        QVERIFY(!reloaded.isValid());
    }

    void toleratesGarbageFile() {
        QTemporaryDir dir;
        QFile file(dir.path() + QStringLiteral("/feed.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        FeedCache cache(dir.path());
        QVERIFY(!cache.isValid());
        QVERIFY(cache.resources().isEmpty());
    }
};

QTEST_MAIN(FeedCacheTest)
#include "test_feedcache.moc"
