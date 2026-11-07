// light walk never forks

#ifndef SHALLTEAR_SHADING_HLSLI
#define SHALLTEAR_SHADING_HLSLI

#include "Surface.hlsli"
#include "Brdf.hlsli"
#include "Lights.hlsli"
#include "Shadows.hlsli"
#include "Environment.hlsli"

float3 ShalltearIntegrateLight(ShalltearSurfaceInput surfaceInput, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {
#ifdef SHALLTEAR_CUSTOM_LIGHTING
	return ShalltearLightingFunction(surfaceInput, surface, normalWS, light);
#else
	return ShalltearStandardLighting(surface, normalWS, light, surfaceInput.viewDirectionWS);
#endif
}

float3 ShalltearShadeSurface(ShalltearSurfaceInput surfaceInput, ShalltearSurface surface, float3 normalWS, float receivesShadows, uint drawLayers) {
	// walks diverge
	float3 positionDdx = ddx(surfaceInput.positionWS);
	float3 positionDdy = ddy(surfaceInput.positionWS);

	surface.occlusion *= ShalltearScreenSpaceOcclusion(surfaceInput.positionSS);

	float NoV = abs(dot(normalWS, surfaceInput.viewDirectionWS)) + 1e-5;
	float3 reflectionWS = reflect(-surfaceInput.viewDirectionWS, normalWS);
	float3 lighting = ShalltearIndirectDiffuse(surface, normalWS, surfaceInput.positionWS) +
					  ShalltearIndirectSpecular(surface, reflectionWS, NoV, surfaceInput.positionWS);

	// directional lights -> punctual ones through the froxel
	float viewDepth = ShalltearViewDepth(surfaceInput.positionWS);
	uint directionalCount = (uint)sltDirectionalLightCount;
	for (uint d = 0; d < directionalCount; ++d) {
		ShalltearLight directional = ShalltearLoadLight(d);
		if (!ShalltearLightReaches(directional, drawLayers)) {
			continue;
		}
		ShalltearLightSample sample = ShalltearSampleDirectional(directional);
		sample.color *= ShalltearLightCookieColor(directional, surfaceInput.positionWS, positionDdx, positionDdy);
		sample.shadow = lerp(1.0, ShalltearDirectionalShadow(directional.shadowIndex, directional.shadowCount, surfaceInput.positionWS, positionDdx,
															 positionDdy, surfaceInput.normalWS, directional.direction, viewDepth),
							 receivesShadows * directional.shadowStrength);
		sample.attenuation *= sample.shadow;
		lighting += ShalltearIntegrateLight(surfaceInput, surface, normalWS, sample);
	}

	if (sltPunctualLightCount > 0.0) {
		uint cluster = ShalltearClusterOf(surfaceInput.positionSS, viewDepth);
		uint2 entry = ShalltearClusterEntry(cluster);
		uint punctualCount = entry.y & 0xFFu;
		for (uint p = 0; p < punctualCount; ++p) {
			ShalltearLight light = ShalltearLoadLight(ShalltearClusterLight(entry, p));
			if (!ShalltearLightReaches(light, drawLayers)) {
				continue;
			}
			ShalltearLightSample sample = ShalltearSamplePunctual(light, surfaceInput.positionWS);
			sample.color *= ShalltearLightCookieColor(light, surfaceInput.positionWS, positionDdx, positionDdy);
			if (light.shadowCount > 0) {
				sample.shadow = lerp(1.0, ShalltearPunctualShadow(light.shadowIndex, light.shadowCount, light.position, surfaceInput.positionWS,
																  positionDdx, positionDdy, surfaceInput.normalWS),
									 receivesShadows * light.shadowStrength);
				sample.attenuation *= sample.shadow;
			}
			lighting += ShalltearIntegrateLight(surfaceInput, surface, normalWS, sample);
		}
	}
	return lighting;
}

float3 ShalltearShadeSurface(ShalltearSurfaceInput surfaceInput, ShalltearSurface surface, float3 normalWS, float receivesShadows) {
	return ShalltearShadeSurface(surfaceInput, surface, normalWS, receivesShadows, 0xFFu);
}

#endif
