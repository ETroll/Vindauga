#pragma once
#include <Qt>
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>
#include <memory>
#include <utility>

#include "AadInteractiveAuth.h"
#include "RdstlsCredentialPrompt.h"

// One RDP session on top of libfreerdp: context/settings setup, a dedicated worker
// thread running the connection loop, bridging of framebuffer/cursor/clipboard updates
// to Qt signals, and input forwarding.
namespace vindauga {

class RdpSession : public QObject {
    Q_OBJECT
public:
    // rdpText is the contents of an .rdp file. aadToken is reserved for a pre-fetched
    // token and is currently unused; the session-host AAD token is obtained
    // interactively through the handler registered with setAadInteractiveAuth().
    RdpSession(QString rdpText, QString aadToken, QObject* parent = nullptr);
    ~RdpSession() override;

    void start(); // moves the session to its own QThread and starts freerdp_connect
    void stop();  // signals abort; safe to call from another thread

    // Registers an interactive AAD handler. Must be called before start(). Not owned by
    // RdpSession; the caller must keep it alive for the session's lifetime. Without one,
    // the GetCommonAccessToken callback falls back to FreeRDP's manual stdin URL flow.
    void setAadInteractiveAuth(AadInteractiveAuth* auth);

    // Registers an RDSTLS credential handler. Must be called before start(). Not owned
    // by RdpSession. Without one, AuthenticateEx(AUTH_RDSTLS) falls back to empty
    // credentials, which an Entra-joined gateway rejects.
    void setRdstlsCredentialPrompt(RdstlsCredentialPrompt* prompt);

    // Diagnostics for context/settings parsing; usable without start().
    bool contextValid() const;
    QString gatewayHostname() const;
    bool isArmTransport() const;

public slots:
    // Input. Callers invoke these via QMetaObject::invokeMethod(session, functor,
    // Qt::QueuedConnection); they are delivered on the worker thread by the
    // processEvents() pumping in runConnectionLoop(). No-op before connected() and after
    // disconnected(). nativeScanCode is QKeyEvent::nativeScanCode() (an X11/XKB keycode,
    // identical on Wayland since XKB keymaps are shared), translated with
    // freerdp_keyboard_get_rdp_scancode_from_x11_keycode().
    void sendKeyEvent(quint32 nativeScanCode, bool pressed, bool autoRepeat);
    void sendMouseMoveEvent(QPoint pos);
    void sendMouseButtonEvent(QPoint pos, Qt::MouseButton button, bool pressed);
    void sendWheelEvent(QPoint pos, int deltaY);
    // Asks the server for a new resolution via the Display Control ("disp") channel.
    // No-op (logged) until the channel is actually ready, i.e. until the server's
    // DisplayControlCaps has arrived; vindaugaDispCaps then re-sends the request using
    // lastRequestedResize()/lastRequestedScale(). The actual gdi_resize happens only
    // when the server answers with a DesktopResize (desktopResized signal), never
    // synchronously here.
    //
    // `scale` is the desired DPI scale as a fraction (1.0 = 100%, 1.5 = 150%; typically
    // window()->devicePixelRatio()). It is rounded to what MS-RDPEDISP allows:
    // DesktopScaleFactor is clamped to [100,500], DeviceScaleFactor to the nearest of
    // {100,140,180}. The server ignores DesktopScaleFactor entirely unless
    // DeviceScaleFactor is exactly one of those three values.
    void requestResize(QSize size, qreal scale = 1.0);

    // Queues one mouse position for delivery via sendMouseMoveEvent, coalescing: if
    // called again before the previous dispatch has run, only the pending position is
    // overwritten (last one wins). Without this, move events accumulated during a quiet
    // network period were replayed in order, producing a visibly jumpy pointer. Safe to
    // call directly from any thread (typically the GUI thread, not wrapped in
    // invokeMethod by the caller).
    void queueMouseMove(QPoint pos);

    // Signals wakeEvent so the WaitForMultipleObjects in runConnectionLoop() wakes up
    // immediately instead of waiting up to 100 ms for the next network event or timeout,
    // a real contributor to perceived input lag. Typically called right after each
    // queued input invocation; also called internally by queueMouseMove(). Safe from any
    // thread (SetEvent is thread-safe); no-op if wakeEvent could not be created.
    void requestWakeUp();

public:
    // Last size requested via requestResize(), whether it was actually sent or silently
    // dropped because the channel was not ready. Public because vindaugaDispCaps (a free
    // C callback) needs it to re-send the request once the "disp" channel becomes ready.
    // Invalid (QSize().isValid() == false) if no resize has been requested yet.
    QSize lastRequestedResize() const;
    // Last `scale` requested via requestResize(); 1.0 if none has been requested yet.
    qreal lastRequestedScale() const;

signals:
    // Emitted once per EndPaint with the full list of disjoint dirty rectangles that
    // FreeRDP's GDI tracks in hwnd->cinvalid, rather than one coarse bounding box. The
    // whole list is sent as a single batch: ninvalid can be large (e.g. text scrolling)
    // and each cross-thread queued emit has its own cost.
    void frameReady(const QList<std::pair<QImage, QRect>>& regions); // Qt::QueuedConnection to the UI
    void desktopResized(QSize size);
    void connected();
    void disconnected(const QString& reason);
    void aadPromptRequired(const QUrl& url); // fallback if the programmatic token is not enough

    // FreeRDP's RetryDialog callback (instance->RetryDialog, set in vindaugaClientNew) is
    // invoked inside freerdp_connect() when a temporary condition blocks the connection
    // and a retry may succeed, e.g. ERRCONNECT_TARGET_BOOTING while Azure boots a
    // deallocated session host. Because the retry happens inside the same
    // freerdp_connect() call, the AAD/RDSTLS steps (already completed at that point) are
    // not repeated, so there are no repeated prompts. `what` is FreeRDP's description of
    // what is being retried; `attempt`/`maxAttempts` are 1-based.
    void reconnecting(const QString& what, int attempt, int maxAttempts);

    // Pointer shape updates from the server (Pointer_New/Set/SetNull/SetDefault,
    // registered via graphics_register_pointer in vindaugaClientNew). `image` is an
    // ARGB32 QImage in remote pixel size (the same pixel space as frameReady, not
    // necessarily 1:1 with the item's logical size); `hotspot` is the click point in the
    // same raw, unscaled pixel coordinates.
    void cursorChanged(const QImage& image, const QPoint& hotspot);
    void cursorHidden();  // server asked to hide the pointer (Pointer_SetNull)
    void cursorReset();   // server asked for the system default pointer (Pointer_SetDefault)

public:
    // Bridge from libfreerdp's free C callbacks (EndPaint/DesktopResize/RetryDialog,
    // registered in RdpSession.cpp) to the signals above. Public only because those
    // callbacks are free functions without access to protected emit; not meant to be
    // called from elsewhere.
    void notifyFrameReady(const QList<std::pair<QImage, QRect>>& regions) {
        emit frameReady(regions);
    }
    void notifyDesktopResized(QSize size) { emit desktopResized(size); }
    void notifyReconnecting(const QString& what, int attempt, int maxAttempts) {
        emit reconnecting(what, attempt, maxAttempts);
    }
    // See cursorChanged/cursorHidden/cursorReset above.
    void notifyCursorChanged(const QImage& image, const QPoint& hotspot) {
        emit cursorChanged(image, hotspot);
    }
    void notifyCursorHidden() { emit cursorHidden(); }
    void notifyCursorReset() { emit cursorReset(); }

    // Bridge from the cliprdr C callbacks to QClipboard. Public for the same reason as
    // the notify* methods. Called from the worker thread; hops to the GUI thread
    // internally (QClipboard is a GUI-thread API).
    //
    // Remote -> local clipboard: non-blocking (nothing to wait for). Arms a guard
    // (ignoringNextClipboardChange) before the setText() call so the clipboard change
    // listener registered in the constructor does not re-announce our own update.
    void setLocalClipboardText(const QString& text);

    // Local -> remote clipboard: blocking GUI-thread hop. Called from
    // respondWithLocalClipboardText() on the worker thread, never synchronously from the
    // cliprdr ServerFormatDataRequest callback itself. Returns UTF-16LE with a null
    // terminator, per the CF_UNICODETEXT format.
    QByteArray localClipboardTextUtf16() const;

    // Gives the free C callbacks access to the registered AAD handler. Public for the
    // same reason as the notify* methods.
    AadInteractiveAuth* aadInteractiveAuth() const;

    // Gives the AuthenticateEx callback access to the registered RDSTLS credential
    // handler. Public for the same reason as the notify* methods.
    RdstlsCredentialPrompt* rdstlsCredentialPrompt() const;

    // The cliprdr Server* callbacks run on FreeRDP's dedicated channel thread
    // (channel_client_thread_proc), not on this session's worker thread. Calling an
    // outgoing Client* send function synchronously from inside a Server* callback
    // (except MonitorReady -> ClientFormatList, which FreeRDP's own X11 client also does)
    // takes down the whole connection ("cliprdr_process_format_list failed with error
    // 1359", ERROR_INTERNAL_ERROR). The slots below therefore leave the channel thread's
    // call stack via QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) and run
    // on the worker thread from the same processEvents() pumping that delivers the input
    // slots. announceLocalClipboardText uses the same pattern when triggered from
    // QClipboard::dataChanged on the GUI thread, but is also called directly from
    // vindaugaCliprdrMonitorReady. Public slots (not private) because the free C
    // callbacks need access, and invokeMethod-by-name requires meta-object registration.
public slots:
    // Announces the CF_UNICODETEXT format to the server (the data itself is only sent
    // if/when the server asks, see vindaugaCliprdrServerFormatDataRequest). No-op if the
    // cliprdr channel has not been negotiated.
    void announceLocalClipboardText();
    // Asks the server for the actual CF_UNICODETEXT data (queued from
    // vindaugaCliprdrServerFormatList, never called synchronously from it; see above).
    void requestRemoteClipboardText();
    // Answers the server with the local clipboard text (queued from
    // vindaugaCliprdrServerFormatDataRequest, never called synchronously from it).
    void respondWithLocalClipboardText();

private slots:
    // Connected to QThread::started (runs on the worker thread after moveToThread in
    // start()). Blocks for the session's whole lifetime and pumps
    // QCoreApplication::processEvents() itself so queued input slots are delivered.
    void runConnectionLoop();

    // Delivers the most recently queued mouse position to FreeRDP; see queueMouseMove().
    // Runs on the worker thread.
    void dispatchPendingMouseMove();

    // Recovery action after a decode failure: re-sends the last requested size/scale via
    // requestResize() so the server performs a full RDPGFX resync (ResetGraphics +
    // CreateSurface + decoder re-init), the same chain a real window resize triggers.
    // Queued (Qt::QueuedConnection) from onDecodeFailureDetected(), never called
    // synchronously from it.
    void performResyncNudge();

private:
    // Registered via setDecodeFailureCallback (Logging.h) while runConnectionLoop() runs
    // and removed when it ends. Not a Qt slot: invoked directly as a std::function from
    // whatever thread produced the log line. Does the minimum (debounce check plus a
    // queued performResyncNudge()) and never calls into FreeRDP synchronously.
    void onDecodeFailureDetected();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vindauga
