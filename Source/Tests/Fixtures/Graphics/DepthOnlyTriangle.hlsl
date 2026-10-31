// test-only shader for the RHI's depth-only pipeline (Graphics.md section 9
// note 42): MsaaTriangle's lower-left triangle again, at a depth of 0.5 so the
// texels it covers read back as something other than the clear. THE PIXEL
// STAGE BELOW IS NEVER BOUND -- the case builds its pipeline from the vertex
// blob alone, which is the whole claim -- and exists because the importer
// compiles both stages of every shader it is handed

#pragma shalltear_blend opaque
#pragma shalltear_depth test_write
#pragma shalltear_cull off

struct VSOutput {
	float4 positionCS : SV_Position;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	float2 corners[3] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0)};
	VSOutput output;
	output.positionCS = float4(corners[vertexId], 0.5, 1.0);
	return output;
}

float4 PSMain(VSOutput input) : SV_Target {
	return float4(1.0, 1.0, 1.0, 1.0);
}
