#include <QtTest>

#include <QFile>

#include <rhi/qshader.h>

// RdpRenderNode loads its compiled .qsb shaders from Qt resources (qFatal on failure)
// only once a live RDP session reaches DesktopResize, which a unit test cannot reach.
// This test verifies what can be checked without a session: that qt6_add_shaders
// produced and embedded both .qsb resources at the paths RdpRenderNode requests, and
// that each deserializes to a valid QShader of the expected stage. It does not cover
// the shader logic itself or the QRhi pipeline setup, which needs a GPU-backed
// QQuickWindow.
class TestRdpRenderNode : public QObject {
    Q_OBJECT

private slots:
    void vertexShaderResourceIsValid() {
        QFile file(QStringLiteral(":/vindauga/shaders/rdprender.vert.qsb"));
        QVERIFY2(file.open(QFile::ReadOnly), "rdprender.vert.qsb not found as Qt resource");
        const QByteArray data = file.readAll();
        QVERIFY2(!data.isEmpty(), "rdprender.vert.qsb is empty");
        const QShader shader = QShader::fromSerialized(data);
        QVERIFY2(shader.isValid(), "rdprender.vert.qsb does not deserialize to a valid QShader");
        QCOMPARE(shader.stage(), QShader::VertexStage);
    }

    void fragmentShaderResourceIsValid() {
        QFile file(QStringLiteral(":/vindauga/shaders/rdprender.frag.qsb"));
        QVERIFY2(file.open(QFile::ReadOnly), "rdprender.frag.qsb not found as Qt resource");
        const QByteArray data = file.readAll();
        QVERIFY2(!data.isEmpty(), "rdprender.frag.qsb is empty");
        const QShader shader = QShader::fromSerialized(data);
        QVERIFY2(shader.isValid(), "rdprender.frag.qsb does not deserialize to a valid QShader");
        QCOMPARE(shader.stage(), QShader::FragmentStage);
    }
};

QTEST_MAIN(TestRdpRenderNode)
#include "test_rdprendernode.moc"
