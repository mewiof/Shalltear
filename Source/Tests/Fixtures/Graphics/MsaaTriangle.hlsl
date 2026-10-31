// test-only shader for the M1 multisampling exit criterion (Rendering.md
// section 7.2): a white right triangle covering the lower-left half of the
// viewport, so its hypotenuse is a DIAGONAL EDGE across the middle of the
// target. Nothing varies across the surface, which is the point - every texel
// is either the clear color, white, or a coverage blend of the two, so a
// value strictly between them can only have come from multisampling

#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

struct VSOutput {
	float4 positionCS : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	// clip-space corners, no geometry (decision 10 leaves vertex pulling to
	// the shader and this one pulls from nothing)
	float2 corners[3] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0)};
	VSOutput output;
	output.positionCS = float4(corners[vertexId], 0.0, 1.0);
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return float4(1.0, 1.0, 1.0, 1.0);
}
