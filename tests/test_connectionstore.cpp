#include <QtTest>

#include "Connections/ConnectionStore.h"

using namespace vindauga;

// Verifies ConnectionStore: import, persistence, removal and robustness. Uses the
// synthetic tests/fixtures/sample.rdp; no network access is involved.
class ConnectionStoreTest : public QObject {
    Q_OBJECT
private slots:
    void importsAndPersistsAcrossInstances() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString id;
        {
            ConnectionStore store(dir.path());
            id = store.importFromFile(QString::fromUtf8(FIXTURES_DIR "/sample.rdp"));
            QVERIFY(!id.isEmpty());

            const QList<SavedConnection> conns = store.connections();
            QCOMPARE(conns.size(), 1);
            QCOMPARE(conns.first().displayName, QStringLiteral("hostpool04"));
            QVERIFY(!conns.first().rdpText.isEmpty());
            QVERIFY(conns.first().addedAt.isValid());
        }

        // A new instance on the same directory must see the persisted connection.
        ConnectionStore reopened(dir.path());
        const QList<SavedConnection> conns = reopened.connections();
        QCOMPARE(conns.size(), 1);
        QCOMPARE(conns.first().id, id);
        QCOMPARE(conns.first().displayName, QStringLiteral("hostpool04"));
    }

    void importOfMissingFileFailsGracefully() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ConnectionStore store(dir.path());

        const QString id = store.importFromFile(QStringLiteral("/does/not/exist.rdp"));
        QVERIFY(id.isEmpty());
        QCOMPARE(store.connections().size(), 0);
    }

    void removeDeletesAndPersists() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ConnectionStore store(dir.path());
        const QString id = store.importFromFile(QString::fromUtf8(FIXTURES_DIR "/sample.rdp"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(store.connections().size(), 1);

        store.remove(id);
        QCOMPARE(store.connections().size(), 0);

        ConnectionStore reopened(dir.path());
        QCOMPARE(reopened.connections().size(), 0);
    }

    void touchLastUsedReordersToFront() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ConnectionStore store(dir.path());

        const QString firstId = store.importFromFile(QString::fromUtf8(FIXTURES_DIR "/sample.rdp"));
        QVERIFY(!firstId.isEmpty());
        // QDateTime::currentDateTimeUtc() has millisecond resolution; a short wait
        // guarantees strictly different timestamps so the ordering check is not flaky.
        QTest::qWait(5);
        // Second "connection": same fixture, only the ordering matters here.
        const QString secondId = store.importFromFile(QString::fromUtf8(FIXTURES_DIR "/sample.rdp"));
        QVERIFY(!secondId.isEmpty());
        QVERIFY(firstId != secondId);

        // secondId was imported last and therefore has the newest lastUsedAt.
        QCOMPARE(store.connections().first().id, secondId);

        QTest::qWait(5);
        store.touchLastUsed(firstId);
        QCOMPARE(store.connections().first().id, firstId);
    }
};

QTEST_MAIN(ConnectionStoreTest)
#include "test_connectionstore.moc"
