#include "SkyContract.hlsli"

#include SHALLTEAR_SURFACE_FILE

ShalltearSkyVaryings VSMain(uint vertexId : SV_VertexID) {
	return ShalltearSkyVertex(vertexId);
}

float4 PSMain(ShalltearSkyVaryings varyings) : SV_Target {
	ShalltearSkyInput input;
	input.directionWS = ShalltearSkyRay(varyings.ndc);
	input.cameraPositionWS = sltCameraPosition;
	input.screenUV = varyings.positionCS.xy * sltRenderExtentInverse;
	input.time = ShalltearTimeParameters();
	input.sunDirectionWS = gSky.sunDirection;
	input.sunColor = gSky.sunColor;
	input.sunAngularDiameter = gSky.sunAngularDiameter;
	input.sunEnabled = any(gSky.sunColor > 0.0);
	input.capturing = gSky.capturing != 0;

	ShalltearSkyOutput sky = ShalltearDefaultSky();
	ShalltearSkyFunction(input, sky);
	return float4(ShalltearExpose(sky.color * gSky.skyIntensity), gSky.weight);
}
