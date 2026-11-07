#pragma shalltear_template lit
#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull back

#pragma shalltear_default baseColor 1 1 1 1
#pragma shalltear_default normalScale 1
#pragma shalltear_default emissive 0 0 0
#pragma shalltear_default normalTexture normal

#pragma shalltear_range normalScale 0 2

#pragma shalltear_hint baseColor color
#pragma shalltear_hint emissive hdr

#pragma shalltear_group "Surface Inputs" baseColor baseColorTexture maskTexture
#pragma shalltear_group "Detail Inputs" normalScale normalTexture
#pragma shalltear_group "Emission" emissive emissiveTexture

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float3 emissive;
	float normalScale;
};

[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState baseColorSampler;
[[vk::binding(3, 1)]] Texture2D normalTexture;
[[vk::binding(4, 1)]] SamplerState normalSampler;
// r/g/b = occlusion/roughness/metallic, used only when projected as decal
[[vk::binding(5, 1)]] Texture2D maskTexture;
[[vk::binding(6, 1)]] SamplerState maskSampler;
[[vk::binding(7, 1)]] Texture2D emissiveTexture;
[[vk::binding(8, 1)]] SamplerState emissiveSampler;

void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {
	float4 base = baseColorTexture.Sample(baseColorSampler, input.uv) * baseColor;
	surface.baseColor = base.rgb;
	surface.alpha = base.a;
	surface.normal = ShalltearUnpackNormalMap(normalTexture.Sample(normalSampler, input.uv), normalScale);
	surface.emissive = emissiveTexture.Sample(emissiveSampler, input.uv).rgb * emissive;
}
