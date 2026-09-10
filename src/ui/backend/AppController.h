#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "Connections/ConnectionStore.h"
#include "ConnectionListModel.h"
#include "Settings/AppSettings.h"

namespace vindauga {

// Glue between QML and core. Owns the ConnectionStore, the list model and the settings.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(vindauga::ConnectionListModel* connections READ connectionsModel CONSTANT)
    // Exposed as a property (not a Q_INVOKABLE get/set pair) so QML can bind a CheckBox
    // two-way to it and the session page can pass it on to RdpItem.
    Q_PROPERTY(bool useDisplayScaleFactor READ useDisplayScaleFactor WRITE
                   setUseDisplayScaleFactor NOTIFY useDisplayScaleFactorChanged)
    // availableGpuDevices is CONSTANT (the GPUs in the machine do not change during a
    // running process): a list of {label, path} maps for the settings ComboBox. An empty
    // list (with or without VINDAUGA_WITH_RDP) is safe; the dropdown then only offers
    // the automatic choice.
    Q_PROPERTY(bool hardwareVideoDecoding READ hardwareVideoDecoding WRITE
                   setHardwareVideoDecoding NOTIFY hardwareVideoDecodingChanged)
    Q_PROPERTY(QString preferredVaapiDevice READ preferredVaapiDevice WRITE
                   setPreferredVaapiDevice NOTIFY preferredVaapiDeviceChanged)
    Q_PROPERTY(QVariantList availableGpuDevices READ availableGpuDevices CONSTANT)
    // Same pattern as useDisplayScaleFactor: bound two-way from the settings page and
    // passed on to RdpItem by the session page.
    Q_PROPERTY(int frameBufferMs READ frameBufferMs WRITE setFrameBufferMs NOTIFY
                   frameBufferMsChanged)
public:
    explicit AppController(QObject* parent = nullptr);

    QString version() const;
    ConnectionListModel* connectionsModel() const;
    bool useDisplayScaleFactor() const;
    void setUseDisplayScaleFactor(bool value);
    bool hardwareVideoDecoding() const;
    void setHardwareVideoDecoding(bool value);
    QString preferredVaapiDevice() const;
    void setPreferredVaapiDevice(QString value);
    QVariantList availableGpuDevices() const;
    int frameBufferMs() const;
    void setFrameBufferMs(int value);

    // fileUrl typically comes from a QML FileDialog (selectedFile). Returns the new
    // connection's id, or an empty string on failure.
    Q_INVOKABLE QString importConnection(const QUrl& fileUrl);
    Q_INVOKABLE void removeConnection(const QString& id);
    // Looks up the stored rdpText for id, updates "last used" and emits
    // openSessionRequested. No-op (no emit) if id does not exist.
    Q_INVOKABLE void openConnection(const QString& id);

signals:
    // connectionId is passed on to RdpItem::connectToSession as the key for
    // CredentialStore lookups.
    void openSessionRequested(const QString& rdpText, const QString& connectionId);
    void useDisplayScaleFactorChanged();
    void hardwareVideoDecodingChanged();
    void preferredVaapiDeviceChanged();
    void frameBufferMsChanged();

private:
    std::unique_ptr<ConnectionStore> m_store;
    std::unique_ptr<ConnectionListModel> m_model;
    std::unique_ptr<AppSettings> m_settings;
};

} // namespace vindauga
