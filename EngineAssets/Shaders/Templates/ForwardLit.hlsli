#include "Surface.hlsli"
#include "Brdf.hlsli"
#include "Lights.hlsli"
#include "Shadows.hlsli"
#include "Environment.hlsli"
#include "Fog.hlsli"

// after the library, before entry points
#include SHALLTEAR_SURFACE_FILE

#include "Vertex.hlsli"

// after the surface file (custom lighting in scope)
#include "Shading.hlsli"

float4 PSMain(ShalltearVaryings input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
	ShalltearSurfaceInput surfaceInput = ShalltearBuildPixelSurfaceInput(input, isFrontFace);
	const uint64_t drawAddress = ShalltearVaryingsDrawAddress(input);

	ShalltearSurface surface = ShalltearDefaultSurface();
	ShalltearSurfaceFunction(surfaceInput, surface);

#ifdef SHALLTEAR_ALPHA_CLIP
	clip(surface.alpha - ShalltearLoadAlphaClipThreshold(drawAddress));
#endif

	float3 normalWS = ShalltearTransformSurfaceNormal(surfaceInput, surface.normal);

	float receivesShadows = (ShalltearLoadDrawFlags(drawAddress) & SHALLTEAR_DRAW_RECEIVES_SHADOWS) != 0 ? 1.0 : 0.0;

	float4 color = float4(ShalltearShadeSurface(surfaceInput, surface, normalWS, receivesShadows) + surface.emissive, surface.alpha);
	uint blend = ShalltearLoadDrawBlend(drawAddress);
	return ShalltearExpose(ShalltearApplyFog(color, surfaceInput.positionWS, blend), blend);
}
