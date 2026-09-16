#include "CertificateTrustDialog.h"

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <memory>

#include "Logging.h"

namespace vindauga {

namespace {

constexpr auto kDialogQml = R"QML(
import QtQuick
import QtQuick.Controls.FluentWinUI3

Rectangle {
    id: root
    anchors.fill: parent
    color: "#2b2b2b"

    property bool isChanged: false
    property bool hostMismatch: false
    property string host: ""
    property string commonName: ""
    property string subject: ""
    property string issuer: ""
    property string fingerprint: ""
    property string oldSubject: ""
    property string oldIssuer: ""
    property string oldFingerprint: ""

    // Matches CertificatePrompt::Decision: 0 Reject, 1 AcceptOnce, 2 AcceptAlways.
    signal decisionMade(int decision)

    Component.onCompleted: rejectButton.forceActiveFocus()

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: column.implicitHeight

        Column {
            id: column
            width: parent.width
            spacing: 10

            Label {
                text: root.isChanged ? qsTr("The server's certificate has changed")
                                      : qsTr("Unknown server certificate")
                color: "white"
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                text: root.isChanged
                      ? qsTr("The certificate presented by %1 no longer matches the one previously trusted. This can happen after a legitimate certificate renewal, but it can also mean the connection is being intercepted.").arg(root.host)
                      : qsTr("Vindauga has not connected to %1 before and cannot verify this certificate automatically. Only continue if you trust this server.").arg(root.host)
                color: "#cccccc"
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Label {
                visible: root.hostMismatch
                text: qsTr("Warning: the certificate name does not match the server hostname.")
                color: "#ff8080"
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Label { text: qsTr("Subject: %1").arg(root.subject); color: "white"; wrapMode: Text.WordWrap; width: parent.width }
            Label { text: qsTr("Issuer: %1").arg(root.issuer); color: "white"; wrapMode: Text.WordWrap; width: parent.width }
            Label { text: qsTr("Fingerprint: %1").arg(root.fingerprint); color: "white"; wrapMode: Text.WordWrap; width: parent.width }

            Label {
                visible: root.isChanged
                text: qsTr("Previously trusted subject: %1").arg(root.oldSubject)
                color: "#cccccc"
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Label {
                visible: root.isChanged
                text: qsTr("Previously trusted issuer: %1").arg(root.oldIssuer)
                color: "#cccccc"
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Label {
                visible: root.isChanged
                text: qsTr("Previously trusted fingerprint: %1").arg(root.oldFingerprint)
                color: "#cccccc"
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                spacing: 8
                anchors.right: parent.right

                Button {
                    id: rejectButton
                    text: qsTr("Reject")
                    onClicked: root.decisionMade(0)
                }
                Button {
                    text: qsTr("Trust once")
                    onClicked: root.decisionMade(1)
                }
                Button {
                    text: qsTr("Trust always")
                    onClicked: root.decisionMade(2)
                }
            }
        }
    }
}
)QML";

} // namespace

CertificateTrustDialog::CertificateTrustDialog(QObject* parent) : QObject(parent) {
    // Named explicitly (rather than relying on Q_DECLARE_METATYPE's fully-qualified
    // "vindauga::CertificateInfo") because moc records the promptOnGuiThread parameter
    // type as written in the header — unqualified "CertificateInfo", since the
    // declaration is lexically inside namespace vindauga. QMetaObject::invokeMethod
    // matches by that literal string, so the metatype must be registered under it too.
    qRegisterMetaType<CertificateInfo>("CertificateInfo");
}

void CertificateTrustDialog::setHostWindow(QQuickWindow* window) {
    m_hostWindow = window;
}

CertificatePrompt::Decision CertificateTrustDialog::promptForCertificate(const CertificateInfo& info) {
    int decision = 0;
    if (QThread::currentThread() == thread()) {
        decision = promptOnGuiThread(info);
    } else {
        QMetaObject::invokeMethod(this, "promptOnGuiThread", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(int, decision), Q_ARG(CertificateInfo, info));
    }
    switch (decision) {
    case 2:
        return Decision::AcceptAlways;
    case 1:
        return Decision::AcceptOnce;
    default:
        return Decision::Reject;
    }
}

int CertificateTrustDialog::promptOnGuiThread(const CertificateInfo& info) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArray(kDialogQml), QUrl());

    std::unique_ptr<QObject> rootObj(component.create());
    if (!rootObj) {
        qCWarning(lcRdp) << "Could not instantiate certificate trust dialog:"
                         << component.errorString();
        return 0; // reject: an unrenderable prompt must not silently accept the certificate
    }
    auto* root = qobject_cast<QQuickItem*>(rootObj.get());
    if (!root) {
        qCWarning(lcRdp) << "Unexpected root type for certificate trust dialog";
        return 0;
    }

    std::unique_ptr<QQuickWindow> ownWindow;
    if (m_hostWindow) {
        root->setParentItem(m_hostWindow->contentItem());
    } else {
        ownWindow = std::make_unique<QQuickWindow>();
        root->setParentItem(ownWindow->contentItem());
        ownWindow->resize(480, 420);
        ownWindow->setTitle(tr("Vindauga — Certificate warning"));
    }

    connect(root, SIGNAL(decisionMade(int)), this, SLOT(onDecisionMade(int)));

    root->setProperty("isChanged", info.isChanged);
    root->setProperty("hostMismatch", info.hostMismatch);
    root->setProperty("host", info.host);
    root->setProperty("commonName", info.commonName);
    root->setProperty("subject", info.subject);
    root->setProperty("issuer", info.issuer);
    root->setProperty("fingerprint", info.fingerprint);
    root->setProperty("oldSubject", info.oldSubject);
    root->setProperty("oldIssuer", info.oldIssuer);
    root->setProperty("oldFingerprint", info.oldFingerprint);

    m_result = Decision::Reject;
    QEventLoop loop;
    m_requestLoop = &loop;

    if (ownWindow)
        ownWindow->show();
    loop.exec();
    m_requestLoop = nullptr;
    if (ownWindow)
        ownWindow->close();
    else
        root->setParentItem(nullptr); // remove the overlay from the host window

    switch (m_result) {
    case Decision::AcceptAlways:
        return 2;
    case Decision::AcceptOnce:
        return 1;
    default:
        return 0;
    }
}

void CertificateTrustDialog::onDecisionMade(int decision) {
    switch (decision) {
    case 2:
        m_result = Decision::AcceptAlways;
        break;
    case 1:
        m_result = Decision::AcceptOnce;
        break;
    default:
        m_result = Decision::Reject;
        break;
    }
    if (m_requestLoop)
        m_requestLoop->quit();
}

} // namespace vindauga
