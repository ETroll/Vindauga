#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QRectF>
#include <QSGRenderNode>
#include <QSize>
#include <memory>
#include <utility>

#include <rhi/qrhi.h>

class QQuickWindow;

namespace vindauga {

// Custom QSGRenderNode that owns one persistent QRhiTexture in the remote desktop's
// pixel size and uploads only the changed rectangles into it
// (QRhiResourceUpdateBatch::uploadTexture with QRhiTextureSubresourceUploadDescription);
// the texture is never rebuilt per update. The pipeline/uniform-buffer setup follows
// Qt's "Scene Graph - Custom QSGRenderNode" example, extended with texture sampling
// instead of the example's flat per-vertex colour.
//
// Threading contract: QRhi and its resources (QRhiTexture/QRhiBuffer/
// QRhiGraphicsPipeline etc.) are owned by the render thread; all GPU resource creation
// and uploads happen in prepare(), which the scene graph calls on the render thread.
// setContent() is called from RdpItem::updatePaintNode() on the GUI thread during the
// scene graph's sync phase, when the render thread is blocked, so it can write
// m_itemRect/m_desktopSize/m_pendingPatches without locking; prepare() only reads them
// after sync has finished.
class RdpRenderNode : public QSGRenderNode {
public:
    explicit RdpRenderNode(QQuickWindow* window);
    ~RdpRenderNode() override;

    // Called from RdpItem::updatePaintNode() (GUI thread, sync phase); see the class
    // comment. itemRect is boundingRect() in the item's own local coordinates.
    // desktopSize is the remote desktop resolution; the GPU texture is rebuilt (and
    // cleared to black, see prepare()) only when it actually changes. patches are this
    // burst's damage regions in remote pixel coordinates; they are appended to an
    // internal queue that prepare() drains on the render thread.
    void setContent(const QRectF& itemRect, const QSize& desktopSize,
                     const QList<std::pair<QImage, QRect>>& patches);

    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;
    RenderingFlags flags() const override;
    QSGRenderNode::StateFlags changedStates() const override;

private:
    void rebuildVertexBufferIfNeeded(QRhi* rhi, QRhiResourceUpdateBatch* updates);
    void rebuildTextureIfNeeded(QRhi* rhi, QRhiResourceUpdateBatch* updates);
    void ensurePipeline(QRhi* rhi);
    void uploadPendingPatches(QRhiResourceUpdateBatch* updates);

    QQuickWindow* m_window;

    QRectF m_itemRect;
    bool m_itemRectDirty = true;

    QSize m_desktopSize;
    QList<std::pair<QImage, QRect>> m_pendingPatches;

    std::unique_ptr<QRhiBuffer> m_vertexBuffer;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    // Size we last asked QRhi to allocate, tracked here rather than re-querying
    // m_texture->pixelSize(), which a backend could in theory round or pad. See
    // rebuildTextureIfNeeded().
    QSize m_textureAllocatedSize;
    std::unique_ptr<QRhiShaderResourceBindings> m_resourceBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QList<QRhiShaderStage> m_shaders;
};

} // namespace vindauga
