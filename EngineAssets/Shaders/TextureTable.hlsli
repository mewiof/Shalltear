#ifndef SHALLTEAR_TEXTURE_TABLE_HLSLI
#define SHALLTEAR_TEXTURE_TABLE_HLSLI

[[vk::binding(0, 3)]] SamplerState sltTableSamplers[];
[[vk::binding(1, 3)]] Texture2D<float4> sltTableTextures2D[];
[[vk::binding(2, 3)]] TextureCube<float4> sltTableTexturesCube[];
[[vk::binding(3, 3)]] Texture3D<float4> sltTableTextures3D[];

// entry = image idx in bits 0-19, sampler idx in 20-31
#define SHALLTEAR_TABLE_SAMPLER_SHIFT 20u
#define SHALLTEAR_TABLE_IMAGE_MASK 0xFFFFFu

float4 ShalltearTableSample2D(uint entry, float2 uv, float2 uvDdx, float2 uvDdy) {
	return sltTableTextures2D[NonUniformResourceIndex(entry & SHALLTEAR_TABLE_IMAGE_MASK)].SampleGrad(
		sltTableSamplers[NonUniformResourceIndex(entry >> SHALLTEAR_TABLE_SAMPLER_SHIFT)], uv, uvDdx, uvDdy);
}

float4 ShalltearTableSampleCube(uint entry, float3 direction, float3 directionDdx, float3 directionDdy) {
	return sltTableTexturesCube[NonUniformResourceIndex(entry & SHALLTEAR_TABLE_IMAGE_MASK)].SampleGrad(
		sltTableSamplers[NonUniformResourceIndex(entry >> SHALLTEAR_TABLE_SAMPLER_SHIFT)], direction, directionDdx, directionDdy);
}

#endif
