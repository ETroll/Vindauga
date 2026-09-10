#pragma once
#include <QImage>
#include <QList>
#include <QQuickItem>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <utility>

#include "AadWebAuthenticator.h"
#include "RdstlsCredentialDialog.h"
#include "Rdp/RdpSession.h"

class QSGNode;

namespace vindauga {

// Qt Quick item that displays and drives one RDP session. Owns one RdpSession (created
// in connectToSession) plus the two concrete handlers it needs (AadWebAuthenticator and
// RdstlsCredentialDialog). Mouse/keyboard/wheel/geometry events are forwarded to
// RdpSession via QMetaObject::invokeMethod(..., Qt::QueuedConnection), since RdpSession
// lives on its own QThread.
//
// Rendering: a raw QQuickItem with updatePaintNode() rather than QQuickPaintedItem.
// There is no CPU-side compositing at all: every incoming damage region (already a
// precise (QImage, QRect) list from FreeRDP's GDI, see RdpSession) is passed straight to
// RdpRenderNode, which uploads it to its exact place in a persistent GPU texture.
// m_desktopSize (from onDesktopResized) is the authoritative source of the desktop's
// dimensions for mapToRemote() and cursor scaling.
class RdpItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    // Set from QML (bound to AppController::useDisplayScaleFactor). MEMBER keeps the
    // property and the field in sync without a setter pair. Default false keeps the
    // always-100% behaviour if RdpItem is instantiated without setting it.
    Q_PROPERTY(bool useDisplayScaleFactor MEMBER m_useDisplayScaleFactor)
    // Set from QML (bound to AppController::frameBufferMs). Number of milliseconds
    // onFrameReady() waits after the first frame of a burst before drawing (see
    // m_frameBufferTimer); collects more frames than plain coalescing would, at the cost
    // of that much extra visual latency. Default 0 (off: draw as soon as possible).
    Q_PROPERTY(int frameBufferMs MEMBER m_frameBufferMs)
public:
    explicit RdpItem(QQuickItem* parent = nullptr);
    ~RdpItem() override;

    // rdpText is the contents of the .rdp file. connectionId is SavedConnection::id
    // (optional; empty = no credential caching), the shared key for keychain lookups in
    // AadWebAuthenticator/RdstlsCredentialDialog. Creates and starts an RdpSession. May
    // be called again for a retry as long as the previous session has already
    // disconnected; the old session is cleaned up first. No-op (with a warning) if
    // called while a session is still connected.
    Q_INVOKABLE void connectToSession(const QString& rdpText, const QString& connectionId = QString());

    // Called by the scene graph on the GUI thread during the sync phase (the render
    // thread is blocked until sync is done), so m_pendingRenderPatches/m_desktopSize can
    // be handed to RdpRenderNode::setContent() without locking; see the threading
    // contract in RdpRenderNode.h.
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

signals:
    void sessionConnected();
    void sessionDisconnected(const QString& reason);
    // Forwards RdpSession::reconnecting: FreeRDP's internal retry mechanism, used e.g.
    // when a stopped AVD session host VM is booted automatically ("Starting your VM. It
    // may take up to 5 minutes.").
    void sessionReconnecting(const QString& what, int attempt, int maxAttempts);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private slots:
    // RdpSession::frameReady delivers a whole batch of (QImage, QRect) pairs per EndPaint
    // call.
    void onFrameReady(const QList<std::pair<QImage, QRect>>& regions);
    void onDesktopResized(QSize size);
    // See RdpSession::cursorChanged/cursorHidden/cursorReset.
    void onCursorChanged(const QImage& image, const QPoint& hotspot);
    void onCursorHidden();
    void onCursorReset();

    // See m_pendingFrames below. Queued (via onFrameReady) to run only once per burst of
    // pending frames, instead of every frame doing its own update() round.
    void drainPendingFrames();

private:
    // Scales a point in the item's local coordinates (boundingRect()) to the pixel
    // position in the remote desktop (m_desktopSize). The two are not necessarily 1:1,
    // since RdpRenderNode draws the texture scaled to the item's boundingRect().
    QPoint mapToRemote(const QPointF& itemPos) const;

    // Asks the server for a resolution matching physical pixels, not logical ones:
    // logicalSize (Qt Quick's DPI-independent units) is multiplied by
    // window()->devicePixelRatio() before being sent to RdpSession::requestResize.
    // Otherwise a HiDPI screen would get a remote session at half the real pixel density,
    // visibly blurry. Used both on geometry changes and right after connected(), so the
    // session matches the window size at connect time.
    void requestRemoteResize(const QSizeF& logicalSize);

    // Debounce for remote resize requests. A window drag or WM animation delivers many
    // intermediate geometries, and each resize request is a full round trip to the server
    // (new resolution negotiated on the display channel, video decoder reinitialised), so
    // the session never stabilises while the chain is in progress. Only send once the
    // geometry has been unchanged for the timer interval (see the constructor).
    QTimer m_resizeDebounceTimer;
    QSizeF m_pendingResizeSize;

    bool m_useDisplayScaleFactor = false;
    int m_frameBufferMs = 0;
    // Single-shot, connected once in the constructor to drainPendingFrames(). Used only
    // when m_frameBufferMs > 0; see onFrameReady().
    QTimer m_frameBufferTimer;

    std::unique_ptr<RdpSession> m_session;
    AadWebAuthenticator m_aadAuth;
    RdstlsCredentialDialog m_rdstlsPrompt;
    // Authoritative source of the desktop's dimensions (set by onDesktopResized). Empty
    // means "no active session with a known resolution yet"; updatePaintNode() returns
    // early then.
    QSize m_desktopSize;
    bool m_connected = false;
    bool m_everConnected = false; // whether cached RDSTLS credentials should be invalidated

    // Frame coalescing. RdpSession::frameReady is a cross-thread Qt::QueuedConnection,
    // so each emit would otherwise be its own GUI-thread event with its own full update()
    // round; during a burst (video, scrolling, a window opening) that queue grows faster
    // than the GUI thread can drain it, which shows as freezes followed by a catch-up.
    // onFrameReady() is therefore deliberately cheap (one QList::append) and
    // drainPendingFrames() handles the whole pending batch, queued once per burst (see
    // m_frameDrainScheduled). No pixel data is dropped: every damage region must still be
    // uploaded for correctness; this only reduces the number of event-loop rounds.
    // GUI-thread state only, no locking needed.
    //
    // This is event coalescing, not bounded backpressure: under sustained overload (the
    // worker decodes faster than the GUI thread drains) m_pendingFrames still grows
    // without bound. Accepted deliberately.
    QList<std::pair<QImage, QRect>> m_pendingFrames;
    bool m_frameDrainScheduled = false;

    // Second-stage queue: drainPendingFrames() moves m_pendingFrames here, and
    // updatePaintNode() hands the whole list to RdpRenderNode::setContent(). Kept
    // separate from m_pendingFrames because it can accumulate over several bursts before
    // the scene graph's sync phase actually runs (sync is not synchronous with update()).
    QList<std::pair<QImage, QRect>> m_pendingRenderPatches;
};

} // namespace vindauga
