#include "quad_defs.hlsli"

[[vk::binding(0)]] ConstantBuffer<DrawTextureConstants> g_consts : register(b0);

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD) {
    PSInput result;

    // This imitates SDL_RenderTextureRotated, where:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - rotPivot is the rotation pivot point

    float2 origin = g_consts.dstRect.xy;
    const float2 size = g_consts.dstRect.zw;
    origin.y = g_consts.renderTargetSize.y - origin.y - size.y;

    result.position = position;
    result.position.xy *= size;
    result.position.xy = Rotate2DPivot(result.position.xy, -radians(g_consts.rotAngle), g_consts.rotPivot);
    result.position.xy += origin;
    result.position.xy /= g_consts.renderTargetSize;
    result.position.xy *= 2.0f;
    result.position.xy -= 1.0f;

    result.uv = lerp(g_consts.srcRect.xy, g_consts.srcRect.xy + g_consts.srcRect.zw, uv);

    return result;
}
