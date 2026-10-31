#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

[[vk::binding(0, 1)]] Texture2D sltSource;
[[vk::binding(1, 1)]] SamplerState sltSourceSampler;

struct VSOutput {
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
	output.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);
	output.uv = float2(corner.x, 1.0 - corner.y);
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return sltSource.Sample(sltSourceSampler, input.uv);
}
