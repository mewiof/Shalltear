#ifndef SHALLTEAR_PARTICLE_CAMERA_HLSLI
#define SHALLTEAR_PARTICLE_CAMERA_HLSLI

#include "ShalltearCommon.hlsli"

float4 SltParticleCameraClipWRow() {
	return mul(ShalltearProjectionMatrix(), ShalltearViewMatrix())[3];
}

#endif
