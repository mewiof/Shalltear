// only reader of the shadow map array

#ifndef SHALLTEAR_SHADOWS_HLSLI
#define SHALLTEAR_SHADOWS_HLSLI

#include "ShalltearCommon.hlsli"
#include "CascadeSelect.hlsli"

// only declared by a shader that shadows
[[vk::binding(1, 0)]] Texture2D<float> sltShadowAtlas;
[[vk::binding(2, 0)]] SamplerComparisonState sltShadowSampler;
[[vk::binding(3, 0)]] Texture2D<float> sltPunctualShadowAtlas;

struct ShalltearShadowMap {
	float4 column0;
	float4 column1;
	float4 column2;
	float4 column3;
	float4 uvBounds;
	float farDepth;
	float worldTexelSize;
	float depthBias;
	float normalBias;
};

ShalltearShadowMap ShalltearLoadShadowMap(uint index) {
	return vk::RawBufferLoad<ShalltearShadowMap>(
		sltShadowMapAddress + uint64_t(index) * SHALLTEAR_SHADOW_MAP_STRIDE, 16);
}

// steepest receiver slope as a tangent ~84 degrees
#define SHALLTEAR_SHADOW_MAX_SLOPE 10.0

// both offsets in texels of this map -> applied before projection
float3 ShalltearShadowBiasedPosition(ShalltearShadowMap map, float texelSize, float3 positionWS,
									 float3 normalWS, float3 lightDirection) {
	float NoL = saturate(dot(normalWS, -lightDirection));
	float sinTheta = sqrt(saturate(1.0 - NoL * NoL));
	float tanTheta = min(sinTheta / max(NoL, 1e-3), SHALLTEAR_SHADOW_MAX_SLOPE);

	float3 offset = normalWS * (texelSize * map.normalBias * sinTheta);
	offset -= lightDirection * (texelSize * map.depthBias * (1.0 + tanTheta));
	return positionWS + offset;
}

float3 ShalltearShadowProject(ShalltearShadowMap map, float3 positionWS) {
	float4 projected = map.column0 * positionWS.x + map.column1 * positionWS.y +
					   map.column2 * positionWS.z + map.column3;
	return projected.xyz / max(projected.w, 1e-6);
}

// should stop far fetches on slanted receiver reading itself as occluder
float2 ShalltearShadowReceiverSlope(ShalltearShadowMap map, float texelSize, float3 biased, float3 projected,
									float3 positionDdx, float3 positionDdy, float3 lightDirection) {
	float3 acrossX = ShalltearShadowProject(map, biased + positionDdx * (texelSize / max(length(positionDdx), 1e-20))) - projected;
	float3 acrossY = ShalltearShadowProject(map, biased + positionDdy * (texelSize / max(length(positionDdy), 1e-20))) - projected;
	float3 plane = cross(acrossX, acrossY);
	float towardLight = abs(ShalltearShadowProject(map, biased - lightDirection * texelSize).z - projected.z);
	float steepest = SHALLTEAR_SHADOW_MAX_SLOPE * towardLight / sltShadowAtlasTexel;
	float divisor = max(max(abs(plane.z), length(plane.xy) / max(steepest, 1e-20)), 1e-30);
	return -plane.xy * ((plane.z < 0.0 ? -1.0 : 1.0) / divisor);
}

// tent's area from its left foot to x
float ShalltearShadowTentArea(float x, float halfWidth) {
	float clamped = clamp(x, -halfWidth, halfWidth);
	return 0.5 * halfWidth * halfWidth + halfWidth * clamped - 0.5 * clamped * abs(clamped);
}

// x = where fetch lands in texels, y = weight
float2 ShalltearShadowTentFetch(uint pair, uint pairs, float offset, float halfWidth) {
	float left = float(2 * pair) - float(pairs) - offset;
	float leftArea = ShalltearShadowTentArea(left, halfWidth);
	float middleArea = ShalltearShadowTentArea(left + 1.0, halfWidth);
	float rightArea = ShalltearShadowTentArea(left + 2.0, halfWidth);
	float pairArea = max(rightArea - leftArea, 1e-6);
	return float2(float(2 * pair) - float(pairs) + 0.5 + (rightArea - middleArea) / pairArea,
				  pairArea / (halfWidth * halfWidth));
}

// widths 1-7 cost 1-16 bilinear fetches
float ShalltearShadowTent(Texture2D<float> atlas, float2 uv, float reference, float2 slope, float4 uvBounds) {
	uint width = (uint)max(sltShadowPcfTaps, 1.0);
	if (width <= 1) {
		return atlas.SampleCmpLevelZero(sltShadowSampler, clamp(uv, uvBounds.xy, uvBounds.zw), reference);
	}
	float2 texel = uv / sltShadowAtlasTexel;
	float2 corner = floor(texel + 0.5);
	float2 offset = texel - corner;
	uint pairs = (width + 1) / 2;
	float halfWidth = 0.5 * float(width);
	float sum = 0.0;
	for (uint y = 0; y < pairs; ++y) {
		float2 row = ShalltearShadowTentFetch(y, pairs, offset.y, halfWidth);
		for (uint x = 0; x < pairs; ++x) {
			float2 column = ShalltearShadowTentFetch(x, pairs, offset.x, halfWidth);
			float2 tap = clamp((corner + float2(column.x, row.x)) * sltShadowAtlasTexel, uvBounds.xy, uvBounds.zw);
			sum += column.y * row.y * atlas.SampleCmpLevelZero(sltShadowSampler, tap, reference + dot(tap - uv, slope));
		}
	}
	return sum;
}

float ShalltearSampleShadowMap(Texture2D<float> atlas, uint index, float texelSize, float3 positionWS,
							   float3 positionDdx, float3 positionDdy, float3 normalWS, float3 lightDirection) {
	ShalltearShadowMap map = ShalltearLoadShadowMap(index);
	float3 biased = ShalltearShadowBiasedPosition(map, texelSize, positionWS, normalWS, lightDirection);
	float3 projected = ShalltearShadowProject(map, biased);
	if (projected.z <= 0.0 || projected.z >= 1.0) {
		return 1.0;
	}
	float2 slope = ShalltearShadowReceiverSlope(map, texelSize, biased, projected, positionDdx, positionDdy, lightDirection);
	return ShalltearShadowTent(atlas, projected.xy, projected.z, slope, map.uvBounds);
}

float ShalltearSampleCascade(uint index, float3 positionWS, float3 positionDdx, float3 positionDdy,
							 float3 normalWS, float3 lightDirection) {
	float texel = ShalltearLoadShadowMap(index).worldTexelSize;
	return ShalltearSampleShadowMap(sltShadowAtlas, index, texel, positionWS, positionDdx, positionDdy, normalWS, lightDirection);
}

#define SHALLTEAR_CASCADE_BLEND_FRACTION 0.1

float ShalltearDirectionalShadow(uint firstMap, uint count, float3 positionWS, float3 positionDdx, float3 positionDdy,
								 float3 normalWS, float3 lightDirection, float viewDepth) {
	if (sltShadowMapAddress == 0 || count == 0) {
		return 1.0;
	}
	uint cascade = ShalltearCascadeIndexOf(firstMap, count, viewDepth);
	if (cascade >= count) {
		return 1.0;
	}

	float shadow = ShalltearSampleCascade(firstMap + cascade, positionWS, positionDdx, positionDdy, normalWS, lightDirection);

	float cascadeFar = ShalltearCascadeFar(firstMap + cascade);
	float cascadeNear = cascade == 0 ? 0.0 : ShalltearCascadeFar(firstMap + cascade - 1);
	float band = (cascadeFar - cascadeNear) * SHALLTEAR_CASCADE_BLEND_FRACTION;
	if (cascade + 1 < count && band > 0.0 && viewDepth > cascadeFar - band) {
		float blend = saturate((viewDepth - (cascadeFar - band)) / band);
		shadow = lerp(shadow, ShalltearSampleCascade(firstMap + cascade + 1, positionWS, positionDdx, positionDdy, normalWS, lightDirection), blend);
	}

	float fadeRange = max(sltShadowFadeEnd - sltShadowFadeStart, 1e-4);
	float fade = saturate((sltShadowFadeEnd - viewDepth) / fadeRange);
	return lerp(1.0, shadow, fade);
}

uint ShalltearPointFaceOf(float3 fromLight) {
	float3 a = abs(fromLight);
	if (a.x >= a.y && a.x >= a.z) {
		return fromLight.x >= 0.0 ? 0 : 1;
	}
	if (a.y >= a.z) {
		return fromLight.y >= 0.0 ? 2 : 3;
	}
	return fromLight.z >= 0.0 ? 4 : 5;
}

float ShalltearPunctualShadow(uint firstMap, uint count, float3 lightPosition, float3 positionWS,
							  float3 positionDdx, float3 positionDdy, float3 normalWS) {
	if (sltShadowMapAddress == 0 || count == 0) {
		return 1.0;
	}
	float3 toSurface = positionWS - lightPosition;
	float distanceToLight = max(length(toSurface), 1e-4);
	float3 fromLight = toSurface / distanceToLight;
	uint face = count > 1 ? ShalltearPointFaceOf(fromLight) : 0;

	// scales with receiver distance
	uint index = firstMap + min(face, count - 1);
	float texel = ShalltearLoadShadowMap(index).worldTexelSize * distanceToLight;
	return ShalltearSampleShadowMap(sltPunctualShadowAtlas, index, texel, positionWS, positionDdx, positionDdy, normalWS, fromLight);
}

#endif
