#version 440

// Samples the persistent desktop texture owned by RdpRenderNode. qt_Opacity is
// multiplied in as in Qt's custom render node example (correct for Qt Quick's
// premultiplied-alpha blend mode; see RdpRenderNode::ensurePipeline()).
layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    // Force alpha to 1.0 instead of using the source texture's own alpha byte. The
    // source is FreeRDP's GDI primary buffer, whose alpha channel is not guaranteed to
    // be 0xFF; with blending enabled that would blend the desktop against the
    // background instead of drawing it. The desktop is always opaque, so only
    // qt_Opacity may affect the output alpha.
    fragColor = vec4(texture(source, vTexCoord).rgb, 1.0) * qt_Opacity;
}
