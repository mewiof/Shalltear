#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull off

[[vk::binding(0, 1)]] Texture2D<float> sltSource;
[[vk::binding(1, 1)]] SamplerState sltSourceSampler;

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position {
	float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
	return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float PSMain(float4 positionCS : SV_Position) : SV_Depth {
	return sltSource.Load(int3(int2(positionCS.xy), 0));
}
