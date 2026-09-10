#include "RdpRenderNode.h"

#include <QFile>
#include <QMatrix4x4>
#include <QQuickWindow>

#include "Logging.h"

namespace vindauga {

namespace {

QShader loadShader(const QString& resourcePath) {
    QFile file(resourcePath);
    if (!file.open(QFile::ReadOnly))
        qFatal("RdpRenderNode: could not load shader %s", qPrintable(resourcePath));
    return QShader::fromSerialized(file.readAll());
}

} // namespace

RdpRenderNode::RdpRenderNode(QQuickWindow* window) : m_window(window) {
    m_shaders.append(QRhiShaderStage(QRhiShaderStage::Vertex,
                                      loadShader(QStringLiteral(":/vindauga/shaders/rdprender.vert.qsb"))));
    m_shaders.append(QRhiShaderStage(QRhiShaderStage::Fragment,
                                      loadShader(QStringLiteral(":/vindauga/shaders/rdprender.frag.qsb"))));
}

RdpRenderNode::~RdpRenderNode() = default;

void RdpRenderNode::setContent(const QRectF& itemRect, const QSize& desktopSize,
                                const QList<std::pair<QImage, QRect>>& patches) {
    if (m_itemRect != itemRect) {
        m_itemRect = itemRect;
        m_itemRectDirty = true;
        markDirty(QSGNode::DirtyGeometry);
    }
    if (m_desktopSize != desktopSize) {
        m_desktopSize = desktopSize;
        markDirty(QSGNode::DirtyMaterial);
    }
    if (!patches.isEmpty()) {
        m_pendingPatches.append(patches);
        markDirty(QSGNode::DirtyMaterial);
    }
}

void RdpRenderNode::releaseResources() {
    m_vertexBuffer.reset();
    m_uniformBuffer.reset();
    m_sampler.reset();
    m_texture.reset();
    m_pipeline.reset();
    m_resourceBindings.reset();
}

RdpRenderNode::RenderingFlags RdpRenderNode::flags() const {
    // No DepthAwareRendering (and no depth test in ensurePipeline()), unlike Qt's
    // customrendernode example: a depth test here can collide with Qt Quick's own opaque
    // depth pre-pass for the opaque background item drawn before this node, and any
    // mismatch in Z values would depth-reject the whole quad. Paint order is sufficient
    // for a single full-surface 2D quad. NoExternalRendering still holds: pure QRhi
    // content, no external 3D API.
    return QSGRenderNode::NoExternalRendering;
}

QSGRenderNode::StateFlags RdpRenderNode::changedStates() const {
    return QSGRenderNode::StateFlag::ViewportState | QSGRenderNode::StateFlag::CullState;
}

void RdpRenderNode::rebuildVertexBufferIfNeeded(QRhi* rhi, QRhiResourceUpdateBatch* updates) {
    if (!m_itemRectDirty && m_vertexBuffer)
        return;
    m_itemRectDirty = false;

    const float w = static_cast<float>(m_itemRect.width());
    const float h = static_cast<float>(m_itemRect.height());
    // Triangle-strip quad, interleaved position (item-local pixels) + uv (0..1). uv (0,0)
    // is top-left, matching QImage row order directly; QRhi's uploadTexture() normalises
    // orientation per backend, so no manual Y flip is needed.
    const float verts[] = {
        0.0f, 0.0f, 0.0f, 0.0f, //
        w,    0.0f, 1.0f, 0.0f, //
        0.0f, h,    0.0f, 1.0f, //
        w,    h,    1.0f, 1.0f, //
    };

    m_vertexBuffer.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(verts)));
    m_vertexBuffer->create();
    updates->uploadStaticBuffer(m_vertexBuffer.get(), verts);
}

void RdpRenderNode::rebuildTextureIfNeeded(QRhi* rhi, QRhiResourceUpdateBatch* updates) {
    if (m_desktopSize.isEmpty())
        return;
    // Compare against m_textureAllocatedSize (our own bookkeeping), not
    // m_texture->pixelSize(); see the field comment in RdpRenderNode.h.
    if (m_texture && m_textureAllocatedSize == m_desktopSize)
        return;

    // Keep the same QRhiTexture object across a resize and only swap the underlying GPU
    // allocation via setPixelSize()+create(), so m_resourceBindings (set up in
    // ensurePipeline(), referencing m_texture.get()) stays valid.
    if (!m_texture)
        m_texture.reset(rhi->newTexture(QRhiTexture::BGRA8, m_desktopSize));
    else
        m_texture->setPixelSize(m_desktopSize);
    if (!m_texture->create()) {
        qCWarning(lcRdp) << "RdpRenderNode: could not (re)create GPU texture of size"
                          << m_desktopSize;
        return;
    }
    m_textureAllocatedSize = m_desktopSize;
    qCDebug(lcRdp) << "RdpRenderNode: (re)created GPU texture of size" << m_desktopSize;

    // A new or resized GPU allocation has undefined contents; clear to black so areas
    // the server does not immediately repaint do not show garbage.
    QImage black(m_desktopSize, QImage::Format_ARGB32);
    black.fill(Qt::black);
    const QRhiTextureSubresourceUploadDescription blackSub(black);
    updates->uploadTexture(m_texture.get(), QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, blackSub)}));

    // Pending patches from before the resize refer to a region that is now invalid
    // (wrong size); discard them. The server sends a full update after a desktop resize.
    m_pendingPatches.clear();
}

void RdpRenderNode::ensurePipeline(QRhi* rhi) {
    if (m_pipeline)
        return;

    m_uniformBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 68));
    m_uniformBuffer->create();

    m_sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();

    m_resourceBindings.reset(rhi->newShaderResourceBindings());
    m_resourceBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_uniformBuffer.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                   m_texture.get(), m_sampler.get()),
    });
    m_resourceBindings->create();

    m_pipeline.reset(rhi->newGraphicsPipeline());
    // No back-face culling, unlike Qt's customrendernode example with its winding
    // correction: we always draw a single camera-facing 2D quad, so culling gains nothing
    // and a wrong winding assumption (item-local Y-down coordinates vs. the NDC flip
    // after projectionMatrix()*matrix()) would make the whole quad invisible.
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    m_pipeline->setTargetBlends({blend});
    m_pipeline->setShaderResourceBindings(m_resourceBindings.get());
    m_pipeline->setShaderStages(m_shaders.cbegin(), m_shaders.cend());
    // No depth test; see flags().
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{4 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    m_pipeline->setVertexInputLayout(inputLayout);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->create();
}

void RdpRenderNode::uploadPendingPatches(QRhiResourceUpdateBatch* updates) {
    if (m_pendingPatches.isEmpty() || !m_texture)
        return;
    // Debug level only. Lets a black screen be diagnosed as "no patches arrive"
    // (RdpSession) vs. "patches arrive but are not shown" (pipeline/shader).
    qCDebug(lcRdp) << "RdpRenderNode: uploading" << m_pendingPatches.size() << "patch(es) to GPU texture";
    // Defensive bounds check: a patch outside the current texture would be an invalid
    // QRhi subresource upload (backend-dependent error or crash). rebuildTextureIfNeeded()
    // already discards patches from before a resize, but FreeRDP protocol data is
    // external input. Discard rather than clip: a bad patch here indicates a real bug
    // elsewhere.
    const QRect textureBounds(QPoint(0, 0), m_texture->pixelSize());
    // Deliberately one uploadTexture() call per patch rather than one call with many
    // subresource entries: this guarantees the same "later overwrites earlier" order that
    // CPU-side compositing had, for regions that overlap across several merged EndPaint
    // bursts. All calls end up in the same batch and are flushed together via
    // commandBuffer()->resourceUpdate() in prepare().
    for (const auto& [image, rect] : m_pendingPatches) {
        if (image.isNull() || rect.isEmpty())
            continue;
        if (!textureBounds.contains(rect)) {
            qCWarning(lcRdp) << "RdpRenderNode: discarding patch outside texture bounds" << rect
                              << "texture" << textureBounds;
            continue;
        }
        QRhiTextureSubresourceUploadDescription sub(image);
        sub.setDestinationTopLeft(rect.topLeft());
        updates->uploadTexture(m_texture.get(), QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, sub)}));
    }
    m_pendingPatches.clear();
}

void RdpRenderNode::prepare() {
    QRhi* rhi = m_window->rhi();
    if (!rhi)
        return;
    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();

    rebuildVertexBufferIfNeeded(rhi, updates);
    rebuildTextureIfNeeded(rhi, updates);
    if (!m_texture) {
        // No desktop size received yet (RdpItem::onDesktopResized has not fired):
        // nothing to draw this round, but still submit any vertex upload.
        commandBuffer()->resourceUpdate(updates);
        return;
    }
    ensurePipeline(rhi);
    uploadPendingPatches(updates);

    const QMatrix4x4 mvp = *projectionMatrix() * *matrix();
    const float opacity = inheritedOpacity();
    updates->updateDynamicBuffer(m_uniformBuffer.get(), 0, 64, mvp.constData());
    updates->updateDynamicBuffer(m_uniformBuffer.get(), 64, 4, &opacity);

    commandBuffer()->resourceUpdate(updates);
}

void RdpRenderNode::render(const RenderState*) {
    if (!m_pipeline || !m_vertexBuffer)
        return;
    QRhiCommandBuffer* cb = commandBuffer();
    cb->setGraphicsPipeline(m_pipeline.get());
    const QSize renderTargetSize = renderTarget()->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, static_cast<float>(renderTargetSize.width()),
                                  static_cast<float>(renderTargetSize.height())));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vertexBindings[] = {{m_vertexBuffer.get(), 0}};
    cb->setVertexInput(0, 1, vertexBindings);
    cb->draw(4);
}

} // namespace vindauga
