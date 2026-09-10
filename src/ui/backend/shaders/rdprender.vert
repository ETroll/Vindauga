#version 440

// Uniform layout, the qt_Matrix/qt_Opacity names and the gl_PerVertex block follow
// Qt's "Scene Graph - Custom QSGRenderNode" example, extended with a texture coordinate.
layout(location = 0) in vec2 position; // item-local pixel coordinates (RdpRenderNode::setContent itemRect)
layout(location = 1) in vec2 texCoord; // 0..1, (0,0) = top-left of the texture (matches QImage row order)

layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
