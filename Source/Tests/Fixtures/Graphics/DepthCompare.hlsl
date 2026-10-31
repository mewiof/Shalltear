// test-only shader for the M1 comparison-sampler exit criterion (Rendering.md
// section 7.2): reads a depth texture through a SamplerComparisonState and
// returns the pass fraction.
//
// THE REFERENCE SWEEPS ACROSS X, so the result is a STEP at whatever depth the
// map holds. That is what makes the assertion falsifiable: a sampler created
// without SamplerDesc::comparison returns the stored depth itself - one value
// everywhere - and a step can never be read out of it

#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

[[vk::binding(0, 1)]] Texture2D sltDepth;
[[vk::binding(1, 1)]] SamplerComparisonState sltDepthCompare;

struct VSOutput {
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

// the fullscreen triangle, with v flipped for the engine's negative-height
// viewport exactly as PresentPass.hlsl documents
VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
	output.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);
	output.uv = float2(corner.x, 1.0 - corner.y);
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	// reverse-Z (Graphics.md decision 14) compares GREATER_OR_EQUAL, so a
	// reference at or above the stored depth passes and the RIGHT-hand side of
	// the image is the white half
	float passed = sltDepth.SampleCmpLevelZero(sltDepthCompare, input.uv, input.uv.x);
	return float4(passed, passed, passed, 1.0);
}
