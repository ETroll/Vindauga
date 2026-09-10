#include <QtTest>

#include "Rdp/RdpSession.h"

using namespace vindauga;

namespace {
QString readFixture(const char* name) {
    QFile f(QString::fromUtf8(FIXTURES_DIR "/%1").arg(QString::fromLatin1(name)));
    const bool opened = f.open(QIODevice::ReadOnly | QIODevice::Text);
    Q_ASSERT(opened);
    return QString::fromUtf8(f.readAll());
}
} // namespace

// Verifies FreeRDP context creation and .rdp settings parsing; no connection or callbacks.
class RdpSessionTest : public QObject {
    Q_OBJECT
private slots:
    void parsesSampleRdpWithoutCrashing() {
        const QString rdpText = readFixture("sample.rdp");
        RdpSession session(rdpText, QStringLiteral("dummy-token"));

        QVERIFY(session.contextValid());
        // FreeRDP splits "host:port" from the .rdp file and stores the port separately;
        // gatewayHostname() returns only the host name.
        QCOMPARE(session.gatewayHostname(), QStringLiteral("afdfp-rdgateway-r0.wvd.microsoft.com"));
        QVERIFY(session.isArmTransport());
    }

    void handlesGarbageRdpWithoutCrashing() {
        RdpSession session(QStringLiteral("not a valid rdp file\n\x01\x02"),
                            QStringLiteral("dummy-token"));
        // Must not crash. The context may end up valid (empty settings) or invalid
        // depending on how tolerant FreeRDP's parser is; both are acceptable.
        Q_UNUSED(session);
    }
};

QTEST_MAIN(RdpSessionTest)
#include "test_rdpsession.moc"
