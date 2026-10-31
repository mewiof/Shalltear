// reflection-test fixture: exercises UBO (set 0), material UBO + split
// texture/sampler (set 1), push constants, and BDA vertex pulling
struct PerDraw {
	float4x4 world;
	float4 c0;
	float4 c1;
	float4 c2;
	uint64_t vertexAddress;
};
[[vk::push_constant]] PerDraw gPerDraw;

[[vk::binding(0, 0)]] cbuffer ViewData {
	float4x4 viewProjection;
};
[[vk::binding(0, 1)]] cbuffer MaterialData {
	float4 baseColor;
};
[[vk::binding(1, 1)]] Texture2D baseTexture;
[[vk::binding(2, 1)]] SamplerState baseSampler;

struct VSOutput {
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	float3 position = vk::RawBufferLoad<float3>(gPerDraw.vertexAddress + uint64_t(vertexId) * 52, 4);
	VSOutput output;
	output.positionCS = mul(viewProjection, mul(gPerDraw.world, float4(position, 1.0)));
	output.uv = position.xy;
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return baseTexture.Sample(baseSampler, input.uv) * baseColor;
}
