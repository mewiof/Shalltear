// texture slots are declared unconditionally

#pragma shalltear_template lit
#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull back

#pragma shalltear_default baseColor 1 1 1 1
#pragma shalltear_default metallic 0
#pragma shalltear_default roughness 0.5
#pragma shalltear_default reflectance 0.5
#pragma shalltear_default normalScale 1
#pragma shalltear_default emissive 0 0 0
#pragma shalltear_default occlusionStrength 1
#pragma shalltear_default normalTexture normal

#pragma shalltear_range metallic 0 1
#pragma shalltear_range roughness 0 1
#pragma shalltear_range reflectance 0 1
#pragma shalltear_range occlusionStrength 0 1
#pragma shalltear_range normalScale 0 2

#pragma shalltear_hint baseColor color
#pragma shalltear_hint emissive hdr

#pragma shalltear_group "Surface Inputs" baseColor baseColorTexture metallic roughness metallicRoughnessTexture reflectance
#pragma shalltear_group "Detail Inputs" normalScale normalTexture occlusionStrength occlusionTexture
#pragma shalltear_group "Emission" emissive emissiveTexture

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 baseColor;
	float metallic;
	float roughness;
	float reflectance;
	float normalScale;
	float3 emissive;
	float occlusionStrength;
};

[[vk::binding(1, 1)]] Texture2D baseColorTexture;
[[vk::binding(2, 1)]] SamplerState baseColorSampler;
[[vk::binding(3, 1)]] Texture2D metallicRoughnessTexture;
[[vk::binding(4, 1)]] SamplerState metallicRoughnessSampler;
[[vk::binding(5, 1)]] Texture2D normalTexture;
[[vk::binding(6, 1)]] SamplerState normalSampler;
[[vk::binding(7, 1)]] Texture2D occlusionTexture;
[[vk::binding(8, 1)]] SamplerState occlusionSampler;
[[vk::binding(9, 1)]] Texture2D emissiveTexture;
[[vk::binding(10, 1)]] SamplerState emissiveSampler;

void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {
	float4 base = baseColorTexture.Sample(baseColorSampler, input.uv) * baseColor * input.color;
	surface.baseColor = base.rgb;
	surface.alpha = base.a;

	float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(metallicRoughnessSampler, input.uv);
	surface.roughness = saturate(roughness * metallicRoughnessSample.g);
	surface.metallic = saturate(metallic * metallicRoughnessSample.b);
	surface.reflectance = reflectance;

	surface.normal = ShalltearUnpackNormalMap(normalTexture.Sample(normalSampler, input.uv), normalScale);

	float occlusionSample = occlusionTexture.Sample(occlusionSampler, input.uv).r;
	surface.occlusion = lerp(1.0, occlusionSample, occlusionStrength);

	surface.emissive = emissiveTexture.Sample(emissiveSampler, input.uv).rgb * emissive;
}
