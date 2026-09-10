#include <QtTest>
#include "Result.h"
#include "Models.h"

using namespace vindauga;

// Smoke test: verifies that the core library builds and the test framework runs.
class SmokeTest : public QObject {
    Q_OBJECT
private slots:
    void resultOk() {
        auto r = Result<int>::ok(42);
        QVERIFY(r.isOk());
        QCOMPARE(r.value(), 42);
    }
    void resultError() {
        auto r = Result<int>::error(QStringLiteral("boom"));
        QVERIFY(!r.isOk());
        QCOMPARE(r.error(), QStringLiteral("boom"));
    }
    void modelsDefaultConstruct() {
        Workspace w;
        RdpResource res;
        QVERIFY(w.tenantId.isEmpty());
        QVERIFY(res.title.isEmpty());
    }
};

QTEST_MAIN(SmokeTest)
#include "test_smoke.moc"
