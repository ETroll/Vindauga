#pragma once
#include <QObject>
#include <QString>

// Application-wide settings (as opposed to ConnectionStore, which holds saved
// connections). Pure core/, no Qt Quick/GUI dependency.
namespace vindauga {

class AppSettings : public QObject {
    Q_OBJECT
public:
    // storagePath: full path to the .ini file. Empty (default) means
    // $XDG_CONFIG_HOME/vindauga/settings.ini. Exposed so tests can use a temporary file.
    explicit AppSettings(QString storagePath = QString(), QObject* parent = nullptr);

    // Asks the RDP session's Display Control channel to scale the desktop to the screen's
    // devicePixelRatio instead of always 100% (see RdpSession::requestResize). Default
    // false so existing scaling behaviour only changes when the user opts in.
    bool useDisplayScaleFactor() const;
    void setUseDisplayScaleFactor(bool value);

    // Whether to use VAAPI hardware video decoding. It can fail intermittently on
    // hybrid-GPU machines (screen regions stop updating during video), so this is a
    // user-facing setting rather than an environment variable. Default true.
    bool hardwareVideoDecoding() const;
    void setHardwareVideoDecoding(bool value);

    // Empty (default) = automatic: pick the best-effort device via
    // pickPreferredVaapiDevice (prefers non-NVIDIA). Non-empty = a device path chosen by
    // the user (e.g. "/dev/dri/renderD129"), which always overrides the heuristic. Only
    // meaningful when hardwareVideoDecoding() is true.
    QString preferredVaapiDevice() const;
    void setPreferredVaapiDevice(QString value);

    // Milliseconds RdpItem waits after the first frame of a burst before compositing and
    // painting (see RdpItem::onFrameReady), so more frames are coalesced into one paint at
    // the cost of that much added latency. Default 0 (off: paint as soon as possible).
    int frameBufferMs() const;
    void setFrameBufferMs(int value);

signals:
    void useDisplayScaleFactorChanged();
    void hardwareVideoDecodingChanged();
    void preferredVaapiDeviceChanged();
    void frameBufferMsChanged();

private:
    QString m_path;
};

} // namespace vindauga
