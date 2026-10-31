// test-only shader for the RHI's indirect draw (VisualEffects.md §3.3): every
// six vertices are one quad filling one column of a sixteen-column target, so
// how many columns turn white says how many vertices the draw ran - which is
// what a kernel wrote into the arguments, and nothing the CPU knew

#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

static const float COLUMNS = 16.0;

struct VSOutput {
	float4 positionCS : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	float2 corners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
						 float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};
	float column = float(vertexId / 6u);
	float2 corner = corners[vertexId % 6u];
	VSOutput output;
	output.positionCS = float4(-1.0 + 2.0 * (column + corner.x) / COLUMNS, -1.0 + 2.0 * corner.y, 0.5, 1.0);
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return float4(1.0, 1.0, 1.0, 1.0);
}
