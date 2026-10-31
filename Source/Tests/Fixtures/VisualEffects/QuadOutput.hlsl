// M10's hand-written quad output (VisualEffects.md §3.8): what M11's compiler
// generates from an Output context. Each particle is a camera-facing square,
// its half-size the frame block's fifth value past the header, around
// attributes 2-4, in the material's tint - so a readback says where the
// simulation put a particle and that the draw found it.
// Opaque until the material says otherwise, and then its depth write goes

#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull off
#pragma shalltear_default tint 1 1 1 1

#include "VisualEffects/ParticleOutput.hlsli"

[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {
	float4 tint;
};
[[vk::binding(0, 2)]] cbuffer sltViewRecord {
	ShalltearViewData sltPassView;
};

struct VSOutput {
	float4 positionCS : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	uint index = SltParticleOutputIndex(vertexId);
	float3 center = float3(SltParticleOutputLoad(2, index), SltParticleOutputLoad(3, index), SltParticleOutputLoad(4, index));
	float2 corner = SltParticleQuadCorner(SltParticleOutputCorner(vertexId));
	// the camera's right and up are the inverse view's first two columns
	float3 right = sltPassView.inverseView[0].xyz;
	float3 up = sltPassView.inverseView[1].xyz;
	float3 world = center + (right * corner.x + up * corner.y) * SltParticleEffectFloat(SLT_PARTICLE_EFFECT_HEADER + 16);
	float4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);
	float4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],
												  sltPassView.projection[2], sltPassView.projection[3]);
	VSOutput output;
	output.positionCS = mul(projection, mul(view, float4(world, 1.0)));
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return tint;
}
