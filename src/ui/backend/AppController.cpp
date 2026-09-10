#include "AppController.h"

#ifdef VINDAUGA_WITH_RDP
#include "Rdp/VaapiDeviceSelector.h"
#endif

namespace vindauga {

namespace {

// Called at startup and whenever either VA-API setting changes. Without
// VINDAUGA_WITH_RDP there is no FreeRDP and no VaapiDeviceSelector, so this is a no-op.
void applyVaapiSettings(const AppSettings& settings) {
#ifdef VINDAUGA_WITH_RDP
    applyVaapiEnvironment(settings.hardwareVideoDecoding(), settings.preferredVaapiDevice());
#else
    Q_UNUSED(settings);
#endif
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent),
      m_store(std::make_unique<ConnectionStore>()),
      m_model(std::make_unique<ConnectionListModel>(m_store.get())),
      m_settings(std::make_unique<AppSettings>()) {
    connect(m_settings.get(), &AppSettings::useDisplayScaleFactorChanged, this,
            &AppController::useDisplayScaleFactorChanged);
    connect(m_settings.get(), &AppSettings::frameBufferMsChanged, this,
            &AppController::frameBufferMsChanged);
    // Whenever either setting changes, re-apply both: applyVaapiEnvironment takes both
    // values, and re-applying unchanged environment variables is harmless.
    connect(m_settings.get(), &AppSettings::hardwareVideoDecodingChanged, this, [this]() {
        applyVaapiSettings(*m_settings);
        emit hardwareVideoDecodingChanged();
    });
    connect(m_settings.get(), &AppSettings::preferredVaapiDeviceChanged, this, [this]() {
        applyVaapiSettings(*m_settings);
        emit preferredVaapiDeviceChanged();
    });
    // Apply at startup as well, so the stored choice (or the auto heuristic when nothing
    // is stored) is in effect for the first connection, not only after a settings change.
    applyVaapiSettings(*m_settings);
}

QString AppController::version() const {
#ifdef VINDAUGA_VERSION
    return QStringLiteral(VINDAUGA_VERSION);
#else
    return QStringLiteral("unknown");
#endif
}

ConnectionListModel* AppController::connectionsModel() const {
    return m_model.get();
}

bool AppController::useDisplayScaleFactor() const {
    return m_settings->useDisplayScaleFactor();
}

void AppController::setUseDisplayScaleFactor(bool value) {
    m_settings->setUseDisplayScaleFactor(value);
}

bool AppController::hardwareVideoDecoding() const {
    return m_settings->hardwareVideoDecoding();
}

void AppController::setHardwareVideoDecoding(bool value) {
    m_settings->setHardwareVideoDecoding(value);
}

QString AppController::preferredVaapiDevice() const {
    return m_settings->preferredVaapiDevice();
}

void AppController::setPreferredVaapiDevice(QString value) {
    m_settings->setPreferredVaapiDevice(std::move(value));
}

QVariantList AppController::availableGpuDevices() const {
    QVariantList result;
#ifdef VINDAUGA_WITH_RDP
    for (const VaapiDeviceInfo& device : listVaapiDevices()) {
        QVariantMap entry;
        entry[QStringLiteral("label")] = device.label;
        entry[QStringLiteral("path")] = device.path;
        result.append(entry);
    }
#endif
    return result;
}

int AppController::frameBufferMs() const {
    return m_settings->frameBufferMs();
}

void AppController::setFrameBufferMs(int value) {
    m_settings->setFrameBufferMs(value);
}

QString AppController::importConnection(const QUrl& fileUrl) {
    return m_store->importFromFile(fileUrl.toLocalFile());
}

void AppController::removeConnection(const QString& id) {
    m_store->remove(id);
}

void AppController::openConnection(const QString& id) {
    const SavedConnection conn = m_store->connectionById(id);
    if (conn.id.isEmpty())
        return;
    m_store->touchLastUsed(id);
    emit openSessionRequested(conn.rdpText, conn.id);
}

} // namespace vindauga
