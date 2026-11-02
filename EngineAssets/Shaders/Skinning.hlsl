#pragma shalltear_kernel
#include "ShalltearCommon.hlsli"

// must match GPUSkinningPush in Skinning.h
struct ShalltearSkinningPush {
	uint64_t source;
	uint64_t output;
	uint64_t influences;
	uint64_t matrices;
	uint64_t morphRanges;
	uint64_t morphDeltas;
	uint64_t frameScales;
	uint vertexCount;
	uint influenceCount;
	uint jointCount;
	uint flags;
};
[[vk::push_constant]] ShalltearSkinningPush gSkin;

#define SHALLTEAR_SKINNING_GROUP_SIZE 64u
#define SHALLTEAR_SKINNING_ROW (65535u * SHALLTEAR_SKINNING_GROUP_SIZE)
#define SHALLTEAR_SKINNING_RENORMALIZE 1u
#define SHALLTEAR_MORPH_DELTA_STRIDE 32
#define SHALLTEAR_INFLUENCE_STRIDE 24

float3 SkinningNormalize(float3 v) {
	float size = sqrt(dot(v, v));
	if (size > 1e-5) {
		return v / size;
	}
	return float3(0.0, 0.0, 0.0);
}

[numthreads(SHALLTEAR_SKINNING_GROUP_SIZE, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	uint index = id.y * SHALLTEAR_SKINNING_ROW + id.x;
	if (index >= gSkin.vertexCount) {
		return;
	}
	ShalltearVertex vertex = ShalltearLoadVertex(gSkin.source, index);

	if (gSkin.frameScales != 0) {
		uint2 range = vk::RawBufferLoad<uint2>(gSkin.morphRanges + uint64_t(index) * 8, 8);
		for (uint entry = range.x; entry < range.x + range.y; ++entry) {
			uint64_t at = gSkin.morphDeltas + uint64_t(entry) * SHALLTEAR_MORPH_DELTA_STRIDE;
			float4 position = vk::RawBufferLoad<float4>(at, 16);
			float scale = vk::RawBufferLoad<float>(gSkin.frameScales + uint64_t(asuint(position.w)) * 4, 4);
			if (scale != 0.0) {
				vertex.position += position.xyz * scale;
				vertex.normal += vk::RawBufferLoad<float4>(at + 16, 16).xyz * scale;
			}
		}
		if ((gSkin.flags & SHALLTEAR_SKINNING_RENORMALIZE) != 0) {
			vertex.normal = SkinningNormalize(vertex.normal);
		}
	}

	if (gSkin.matrices != 0 && index < gSkin.influenceCount) {
		uint64_t at = gSkin.influences + uint64_t(index) * SHALLTEAR_INFLUENCE_STRIDE;
		uint2 packed = vk::RawBufferLoad<uint2>(at, 8);
		float4 weights = vk::RawBufferLoad<float4>(at + 8, 8);
		uint joints[4] = {packed.x & 0xFFFFu, packed.x >> 16, packed.y & 0xFFFFu, packed.y >> 16};

		float4 column0 = float4(0.0, 0.0, 0.0, 0.0);
		float4 column1 = float4(0.0, 0.0, 0.0, 0.0);
		float4 column2 = float4(0.0, 0.0, 0.0, 0.0);
		float4 column3 = float4(0.0, 0.0, 0.0, 0.0);
		float total = 0.0;
		for (uint k = 0; k < 4; ++k) {
			float weight = weights[k];
			if (weight == 0.0 || joints[k] >= gSkin.jointCount) {
				continue;
			}
			uint64_t matrix = gSkin.matrices + uint64_t(joints[k]) * 64;
			column0 += vk::RawBufferLoad<float4>(matrix, 16) * weight;
			column1 += vk::RawBufferLoad<float4>(matrix + 16, 16) * weight;
			column2 += vk::RawBufferLoad<float4>(matrix + 32, 16) * weight;
			column3 += vk::RawBufferLoad<float4>(matrix + 48, 16) * weight;
			total += weight;
		}

		if (total > 0.0) {
			if (total != 1.0) {
				float rescale = 1.0 / total;
				column0 *= rescale;
				column1 *= rescale;
				column2 *= rescale;
				column3 *= rescale;
			}
			float3 position = vertex.position;
			float3 normal = vertex.normal;
			float3 tangent = vertex.tangent.xyz;
			vertex.position = (column0 * position.x + column1 * position.y + column2 * position.z + column3).xyz;
			vertex.normal = SkinningNormalize(column0.xyz * normal.x + column1.xyz * normal.y + column2.xyz * normal.z);
			vertex.tangent.xyz = column0.xyz * tangent.x + column1.xyz * tangent.y + column2.xyz * tangent.z;
		}
	}

	uint64_t output = gSkin.output + uint64_t(index) * SHALLTEAR_VERTEX_STRIDE;
	vk::RawBufferStore<float4>(output, float4(vertex.position, vertex.u), 4);
	vk::RawBufferStore<float4>(output + 16, float4(vertex.normal, vertex.v), 4);
	vk::RawBufferStore<float4>(output + 32, vertex.tangent, 4);
	vk::RawBufferStore<uint>(output + 48, vertex.color, 4);
}
