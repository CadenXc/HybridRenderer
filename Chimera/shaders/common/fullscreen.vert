#version 460

layout(location = 0) out vec2 outUV;

void main() 
{
    // 利用 gl_VertexIndex 自动生成覆盖整个屏幕的大三角形
    // 顶点索引 0: (-1, -1), UV: (0, 0)
    // 顶点索引 1: ( 3, -1), UV: (2, 0)
    // 顶点索引 2: (-1,  3), UV: (0, 2)
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}
