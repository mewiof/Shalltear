#pragma shalltear_template unlit
#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull back

#pragma shalltear_default baseColor 1 1 1 1

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
};

[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState baseColorSampler;

void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {
	float4 base = baseColorTexture.Sample(baseColorSampler, input.uv) * baseColor * input.color;
	surface.baseColor = base.rgb;
	surface.alpha = base.a;
}
