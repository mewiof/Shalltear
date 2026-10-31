#ifndef SHALLTEAR_PARTICLE_LIT_HLSLI
#define SHALLTEAR_PARTICLE_LIT_HLSLI

#include "Shading.hlsli"

ShalltearSurfaceInput SltParticleSurfaceInput(float4 positionCS, float3 positionWS, float3 normalWS, float4 tangentWS, bool isFrontFace) {
	ShalltearSurfaceInput input = (ShalltearSurfaceInput)0;
	input.positionWS = positionWS;
	input.normalWS = normalWS;
	input.tangentWS = tangentWS;
	input.viewDirectionWS = ShalltearViewDirection(positionWS);
	input.positionSS = positionCS.xy;
	input.isFrontFace = isFrontFace;
	input.objectToWorld = float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
	input.worldToObject = input.objectToWorld;
	return input;
}

float SltParticleReceivesShadows(bool receives, bool blended) {
	return receives ? (blended ? sltTransparentReceivesShadows : 1.0) : 0.0;
}

#endif
