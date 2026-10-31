// shaders should never declare vertex input attributes because everything is pulled

#ifndef SHALLTEAR_COMMON_HLSLI
#define SHALLTEAR_COMMON_HLSLI

struct ShalltearVertex {
	float3 position;
	float u;
	float3 normal;
	float v;
	float4 tangent;
	uint color;
};
#define SHALLTEAR_VERTEX_STRIDE 52

ShalltearVertex ShalltearLoadVertex(uint64_t vertexAddress, uint index) {
	return vk::RawBufferLoad<ShalltearVertex>(vertexAddress + uint64_t(index) * SHALLTEAR_VERTEX_STRIDE, 4);
}

float4 ShalltearUnpackColor(uint packed) {
	return float4(
		float(packed & 0xFF) / 255.0,
		float((packed >> 8) & 0xFF) / 255.0,
		float((packed >> 16) & 0xFF) / 255.0,
		float((packed >> 24) & 0xFF) / 255.0);
}

// push block has only address of this draw's element
struct ShalltearDrawHeader {
	uint64_t drawAddress;
	uint value;
	uint reserved;
};
#define SHALLTEAR_PER_DRAW_STRIDE 128

struct ShalltearPerDraw {
	float4 world0;
	float4 world1;
	float4 world2;
	float4 world3;
	float4 normalMatrix0;
	float4 normalMatrix1;
	float4 normalMatrix2;
	uint64_t vertexAddress;
	float alphaClipThreshold;
	// bit 0 receives shadows, bits 1-3 carry blend
	uint flags;
};
#define SHALLTEAR_DRAW_RECEIVES_SHADOWS 1u
// values are RHI::BlendMode's which RenderSystem.cpp asserts
#define SHALLTEAR_DRAW_BLEND_SHIFT 1u
#define SHALLTEAR_DRAW_BLEND_MASK 7u
#define SHALLTEAR_BLEND_OPAQUE 0u
#define SHALLTEAR_BLEND_ALPHA 1u
#define SHALLTEAR_BLEND_ADDITIVE 2u
#define SHALLTEAR_BLEND_PREMULTIPLIED 3u
#define SHALLTEAR_BLEND_MULTIPLY 4u

// passing block by value miscompiles in DXC
ShalltearPerDraw ShalltearLoadPerDraw(uint64_t drawAddress) {
	return vk::RawBufferLoad<ShalltearPerDraw>(drawAddress, 16);
}

#define SHALLTEAR_ALPHA_CLIP_OFFSET 120
float ShalltearLoadAlphaClipThreshold(uint64_t drawAddress) {
	return vk::RawBufferLoad<float>(drawAddress + SHALLTEAR_ALPHA_CLIP_OFFSET, 4);
}

#define SHALLTEAR_DRAW_FLAGS_OFFSET 124
uint ShalltearLoadDrawFlags(uint64_t drawAddress) {
	return vk::RawBufferLoad<uint>(drawAddress + SHALLTEAR_DRAW_FLAGS_OFFSET, 4);
}

uint ShalltearLoadDrawBlend(uint64_t drawAddress) {
	return (ShalltearLoadDrawFlags(drawAddress) >> SHALLTEAR_DRAW_BLEND_SHIFT) & SHALLTEAR_DRAW_BLEND_MASK;
}

float4 ShalltearTransformPosition(ShalltearPerDraw draw, float3 position) {
	return draw.world0 * position.x + draw.world1 * position.y + draw.world2 * position.z + draw.world3;
}

// sign of the world matrix determinant applied before bitangent
float ShalltearDrawHandedness(ShalltearPerDraw draw) {
	return dot(cross(draw.world0.xyz, draw.world1.xyz), draw.world2.xyz) < 0.0 ? -1.0 : 1.0;
}

float3 ShalltearTransformNormal(float4 column0, float4 column1, float4 column2, float3 normal) {
	float3x3 normalMatrix = float3x3(column0.xyz, column1.xyz, column2.xyz);
	float3 transformed = mul(normal, normalMatrix);
	// normalize gives NaN for 0 length tangents
	float lengthSquared = dot(transformed, transformed);
	return lengthSquared > 1e-20 ? transformed * rsqrt(lengthSquared) : transformed;
}

// per view/bound once per view from upload ring
[[vk::binding(0, 0)]] cbuffer ShalltearView {
	float4x4 sltViewProjection;
	float3 sltCameraPosition;
	float sltDirectionalLightCount;
	float3 sltAmbientColor;
	float sltPunctualLightCount;
	uint64_t sltLightAddress;
	uint64_t sltClusterAddress;
	float4 sltViewForwardRow;
	uint3 sltClusterDims;
	uint sltClusterPad;
	float2 sltClusterTileScale; // pixels2tile
	float sltClusterSliceScale;
	float sltClusterSliceBias;
	uint64_t sltShadowMapAddress;
	float sltShadowCascadeCount;
	float sltShadowAtlasTexel;
	float sltShadowPcfTaps;
	float sltShadowFadeStart;
	float sltShadowFadeEnd;
	float sltTransparentReceivesShadows;
	uint64_t sltEnvironmentAddress;
	// SV_Position = pixel center. No 1/2 texel offset should be here
	float2 sltRenderExtentInverse;
	uint64_t sltProbeAddress;
	float sltProbeCount;
	float sltFogLayerCount;
	uint64_t sltViewDataAddress;
	float sltViewDataPad0;
	float sltViewDataPad1;
};

#include "ViewData.hlsli"

// 0 address reads 0, not losing the device
float4 ShalltearLoadViewFloat4(uint offset) {
	if (sltViewDataAddress == 0) {
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	return vk::RawBufferLoad<float4>(sltViewDataAddress + offset, 16);
}

float4x4 ShalltearLoadViewMatrix(uint offset) {
	return ShalltearViewDataMatrix(ShalltearLoadViewFloat4(offset), ShalltearLoadViewFloat4(offset + 16), ShalltearLoadViewFloat4(offset + 32),
								   ShalltearLoadViewFloat4(offset + 48));
}

float4x4 ShalltearViewMatrix() { return ShalltearLoadViewMatrix(0); }
float4x4 ShalltearInverseViewMatrix() { return ShalltearLoadViewMatrix(64); }
float4x4 ShalltearProjectionMatrix() { return ShalltearLoadViewMatrix(128); }
float4x4 ShalltearInverseProjectionMatrix() { return ShalltearLoadViewMatrix(192); }
float4x4 ShalltearInverseViewProjectionMatrix() { return ShalltearLoadViewMatrix(256); }
float4 ShalltearTimeParameters() { return ShalltearLoadViewFloat4(SHALLTEAR_VIEW_DATA_TIME); }
float4 ShalltearProjectionParameters() { return ShalltearLoadViewFloat4(SHALLTEAR_VIEW_DATA_PROJECTION_PARAMS); }
float4 ShalltearScreenParameters() { return ShalltearLoadViewFloat4(SHALLTEAR_VIEW_DATA_SCREEN_PARAMS); }

float ShalltearGetExposure() {
	if (sltViewDataAddress == 0) {
		return 1.0;
	}
	return ShalltearLoadExposure(vk::RawBufferLoad<uint64_t>(sltViewDataAddress + SHALLTEAR_VIEW_DATA_EXPOSURE, 8));
}

#define SHALLTEAR_HALF_MAX 65504.0

// writers clamp to finite 1/2 range after fog
float3 ShalltearExpose(float3 radiance) {
	return min(radiance * ShalltearGetExposure(), SHALLTEAR_HALF_MAX);
}

float4 ShalltearExpose(float4 color, uint blend) {
	if (blend == SHALLTEAR_BLEND_MULTIPLY) {
		return color;
	}
	return float4(ShalltearExpose(color.rgb), color.a);
}

// orthographic rays are parallel. This is camera's backward axis
float3 ShalltearViewDirection(float3 positionWS) {
	if (ShalltearProjectionParameters().z != 0.0) {
		return -normalize(ShalltearViewMatrix()[2].xyz);
	}
	return normalize(sltCameraPosition - positionWS);
}

[[vk::binding(6, 0)]] Texture2D<float> sltAmbientOcclusion;
[[vk::binding(7, 0)]] SamplerState sltAmbientOcclusionSampler;

float ShalltearScreenSpaceOcclusion(float2 positionSS) {
	return sltAmbientOcclusion.SampleLevel(sltAmbientOcclusionSampler, positionSS * sltRenderExtentInverse, 0);
}

#endif
