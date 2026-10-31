#pragma shalltear_blend opaque
#pragma shalltear_depth test_only
#pragma shalltear_cull off

#include "SkyContract.hlsli"

[[vk::binding(4, 0)]] TextureCube sltSkyCube;
[[vk::binding(5, 0)]] SamplerState sltSkySampler;

ShalltearSkyVaryings VSMain(uint vertexId : SV_VertexID) {
	return ShalltearSkyVertex(vertexId);
}

float4 PSMain(ShalltearSkyVaryings input) : SV_Target {
	float3 direction = ShalltearSkyRay(input.ndc);
	float3 color = sltSkyCube.SampleLevel(sltSkySampler, direction, 0.0).rgb * gSky.skyIntensity;
	color += ShalltearSkySunDisc(direction);
	return float4(ShalltearExpose(color), gSky.weight);
}
