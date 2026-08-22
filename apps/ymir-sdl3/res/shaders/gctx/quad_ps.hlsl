#include "quad_defs.hlsli"

[[vk::binding(1)]] Texture2D g_texture : register(t0);
[[vk::binding(1)]] SamplerState g_sampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET {
    return g_texture.Sample(g_sampler, input.uv);
    // return float4(input.uv, 0.0, 1.0);
}
