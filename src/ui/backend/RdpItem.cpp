#include "RdpItem.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>

#include "Logging.h"
#include "RdpRenderNode.h"

namespace vindauga {

RdpItem::RdpItem(QQuickItem* parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFlag(QQuickItem::ItemIsFocusScope, true);
    // QQuickPaintedItem sets this internally; a raw QQuickItem does not, and without it
    // updatePaintNode() is never called and the item stays invisible.
    setFlag(QQuickItem::ItemHasContents, true);

    // See m_resizeDebounceTimer in RdpItem.h. 200 ms is long enough to collapse a whole
    // window-drag/WM-animation sequence into one resize request, short enough to still
    // feel responsive for a single deliberate resize.
    m_resizeDebounceTimer.setSingleShot(true);
    m_resizeDebounceTimer.setInterval(200);
    connect(&m_resizeDebounceTimer, &QTimer::timeout, this,
            [this]() { requestRemoteResize(m_pendingResizeSize); });

    // Used only when m_frameBufferMs > 0; see onFrameReady(). The interval is not set
    // here but from onFrameReady() before each start(), since m_frameBufferMs is a live
    // Q_PROPERTY that can change during a session.
    m_frameBufferTimer.setSingleShot(true);
    connect(&m_frameBufferTimer, &QTimer::timeout, this, &RdpItem::drainPendingFrames);
}

RdpItem::~RdpItem() = default;

void RdpItem::connectToSession(const QString& rdpText, const QString& connectionId) {
    if (m_session) {
        // Allow reusing the same RdpItem for a retry, but only once the previous session
        // has finished. The old session is cleaned up (stop() + thread join in
        // ~RdpSession()) before a new one is created; RdpSession does not support
        // reconnecting a live session.
        if (m_connected) {
            qCWarning(lcRdp) << "connectToSession() called while a session is already "
                                 "connected — ignoring";
            return;
        }
        m_session.reset();
    }
    // Do not let the previous session's last frame linger in RdpRenderNode's GPU texture
    // (e.g. as a frozen image behind the error card). With m_desktopSize empty,
    // updatePaintNode() returns nullptr and Qt deletes the old node; the item stays blank
    // until the new session's first DesktopResize arrives.
    m_desktopSize = QSize();
    m_pendingFrames.clear();
    m_pendingRenderPatches.clear();
    update();

    m_session = std::make_unique<RdpSession>(rdpText, QString());
    if (!m_session->contextValid()) {
        qCWarning(lcRdp) << "Invalid .rdp content — context was not created";
        m_session.reset();
        return;
    }
    m_session->setAadInteractiveAuth(&m_aadAuth);
    m_session->setRdstlsCredentialPrompt(&m_rdstlsPrompt);

    // Show the AAD web view and the RDSTLS dialog as overlays inside our window rather
    // than as separate top-level windows. Falls back to a separate window if window() is
    // null (item not in a scene yet).
    m_aadAuth.setHostWindow(window());
    m_rdstlsPrompt.setHostWindow(window());

    // Shared credential key for both handlers, so the RDSTLS dialog can be skipped and
    // the AAD web view autofilled with the same stored credentials. See CredentialStore.
    m_aadAuth.setConnectionKey(connectionId);
    m_rdstlsPrompt.setConnectionKey(connectionId);

    // The RDSTLS stage (where credentials are captured and stored) comes after the AAD
    // stage in the freerdp_connect() sequence, so for a brand-new connection the keychain
    // would still be empty when the AAD web view loads and autofill would not work on
    // the first attempt. Ask proactively here, before session->start():
    // requestCredentials() checks the keychain first, so this is either an instant cache
    // hit or shows the dialog once and stores the result. Either way credentials are
    // available before the AAD web view loads.
    if (!connectionId.isEmpty()) {
        const QStringList creds =
            m_rdstlsPrompt.requestCredentials(QString(), QStringLiteral("AzureAD"));
        if (creds.size() != 3) {
            qCDebug(lcRdp) << "User cancelled credential entry before the connection started";
            m_session.reset();
            emit sessionDisconnected(tr("Cancelled"));
            return;
        }
    }

    m_everConnected = false;
    connect(m_session.get(), &RdpSession::frameReady, this, &RdpItem::onFrameReady);
    connect(m_session.get(), &RdpSession::desktopResized, this, &RdpItem::onDesktopResized);
    connect(m_session.get(), &RdpSession::cursorChanged, this, &RdpItem::onCursorChanged);
    connect(m_session.get(), &RdpSession::cursorHidden, this, &RdpItem::onCursorHidden);
    connect(m_session.get(), &RdpSession::cursorReset, this, &RdpItem::onCursorReset);
    connect(m_session.get(), &RdpSession::connected, this, [this]() {
        m_connected = true;
        m_everConnected = true;
        // The AAD/RDSTLS overlay takes Quick scene focus, and nothing gives it back to
        // RdpItem when the overlay is removed. connected() is the point where that is
        // guaranteed to be over, so reclaim focus here.
        forceActiveFocus();
        if (window())
            window()->requestActivate();
        // Sync the remote resolution to our actual (HiDPI-corrected) window size right
        // away rather than waiting for a later geometryChange; otherwise the session would
        // always start in the .rdp file's baked-in default resolution (typically 1024x768).
        requestRemoteResize(boundingRect().size());
        emit sessionConnected();
    });
    connect(m_session.get(), &RdpSession::disconnected, this, [this](const QString& reason) {
        m_connected = false;
        // Do not let an RDP pointer shape from the finished session linger over the UI
        // shown afterwards.
        unsetCursor();
        // The session never connected and cached RDSTLS credentials were used without
        // showing the dialog: most likely they are stale or wrong (e.g. a password change).
        // Invalidate them so the next attempt asks again instead of failing identically.
        if (!m_everConnected && m_rdstlsPrompt.usedCachedCredentialsLastAttempt()) {
            qCDebug(lcRdp) << "Cached RDSTLS credentials did not lead to a "
                              "successful session — removing them from keychain";
            m_rdstlsPrompt.clearCachedCredentials();
        }
        emit sessionDisconnected(reason);
    });
    connect(m_session.get(), &RdpSession::reconnecting, this,
            [this](const QString& what, int attempt, int maxAttempts) {
                emit sessionReconnecting(what, attempt, maxAttempts);
            });

    m_session->start();
    forceActiveFocus();
}

QSGNode* RdpItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    // Check m_desktopSize.isEmpty() unconditionally, whether or not oldNode exists.
    // Returning nullptr when no resolution is known yet (or it was just reset by
    // connectToSession()) makes Qt Quick delete oldNode itself; it must not be deleted
    // manually.
    if (m_desktopSize.isEmpty() || !window())
        return nullptr;

    auto* node = static_cast<RdpRenderNode*>(oldNode);
    if (!node)
        node = new RdpRenderNode(window());

    // Hand this burst's damage regions plus the current item size and desktop resolution
    // straight to the render node; there is no CPU-side compositing. Called
    // unconditionally: setContent() works out what actually changed, and
    // m_pendingRenderPatches is usually empty for a pure geometry update.
    node->setContent(boundingRect(), m_desktopSize, m_pendingRenderPatches);
    m_pendingRenderPatches.clear();
    return node;
}

void RdpItem::onFrameReady(const QList<std::pair<QImage, QRect>>& regions) {
    // Deliberately cheap; see m_pendingFrames in RdpItem.h. regions is a whole batch
    // from one EndPaint call (possibly several disjoint rects); append(QList) adds them
    // all.
    m_pendingFrames.append(regions);
    if (!m_frameDrainScheduled) {
        m_frameDrainScheduled = true;
        // With frame buffering enabled, wait m_frameBufferMs after the first frame of
        // the burst before draining, instead of draining as soon as the GUI thread is
        // free: a jitter buffer that deliberately collects more frames than plain
        // coalescing would, trading a little latency for smoother delivery.
        // m_frameBufferMs is read live; a change while the timer is already running
        // affects the next burst, not the current one. Like the coalescing itself this
        // is not bounded backpressure: a long buffer at a high frame rate can accumulate
        // many QImages before one heavy drain.
        if (m_frameBufferMs > 0)
            m_frameBufferTimer.start(m_frameBufferMs);
        else
            QMetaObject::invokeMethod(this, &RdpItem::drainPendingFrames, Qt::QueuedConnection);
    }
}

void RdpItem::drainPendingFrames() {
    m_frameDrainScheduled = false;
    if (m_pendingFrames.isEmpty())
        return;

    // No CPU-side compositing: every damage region is moved as-is to
    // m_pendingRenderPatches (consumed by updatePaintNode()), and RdpRenderNode uploads
    // it straight into the GPU texture. Only one update() round per burst, not one per
    // incoming frame.
    m_pendingRenderPatches.append(m_pendingFrames);
    m_pendingFrames.clear();
    update();
}

void RdpItem::onDesktopResized(QSize size) {
    // Info level so resolution/input problems can be diagnosed at the default log level.
    // The item's own (logical) size and devicePixelRatio are logged alongside: the scale
    // factor used by mapToRemote() and the cursor code is the ratio between the two, not
    // `size` alone.
    qCInfo(lcRdp) << "Remote desktop resized:" << size << "— item:" << boundingRect().size()
                  << "dpr:" << (window() ? window()->devicePixelRatio() : -1.0);
    // RdpRenderNode rebuilds and clears its GPU texture when it sees the new desktopSize
    // via setContent(), so old content from a larger resolution does not linger around
    // new, smaller damage regions. Pending patches in the previous resolution are
    // discarded here as well: FreeRDP never sends a surface command for the new
    // resolution before the DesktopResize PDU has been processed, so everything queued
    // here is stale.
    if (m_desktopSize != size) {
        m_desktopSize = size;
        m_pendingFrames.clear();
        m_pendingRenderPatches.clear();
        update();
    }
}

void RdpItem::onCursorChanged(const QImage& image, const QPoint& hotspot) {
    if (image.isNull())
        return;
    QPixmap pixmap = QPixmap::fromImage(image);
    // The image is in remote pixel size (m_desktopSize), which is not necessarily 1:1
    // with the item's logical coordinates (RdpRenderNode draws the desktop scaled to
    // boundingRect(); see mapToRemote). Apply the same scale via
    // QPixmap::setDevicePixelRatio() so Qt shows the pointer (and places its hotspot) in
    // item size rather than raw remote pixels.
    if (!m_desktopSize.isEmpty() && width() > 0) {
        const qreal dpr = static_cast<qreal>(m_desktopSize.width()) / width();
        if (dpr > 0)
            pixmap.setDevicePixelRatio(dpr);
    }
    setCursor(QCursor(pixmap, hotspot.x(), hotspot.y()));
}

void RdpItem::onCursorHidden() { setCursor(Qt::BlankCursor); }

void RdpItem::onCursorReset() { unsetCursor(); }

QPoint RdpItem::mapToRemote(const QPointF& itemPos) const {
    if (m_desktopSize.isEmpty() || width() <= 0 || height() <= 0)
        return itemPos.toPoint();
    const qreal sx = static_cast<qreal>(m_desktopSize.width()) / width();
    const qreal sy = static_cast<qreal>(m_desktopSize.height()) / height();
    return QPoint(static_cast<int>(itemPos.x() * sx), static_cast<int>(itemPos.y() * sy));
}

void RdpItem::mousePressEvent(QMouseEvent* event) {
    forceActiveFocus();
    if (!m_session) {
        event->ignore();
        return;
    }
    const QPoint pos = mapToRemote(event->position());
    const Qt::MouseButton button = event->button();
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), pos, button]() { session->sendMouseButtonEvent(pos, button, true); },
        Qt::QueuedConnection);
    // See RdpSession::requestWakeUp(): avoids a click waiting up to 100 ms for
    // runConnectionLoop()'s next wake-up.
    m_session->requestWakeUp();
    event->accept();
}

void RdpItem::mouseMoveEvent(QMouseEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    // queueMouseMove() coalesces (last position wins) and signals the wake event itself;
    // queueing one invokeMethod per mouse move gave a jerky pointer after an idle period.
    m_session->queueMouseMove(mapToRemote(event->position()));
    event->accept();
}

void RdpItem::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    const QPoint pos = mapToRemote(event->position());
    const Qt::MouseButton button = event->button();
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), pos, button]() { session->sendMouseButtonEvent(pos, button, false); },
        Qt::QueuedConnection);
    m_session->requestWakeUp();
    event->accept();
}

void RdpItem::hoverMoveEvent(QHoverEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    m_session->queueMouseMove(mapToRemote(event->position()));
    event->accept();
}

void RdpItem::wheelEvent(QWheelEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    const QPoint pos = mapToRemote(event->position());
    const int deltaY = event->angleDelta().y();
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), pos, deltaY]() { session->sendWheelEvent(pos, deltaY); },
        Qt::QueuedConnection);
    m_session->requestWakeUp();
    event->accept();
}

void RdpItem::keyPressEvent(QKeyEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    const quint32 scanCode = static_cast<quint32>(event->nativeScanCode());
    const bool autoRepeat = event->isAutoRepeat();
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), scanCode, autoRepeat]() {
            session->sendKeyEvent(scanCode, true, autoRepeat);
        },
        Qt::QueuedConnection);
    m_session->requestWakeUp();
    event->accept();
}

void RdpItem::keyReleaseEvent(QKeyEvent* event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    const quint32 scanCode = static_cast<quint32>(event->nativeScanCode());
    const bool autoRepeat = event->isAutoRepeat();
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), scanCode, autoRepeat]() {
            session->sendKeyEvent(scanCode, false, autoRepeat);
        },
        Qt::QueuedConnection);
    m_session->requestWakeUp();
    event->accept();
}

void RdpItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (m_session && m_connected && newGeometry.size() != oldGeometry.size()) {
        // Do not send a resize request for every intermediate geometry during a window
        // drag or WM animation; only once the geometry has been unchanged for the timer
        // interval. start() on a running single-shot timer restarts it, so a rapid
        // sequence of calls collapses to one request sent interval() ms after the
        // geometry stopped changing.
        m_pendingResizeSize = newGeometry.size();
        m_resizeDebounceTimer.start();
    }
}

void RdpItem::requestRemoteResize(const QSizeF& logicalSize) {
    if (!m_session)
        return;
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    const QSize size = (logicalSize * dpr).toSize();
    // Scaling (DesktopScaleFactor/DeviceScaleFactor, see RdpSession::requestResize) is
    // sent as something other than 100% only if the user enabled it in settings
    // (m_useDisplayScaleFactor); by default only the pixel count is scaled, never the DPI
    // metadata.
    const qreal scale = m_useDisplayScaleFactor ? dpr : 1.0;
    // Info level so the whole resize chain (this line, RdpSession::requestResize,
    // onDesktopResized) is visible at the default log level.
    qCInfo(lcRdp) << "requestRemoteResize: logical" << logicalSize << "× dpr" << dpr << "=" << size
                  << "scale=" << scale << "(useDisplayScaleFactor=" << m_useDisplayScaleFactor << ")";
    QMetaObject::invokeMethod(
        m_session.get(),
        [session = m_session.get(), size, scale]() { session->requestResize(size, scale); },
        Qt::QueuedConnection);
}

} // namespace vindauga
