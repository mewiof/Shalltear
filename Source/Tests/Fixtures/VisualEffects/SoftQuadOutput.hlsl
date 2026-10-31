// QuadOutput, SOFT (VisualEffects.md §3.8): the tint's alpha is scaled by how
// far in front of the depth prepass the fragment is, over a distance the frame
// block holds sixth past its header - declaring `sltSceneDepth` is what asks
// the frame for that prepass at all

#pragma shalltear_blend alpha
#pragma shalltear_depth test_only
#pragma shalltear_cull off
#pragma shalltear_default tint 1 1 1 1

#include "VisualEffects/ParticleOutput.hlsli"

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 tint;
};
[[vk::binding(0, 2)]] cbuffer sltViewRecord {
	ShalltearViewData sltPassView;
};
[[vk::binding(1, 2)]] Texture2D sltSceneDepth;

struct VSOutput {
	float4 positionCS : SV_Position;
	float viewDepth : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	uint index = SltParticleOutputIndex(vertexId);
	float3 center = float3(SltParticleOutputLoad(2, index), SltParticleOutputLoad(3, index), SltParticleOutputLoad(4, index));
	float2 corner = SltParticleQuadCorner(SltParticleOutputCorner(vertexId));
	float3 right = sltPassView.inverseView[0].xyz;
	float3 up = sltPassView.inverseView[1].xyz;
	float3 world = center + (right * corner.x + up * corner.y) * SltParticleEffectFloat(SLT_PARTICLE_EFFECT_HEADER + 16);
	float4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);
	float4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],
												  sltPassView.projection[2], sltPassView.projection[3]);
	float4 viewPosition = mul(view, float4(world, 1.0));
	VSOutput output;
	output.positionCS = mul(projection, viewPosition);
	output.viewDepth = viewPosition.z;
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	float sceneDepth = sltSceneDepth.Load(int3(int2(input.positionCS.xy), 0)).r;
	float fade = SltParticleSoftFade(sceneDepth, input.viewDepth, sltPassView, SltParticleEffectFloat(SLT_PARTICLE_EFFECT_HEADER + 20));
	return float4(tint.rgb, tint.a * fade);
}
