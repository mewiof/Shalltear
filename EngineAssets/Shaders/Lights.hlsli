
#ifndef SHALLTEAR_LIGHTS_HLSLI
#define SHALLTEAR_LIGHTS_HLSLI

#include "ShalltearCommon.hlsli"
#include "Brdf.hlsli"

struct ShalltearLight {
	float3 position;
	float invRangeSquared;
	float3 color;
	float spotScale;
	float3 direction;
	float spotOffset;
	uint shadowIndex;
	uint shadowCount;
	float shadowStrength;
	uint pad0;
};
#define SHALLTEAR_LIGHT_STRIDE 64

ShalltearLight ShalltearLoadLight(uint index) {
	return vk::RawBufferLoad<ShalltearLight>(sltLightAddress + uint64_t(index) * SHALLTEAR_LIGHT_STRIDE, 16);
}

ShalltearLightSample ShalltearSampleDirectional(ShalltearLight light) {
	ShalltearLightSample sample;
	sample.direction = -light.direction;
	sample.color = light.color;
	sample.attenuation = 1.0;
	sample.shadow = 1.0; // integrator folds light's maps after
	return sample;
}

// attenuation 0. Not a branch so walk should stay uniform
ShalltearLightSample ShalltearSamplePunctual(ShalltearLight light, float3 positionWS) {
	float3 toSurface = positionWS - light.position;
	float distanceSquared = dot(toSurface, toSurface);
	float3 fromLight = toSurface * rsqrt(max(distanceSquared, 1e-10));

	float ratio = distanceSquared * light.invRangeSquared;
	float window = saturate(1.0 - ratio * ratio);
	float attenuation = window * window / max(distanceSquared, 1e-4);

	float coneFactor = dot(fromLight, light.direction);
	attenuation *= saturate(coneFactor * light.spotScale + light.spotOffset);

	ShalltearLightSample sample;
	sample.direction = -fromLight;
	sample.color = light.color;
	sample.attenuation = attenuation;
	sample.shadow = 1.0;
	return sample;
}

// must agree with LightCulling.cpp (which bins same point on CPU)
uint ShalltearClusterOf(float2 positionSS, float viewZ) {
	uint2 tile = min(uint2(positionSS * sltClusterTileScale), sltClusterDims.xy - 1);
	int slice = int(log2(max(viewZ, 1e-4)) * sltClusterSliceScale + sltClusterSliceBias);
	uint z = uint(clamp(slice, 0, int(sltClusterDims.z) - 1));
	return (z * sltClusterDims.y + tile.y) * sltClusterDims.x + tile.x;
}

uint2 ShalltearClusterEntry(uint cluster) {
	return vk::RawBufferLoad<uint2>(sltClusterAddress + uint64_t(cluster) * 8, 8);
}

// flat list begins after the table
uint ShalltearClusterLight(uint2 entry, uint slot) {
	uint64_t indices = sltClusterAddress + uint64_t(sltClusterDims.x * sltClusterDims.y * sltClusterDims.z) * 8;
	return vk::RawBufferLoad<uint>(indices + uint64_t(entry.x + slot) * 4, 4);
}

float ShalltearViewDepth(float3 positionWS) {
	return dot(sltViewForwardRow, float4(positionWS, 1.0));
}

#endif
