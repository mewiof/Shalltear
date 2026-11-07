
#ifndef SHALLTEAR_LIGHTS_HLSLI
#define SHALLTEAR_LIGHTS_HLSLI

#include "ShalltearCommon.hlsli"
#include "Brdf.hlsli"
#include "TextureTable.hlsli"

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
	// bits 0-7 layers, 8-15 flags, 16-31 extension slot
	uint info;
};
#define SHALLTEAR_LIGHT_STRIDE 64
#define SHALLTEAR_LIGHT_FLAG_COOKIE 0x100u
#define SHALLTEAR_LIGHT_FLAG_EVERY_LAYER 0x200u

ShalltearLight ShalltearLoadLight(uint index) {
	return vk::RawBufferLoad<ShalltearLight>(sltLightAddress + uint64_t(index) * SHALLTEAR_LIGHT_STRIDE, 16);
}

bool ShalltearLightReaches(ShalltearLight light, uint drawLayers) {
	return (light.info & SHALLTEAR_LIGHT_FLAG_EVERY_LAYER) != 0 || (light.info & drawLayers & 0xFFu) != 0;
}

struct ShalltearLightCookie {
	float4 row0;
	float4 row1;
	float4 row2;
	uint entry;
	uint projection;
	uint pad0;
	uint pad1;
};
#define SHALLTEAR_COOKIE_ORTHOGRAPHIC 0u
#define SHALLTEAR_COOKIE_PERSPECTIVE 1u

float3 ShalltearLightCookieColor(ShalltearLight light, float3 positionWS, float3 positionDdx, float3 positionDdy) {
	if ((light.info & SHALLTEAR_LIGHT_FLAG_COOKIE) == 0) {
		return float3(1.0, 1.0, 1.0);
	}
	ShalltearLightCookie cookie =
		vk::RawBufferLoad<ShalltearLightCookie>(sltLightAddress + uint64_t(light.info >> 16) * SHALLTEAR_LIGHT_STRIDE, 16);
	float4 position = float4(positionWS, 1.0);
	float3 h = float3(dot(cookie.row0, position), dot(cookie.row1, position), dot(cookie.row2, position));
	float3 hDdx = float3(dot(cookie.row0.xyz, positionDdx), dot(cookie.row1.xyz, positionDdx), dot(cookie.row2.xyz, positionDdx));
	float3 hDdy = float3(dot(cookie.row0.xyz, positionDdy), dot(cookie.row1.xyz, positionDdy), dot(cookie.row2.xyz, positionDdy));
	if (cookie.projection == SHALLTEAR_COOKIE_ORTHOGRAPHIC) {
		return ShalltearTableSample2D(cookie.entry, h.xy, hDdx.xy, hDdy.xy).rgb;
	}
	if (cookie.projection == SHALLTEAR_COOKIE_PERSPECTIVE) {
		float2 uv = h.xy / max(h.z, 1e-6);
		float2 uvDdx = (h.xy + hDdx.xy) / max(h.z + hDdx.z, 1e-6) - uv;
		float2 uvDdy = (h.xy + hDdy.xy) / max(h.z + hDdy.z, 1e-6) - uv;
		return ShalltearTableSample2D(cookie.entry, saturate(uv), uvDdx, uvDdy).rgb;
	}
	return ShalltearTableSampleCube(cookie.entry, h, hDdx, hDdy).rgb;
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
