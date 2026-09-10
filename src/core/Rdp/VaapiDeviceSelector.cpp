#include "VaapiDeviceSelector.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>

#include "../Logging.h"

namespace vindauga {

namespace {

// Reads DRIVER=<name> from /sys/class/drm/<renderNodeName>/device/uevent, the same source
// `lspci -nnk`/`udevadm` use. Empty string if the file is missing or has no DRIVER line;
// the candidate is then listed without a driver name.
QString readDrmDriverName(const QString& renderNodeName) {
    QFile uevent(QStringLiteral("/sys/class/drm/%1/device/uevent").arg(renderNodeName));
    if (!uevent.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream stream(&uevent);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith(QStringLiteral("DRIVER=")))
            return line.mid(7).trimmed();
    }
    return {};
}

// Human-readable label for the settings dropdown. Known drivers get a vendor name;
// unknown ones show as "GPU (<driver>)" rather than a wrong guess.
QString driverToLabel(const QString& driver) {
    if (driver == QStringLiteral("i915"))
        return QStringLiteral("Intel (i915)");
    if (driver == QStringLiteral("nvidia"))
        return QStringLiteral("NVIDIA (nvidia)");
    if (driver == QStringLiteral("amdgpu"))
        return QStringLiteral("AMD (amdgpu)");
    if (driver == QStringLiteral("radeon"))
        return QStringLiteral("AMD (radeon)");
    if (driver.isEmpty())
        return QStringLiteral("Unknown GPU");
    return QStringLiteral("GPU (%1)").arg(driver);
}

} // namespace

QList<VaapiDeviceInfo> listVaapiDevices() {
    QList<VaapiDeviceInfo> result;
    QDir driDir(QStringLiteral("/dev/dri"));
    // Only renderD* nodes (render-only, not the display/KMS card* nodes): the same class of
    // device node FreeRDP's own VAAPI code opens (default "/dev/dri/renderD128" in
    // h264_ffmpeg.c).
    const QStringList renderNodes =
        driDir.entryList({QStringLiteral("renderD*")}, QDir::System, QDir::Name);
    for (const QString& name : renderNodes) {
        const QString driver = readDrmDriverName(name);
        result.append(VaapiDeviceInfo{driDir.filePath(name), driverToLabel(driver), driver});
    }
    return result;
}

QString pickPreferredVaapiDevice(const QList<VaapiDeviceInfo>& devices) {
    if (devices.isEmpty())
        return {};
    // Prefer the first device that both has a readable driver name and is not NVIDIA. An
    // unknown/unreadable driver is not the same as "confirmed non-NVIDIA": treating it that
    // way could skip an identifiable NVIDIA device in favour of one we know nothing about.
    for (const VaapiDeviceInfo& device : devices) {
        if (!device.driver.isEmpty() && device.driver != QStringLiteral("nvidia"))
            return device.path;
    }
    // No confirmed non-NVIDIA candidate. Explicitly choosing the first device matches
    // FreeRDP's own default in practice, but gives consistent behaviour and a log line.
    return devices.first().path;
}

void applyVaapiEnvironment(bool hardwareDecodingEnabled, const QString& explicitDevicePath) {
    if (!hardwareDecodingEnabled) {
        qCInfo(lcRdp) << "Hardware video decoding disabled in settings — "
                         "FREERDP_HWCTX_TYPE=none (software H264 decoding)";
        qputenv("FREERDP_HWCTX_TYPE", "none");
        qunsetenv("FREERDP_VAAPI_DEVICE");
        return;
    }
    qunsetenv("FREERDP_HWCTX_TYPE");

    // Validate a previously saved user choice against the devices that exist now (an eGPU
    // may have been removed); otherwise a stale path would be passed blindly to FreeRDP.
    // listVaapiDevices() runs once and is reused for the auto-fallback below.
    const QList<VaapiDeviceInfo> devices = listVaapiDevices();
    QString devicePath = explicitDevicePath;
    if (!devicePath.isEmpty()) {
        const bool stillPresent =
            std::any_of(devices.cbegin(), devices.cend(),
                        [&devicePath](const VaapiDeviceInfo& d) { return d.path == devicePath; });
        if (!stillPresent) {
            qCWarning(lcRdp) << "Saved VAAPI device" << devicePath
                              << "no longer exists on this machine — falling back to "
                                 "automatic selection";
            devicePath.clear();
        }
    }
    if (devicePath.isEmpty())
        devicePath = pickPreferredVaapiDevice(devices);

    if (devicePath.isEmpty()) {
        // No user choice and no candidate found (e.g. no /dev/dri): let FreeRDP's own
        // default (renderD128) apply silently.
        qunsetenv("FREERDP_VAAPI_DEVICE");
        return;
    }
    qCInfo(lcRdp) << "VAAPI device for hardware video decoding:" << devicePath
                  << (explicitDevicePath.isEmpty() ? "(auto-selected)" : "(user choice)");
    qputenv("FREERDP_VAAPI_DEVICE", devicePath.toUtf8());
}

} // namespace vindauga
