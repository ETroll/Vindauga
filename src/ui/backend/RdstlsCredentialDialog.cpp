#include "RdstlsCredentialDialog.h"

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

// Small temporary form: username/domain/password (masked).
constexpr auto kDialogQml = R"QML(
import QtQuick
import QtQuick.Controls.FluentWinUI3

Rectangle {
    id: root
    anchors.fill: parent
    color: "#2b2b2b"

    property string suggestedUsername: ""
    property string suggestedDomain: ""

    signal credentialsAccepted(string username, string domain, string password)
    signal credentialsCancelled()

    Component.onCompleted: {
        userField.text = suggestedUsername
        userField.forceActiveFocus()
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        width: 280

        Label {
            text: qsTr("Sign in to the desktop")
            color: "white"
            font.bold: true
            font.pixelSize: 16
        }
        Label {
            text: qsTr("The Entra ID sign-in alone isn't enough for this connection. Username is your UPN/email.")
            color: "#cccccc"
            wrapMode: Text.WordWrap
            width: parent.width
        }
        TextField {
            id: userField
            width: parent.width
            placeholderText: qsTr("Username (UPN, e.g. name@company.com)")
        }
        TextField {
            id: passField
            width: parent.width
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            Keys.onReturnPressed: okButton.clicked()
        }
        Row {
            spacing: 8
            anchors.right: parent.right
            Button {
                text: qsTr("Cancel")
                onClicked: root.credentialsCancelled()
            }
            Button {
                id: okButton
                text: qsTr("Connect")
                // The domain is not asked for: it is always "AzureAD" in this ARM/AAD
                // scenario (FreeRDP's own default).
                onClicked: root.credentialsAccepted(userField.text, suggestedDomain, passField.text)
            }
        }
    }
}
)QML";

} // namespace

RdstlsCredentialDialog::RdstlsCredentialDialog(QObject* parent) : QObject(parent) {}

void RdstlsCredentialDialog::setHostWindow(QQuickWindow* window) {
    m_hostWindow = window;
}

void RdstlsCredentialDialog::setConnectionKey(const QString& key) {
    m_connectionKey = key;
}

void RdstlsCredentialDialog::clearCachedCredentials() {
    if (!m_connectionKey.isEmpty())
        m_credentialStore.remove(m_connectionKey);
}

QStringList RdstlsCredentialDialog::requestCredentials(const QString& suggestedUsername,
                                                        const QString& suggestedDomain) {
    QStringList result;
    if (QThread::currentThread() == thread()) {
        // Called directly on the GUI thread: either the RdpSession worker thread has not
        // started yet (RdpItem::connectToSession's proactive call before session->start(),
        // the common case) or some other GUI-thread use.
        result = requestOnGuiThread(suggestedUsername, suggestedDomain);
    } else {
        QMetaObject::invokeMethod(this, "requestOnGuiThread", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(QStringList, result),
                                  Q_ARG(QString, suggestedUsername),
                                  Q_ARG(QString, suggestedDomain));
    }
    return result;
}

QStringList RdstlsCredentialDialog::requestOnGuiThread(const QString& suggestedUsername,
                                                        const QString& suggestedDomain) {
    m_usedCachedCredentials = false;
    if (!m_connectionKey.isEmpty()) {
        const CredentialStore::Credentials cached = m_credentialStore.load(m_connectionKey);
        if (cached.found) {
            qCDebug(lcRdp) << "Using cached RDSTLS credentials from keychain "
                              "(username length"
                           << cached.username.size() << ") — skipping the dialog";
            m_usedCachedCredentials = true;
            const QString domain = suggestedDomain.isEmpty() ? QStringLiteral("AzureAD")
                                                              : suggestedDomain;
            return { cached.username, domain, cached.password };
        }
    }

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArray(kDialogQml), QUrl());

    std::unique_ptr<QObject> rootObj(component.create());
    if (!rootObj) {
        qCWarning(lcRdp) << "Could not instantiate RDSTLS credential dialog:"
                         << component.errorString();
        return {};
    }
    auto* root = qobject_cast<QQuickItem*>(rootObj.get());
    if (!root) {
        qCWarning(lcRdp) << "Unexpected root type for RDSTLS credential dialog";
        return {};
    }

    // With a host window (typically RdpItem::window()) the dialog is an overlay inside
    // it rather than a separate top-level window. "anchors.fill: parent" in kDialogQml
    // binds to whatever the parent turns out to be, so the same QML serves both cases.
    std::unique_ptr<QQuickWindow> ownWindow;
    if (m_hostWindow) {
        root->setParentItem(m_hostWindow->contentItem());
    } else {
        ownWindow = std::make_unique<QQuickWindow>();
        root->setParentItem(ownWindow->contentItem());
        ownWindow->resize(340, 260);
        ownWindow->setTitle(tr("Vindauga — Desktop sign-in"));
    }

    connect(root, SIGNAL(credentialsAccepted(QString, QString, QString)), this,
            SLOT(onCredentialsAccepted(QString, QString, QString)));
    connect(root, SIGNAL(credentialsCancelled()), this, SLOT(onCredentialsCancelled()));

    root->setProperty("suggestedUsername", suggestedUsername);
    root->setProperty("suggestedDomain", suggestedDomain);

    m_result.clear();
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

    return m_result;
}

void RdstlsCredentialDialog::onCredentialsAccepted(const QString& username, const QString& domain,
                                                    const QString& password) {
    m_result = { username, domain, password };
    if (!m_connectionKey.isEmpty())
        m_credentialStore.save(m_connectionKey, username, password);
    if (m_requestLoop)
        m_requestLoop->quit();
}

void RdstlsCredentialDialog::onCredentialsCancelled() {
    m_result.clear();
    if (m_requestLoop)
        m_requestLoop->quit();
}

} // namespace vindauga
