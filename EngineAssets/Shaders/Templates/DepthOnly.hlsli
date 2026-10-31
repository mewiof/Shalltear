#include "Surface.hlsli"
#include "Brdf.hlsli"

#include SHALLTEAR_SURFACE_FILE

#include "Vertex.hlsli"

ShalltearSurface ShalltearDepthSurface(ShalltearVaryings input, bool isFrontFace, out ShalltearSurfaceInput surfaceInput) {
	surfaceInput = ShalltearBuildPixelSurfaceInput(input, isFrontFace);

	ShalltearSurface surface = ShalltearDefaultSurface();
	ShalltearSurfaceFunction(surfaceInput, surface);

#ifdef SHALLTEAR_ALPHA_CLIP
	clip(surface.alpha - ShalltearLoadAlphaClipThreshold(gDraw.drawAddress));
#endif
	return surface;
}

#ifdef SHALLTEAR_DEPTH_NO_COLOR
void PSMain(ShalltearVaryings input, bool isFrontFace : SV_IsFrontFace) {
	ShalltearSurfaceInput surfaceInput;
	ShalltearDepthSurface(input, isFrontFace, surfaceInput);
}
#elif defined(SHALLTEAR_DEPTH_VALUE)
uint PSMain(ShalltearVaryings input, bool isFrontFace : SV_IsFrontFace) : SV_Target0 {
	ShalltearSurfaceInput surfaceInput;
	ShalltearDepthSurface(input, isFrontFace, surfaceInput);
	return gDraw.value;
}
#else
float4 PSMain(ShalltearVaryings input, bool isFrontFace : SV_IsFrontFace) : SV_Target0 {
	ShalltearSurfaceInput surfaceInput;
	ShalltearSurface surface = ShalltearDepthSurface(input, isFrontFace, surfaceInput);

	float3 normalWS = ShalltearTransformSurfaceNormal(surfaceInput, surface.normal);
	return float4(normalWS * 0.5 + 0.5, 1.0);
}
#endif
