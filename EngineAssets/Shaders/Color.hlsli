#ifndef SHALLTEAR_COLOR_HLSLI
#define SHALLTEAR_COLOR_HLSLI

// SRGB curve because UI colors round trip through
float3 ShalltearLinearToSrgb(float3 c) {
	c = saturate(c);
	return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

// later encode should leave them unchanged
float3 ShalltearSrgbToLinear(float3 c) {
	c = saturate(c);
	return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

// must match TonemapCurve in GraphicsProfile.h
#define SHALLTEAR_TONEMAP_NONE 0
#define SHALLTEAR_TONEMAP_NEUTRAL 1
#define SHALLTEAR_TONEMAP_ACES 2
#define SHALLTEAR_TONEMAP_AGX 3

// Khronos PBR Neutral curve/new project default
float3 ShalltearTonemapNeutral(float3 color) {
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) {
		return color;
	}

	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return lerp(color, newPeak.xxx, g);
}

// ACES through Stephen Hill's fit of RRT/SRGB ODT
static const float3x3 SHALLTEAR_ACES_INPUT = float3x3(
	0.59719, 0.35458, 0.04823,
	0.07600, 0.90834, 0.01566,
	0.02840, 0.13383, 0.83777);

static const float3x3 SHALLTEAR_ACES_OUTPUT = float3x3(
	1.60475, -0.53108, -0.07367,
	-0.10208, 1.10813, -0.00605,
	-0.00327, -0.07276, 1.07602);

float3 ShalltearTonemapAces(float3 color) {
	color = mul(SHALLTEAR_ACES_INPUT, color);
	float3 a = color * (color + 0.0245786) - 0.000090537;
	float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	return saturate(mul(SHALLTEAR_ACES_OUTPUT, a / b));
}

static const float3x3 SHALLTEAR_AGX_INSET = float3x3(
	0.842479062253094, 0.0784335999999992, 0.0792237451477643,
	0.0423282422610123, 0.878468636469772, 0.0791661274605434,
	0.0423756549057051, 0.0784336, 0.879142973793104);

static const float3x3 SHALLTEAR_AGX_OUTSET = float3x3(
	1.19687900512017, -0.0980208811401368, -0.0990297440797205,
	-0.0528968517574562, 1.15190312990417, -0.0989611768448433,
	-0.0529716355144438, -0.0980434501171241, 1.15107367264116);

float3 ShalltearTonemapAgx(float3 color) {
	const float minEv = -12.47393;
	const float maxEv = 4.026069;

	color = mul(SHALLTEAR_AGX_INSET, max(color, 0.0));
	color = clamp(log2(max(color, 1e-10)), minEv, maxEv);
	color = (color - minEv) / (maxEv - minEv);

	float3 x2 = color * color;
	float3 x4 = x2 * x2;
	color = 15.5 * x4 * x2 - 40.14 * x4 * color + 31.96 * x4 - 6.868 * x2 * color + 0.4298 * x2 + 0.1191 * color - 0.00232;

	color = mul(SHALLTEAR_AGX_OUTSET, color);
	return saturate(pow(max(color, 0.0), 2.2));
}

float3 ShalltearTonemap(uint curve, float3 color) {
	if (curve == SHALLTEAR_TONEMAP_NEUTRAL) {
		return ShalltearTonemapNeutral(color);
	}
	if (curve == SHALLTEAR_TONEMAP_ACES) {
		return ShalltearTonemapAces(color);
	}
	if (curve == SHALLTEAR_TONEMAP_AGX) {
		return ShalltearTonemapAgx(color);
	}
	return color;
}

#endif
