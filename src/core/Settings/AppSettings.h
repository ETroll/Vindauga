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

    // Seconds between the synthetic mouse movements FreeRDP sends while the user is idle
    // (FreeRDP_FakeMouseMotionInterval, "/prevent-session-lock" in xfreerdp), so neither
    // the AVD gateway's idle timeout nor the Windows lock screen kicks in during a long
    // read or meeting. 0 (default) = off, since an organisation's lock-screen policy is
    // theirs to set; clamped to [0, 3600].
    int preventSessionLockSeconds() const;
    void setPreventSessionLockSeconds(int value);

    // Feed discovery endpoint. Empty (default) = FeedClient::defaultDiscoveryUrl(), the
    // public Azure cloud; sovereign clouds (Azure Government, China) use another host.
    QString feedDiscoveryUrl() const;
    void setFeedDiscoveryUrl(QString value);

    // User-Agent for the Entra sign-in web view and token requests. Empty (default) =
    // the embedded browser's own string (Chromium on Linux). Some Conditional Access
    // policies only admit particular platforms or browsers, which they recognise from
    // this header; the value is applied to both the web view and AuthManager so a
    // sign-in is evaluated consistently.
    QString signInUserAgent() const;
    void setSignInUserAgent(QString value);

signals:
    void useDisplayScaleFactorChanged();
    void hardwareVideoDecodingChanged();
    void preferredVaapiDeviceChanged();
    void frameBufferMsChanged();
    void preventSessionLockSecondsChanged();
    void feedDiscoveryUrlChanged();
    void signInUserAgentChanged();

private:
    QString m_path;
};

} // namespace vindauga
