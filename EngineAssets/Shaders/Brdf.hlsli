#ifndef SHALLTEAR_BRDF_HLSLI
#define SHALLTEAR_BRDF_HLSLI

#include "Surface.hlsli"

#define SHALLTEAR_PI 3.14159265359

#define SHALLTEAR_MIN_PERCEPTUAL_ROUGHNESS 0.045

struct ShalltearLightSample {
	float3 direction;
	float3 color;
	float attenuation;
	float shadow;
};

float ShalltearD_GGX(float NoH, float roughness) {
	float oneMinusNoHSquared = 1.0 - NoH * NoH;
	float a = NoH * roughness;
	float k = roughness / (oneMinusNoHSquared + a * a);
	return k * k * (1.0 / SHALLTEAR_PI);
}

// includes 1/(4 NoL NoV). Specular term should never divide
float ShalltearV_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
	float a2 = roughness * roughness;
	float lambdaV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	float lambdaL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float3 ShalltearF_Schlick(float3 f0, float VoH) {
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (1.0 - f0) * f;
}

float ShalltearFd_Lambert() {
	return 1.0 / SHALLTEAR_PI;
}

float ShalltearLinearRoughness(float perceptualRoughness) {
	float clamped = max(perceptualRoughness, SHALLTEAR_MIN_PERCEPTUAL_ROUGHNESS);
	return clamped * clamped;
}

float3 ShalltearDiffuseColor(ShalltearSurface surface) {
	return surface.baseColor * (1.0 - surface.metallic);
}

float3 ShalltearF0(ShalltearSurface surface) {
	float dielectric = 0.16 * surface.reflectance * surface.reflectance;
	return lerp(float3(dielectric, dielectric, dielectric), surface.baseColor, surface.metallic);
}

float3 ShalltearStandardLighting(ShalltearSurface surface, float3 normalWS, ShalltearLightSample light, float3 viewDirectionWS) {
	float3 halfVector = normalize(viewDirectionWS + light.direction);

	float NoV = abs(dot(normalWS, viewDirectionWS)) + 1e-5;
	float NoL = saturate(dot(normalWS, light.direction));
	float NoH = saturate(dot(normalWS, halfVector));
	float VoH = saturate(dot(viewDirectionWS, halfVector));

	float roughness = ShalltearLinearRoughness(surface.roughness);
	float D = ShalltearD_GGX(NoH, roughness);
	float V = ShalltearV_SmithGGXCorrelated(NoV, NoL, roughness);
	float3 F = ShalltearF_Schlick(ShalltearF0(surface), VoH);

	float3 specular = (D * V) * F;
	float3 diffuse = ShalltearDiffuseColor(surface) * ShalltearFd_Lambert();

	return (diffuse + specular) * light.color * (light.attenuation * NoL);
}

#endif
