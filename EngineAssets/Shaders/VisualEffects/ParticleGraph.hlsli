#ifndef SHALLTEAR_PARTICLE_GRAPH_HLSLI
#define SHALLTEAR_PARTICLE_GRAPH_HLSLI

#include "Surface.hlsli"

float4x4 SltParticleObjectToWorld(float3x3 element, float3 origin) {
	return float4x4(float4(element[0], origin.x), float4(element[1], origin.y), float4(element[2], origin.z), float4(0.0, 0.0, 0.0, 1.0));
}

float4x4 SltParticleAffineInverse(float4x4 m) {
	float3 r0 = m[0].xyz;
	float3 r1 = m[1].xyz;
	float3 r2 = m[2].xyz;
	float3 t = float3(m[0].w, m[1].w, m[2].w);
	float3 c0 = cross(r1, r2);
	float3 c1 = cross(r2, r0);
	float3 c2 = cross(r0, r1);
	float inverseDeterminant = 1.0 / dot(r0, c0);
	float3 i0 = float3(c0.x, c1.x, c2.x) * inverseDeterminant;
	float3 i1 = float3(c0.y, c1.y, c2.y) * inverseDeterminant;
	float3 i2 = float3(c0.z, c1.z, c2.z) * inverseDeterminant;
	return float4x4(float4(i0, -dot(i0, t)), float4(i1, -dot(i1, t)), float4(i2, -dot(i2, t)), float4(0.0, 0.0, 0.0, 1.0));
}

void SltParticleGraphPlace(inout ShalltearVertexInput input) {
	input.positionWS = mul(input.objectToWorld, float4(input.positionOS, 1.0)).xyz;
	input.normalWS = normalize(mul(input.normalOS, (float3x3)input.worldToObject));
	input.tangentWS = float4(normalize(mul((float3x3)input.objectToWorld, input.tangentOS.xyz)),
							 input.tangentOS.w * (determinant((float3x3)input.objectToWorld) < 0.0 ? -1.0 : 1.0));
	input.viewDirectionWS = ShalltearViewDirection(input.positionWS);
}

ShalltearVertexInput SltParticleGraphVertexInput(float3 positionOS, float3 normalOS, float4 tangentOS, float2 uv, float4 color, uint vertexId,
												  float4x4 objectToWorld, float4x4 worldToObject) {
	ShalltearVertexInput input;
	input.positionOS = positionOS;
	input.normalOS = normalOS;
	input.tangentOS = tangentOS;
	input.uv = uv;
	input.color = color;
	input.vertexId = vertexId;
	input.objectToWorld = objectToWorld;
	input.worldToObject = worldToObject;
	SltParticleGraphPlace(input);
	return input;
}

ShalltearSurfaceInput SltParticleGraphSurfaceInput(float4 positionCS, float3 positionWS, float3 normalWS, float4 tangentWS, float2 uv, float4 color,
													bool isFrontFace, float4x4 objectToWorld) {
	ShalltearSurfaceInput input;
	input.positionWS = positionWS;
	input.normalWS = normalize(normalWS);
	input.tangentWS = tangentWS;
	input.uv = uv;
	input.color = color;
	input.viewDirectionWS = ShalltearViewDirection(positionWS);
	input.positionSS = positionCS.xy;
	input.isFrontFace = isFrontFace;
	input.objectToWorld = objectToWorld;
	input.worldToObject = SltParticleAffineInverse(objectToWorld);
	[unroll] for (uint slot = 0; slot < SHALLTEAR_MAX_CUSTOM_INTERPOLATORS; ++slot) {
		input.interpolators[slot] = float4(0.0, 0.0, 0.0, 0.0);
	}
	return input;
}

#endif
