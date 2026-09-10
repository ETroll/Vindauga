#include "AppSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace vindauga {

namespace {

QString resolveDefaultPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                        QStringLiteral("/vindauga");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/settings.ini");
}

constexpr auto kUseDisplayScaleFactorKey = "useDisplayScaleFactor";
constexpr auto kHardwareVideoDecodingKey = "hardwareVideoDecoding";
constexpr auto kPreferredVaapiDeviceKey = "preferredVaapiDevice";
constexpr auto kFrameBufferMsKey = "frameBufferMs";

} // namespace

AppSettings::AppSettings(QString storagePath, QObject* parent)
    : QObject(parent), m_path(storagePath.isEmpty() ? resolveDefaultPath() : std::move(storagePath)) {}

bool AppSettings::useDisplayScaleFactor() const {
    QSettings settings(m_path, QSettings::IniFormat);
    return settings.value(kUseDisplayScaleFactorKey, false).toBool();
}

void AppSettings::setUseDisplayScaleFactor(bool value) {
    if (value == useDisplayScaleFactor())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(kUseDisplayScaleFactorKey, value);
    emit useDisplayScaleFactorChanged();
}

bool AppSettings::hardwareVideoDecoding() const {
    QSettings settings(m_path, QSettings::IniFormat);
    return settings.value(kHardwareVideoDecodingKey, true).toBool();
}

void AppSettings::setHardwareVideoDecoding(bool value) {
    if (value == hardwareVideoDecoding())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(kHardwareVideoDecodingKey, value);
    emit hardwareVideoDecodingChanged();
}

QString AppSettings::preferredVaapiDevice() const {
    QSettings settings(m_path, QSettings::IniFormat);
    return settings.value(kPreferredVaapiDeviceKey, QString()).toString();
}

void AppSettings::setPreferredVaapiDevice(QString value) {
    if (value == preferredVaapiDevice())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(kPreferredVaapiDeviceKey, value);
    emit preferredVaapiDeviceChanged();
}

int AppSettings::frameBufferMs() const {
    QSettings settings(m_path, QSettings::IniFormat);
    // Clamp on read as well as on write: a hand-edited or older .ini file may hold an
    // absurd value, which would otherwise go straight to RdpItem's QTimer::start() and
    // hold frames back indefinitely.
    return std::clamp(settings.value(kFrameBufferMsKey, 0).toInt(), 0, 1000);
}

void AppSettings::setFrameBufferMs(int value) {
    // Clamp here too, not only in the QML SpinBox; the .ini file can be hand-edited.
    value = std::clamp(value, 0, 1000);
    if (value == frameBufferMs())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(kFrameBufferMsKey, value);
    emit frameBufferMsChanged();
}

} // namespace vindauga
