#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

#include "Color.hlsli"

[[vk::binding(0, 2)]] Texture2D sltSceneColor;
[[vk::binding(1, 2)]] SamplerState sltSceneSampler;
[[vk::binding(2, 2)]] Texture2D sltPassInput0;
[[vk::binding(3, 2)]] Texture2D sltPassInput1;

// must match PostProcessPush in PostProcessPass.cpp
struct ShalltearPostProcessPush {
	float4 colorFilterAndContrast;
	// x is hue shift in rads
	float4 hueSaturationAndFlags;
	float4 vignetteColorAndIntensity;
	float4 vignetteCenterSmoothnessRounded;
	// xy is step's extent in pixels, w is 2^stops
	float4 extentAndStep;
	float4 bloomTintAndIntensity;
	float4 bloomParams;
};

[[vk::push_constant]] ShalltearPostProcessPush gPost;

#define SHALLTEAR_POST_STEP_PREFILTER 0
#define SHALLTEAR_POST_STEP_DOWNSAMPLE 1
#define SHALLTEAR_POST_STEP_UPSAMPLE 2
#define SHALLTEAR_POST_STEP_COMPOSITE 3

#define SHALLTEAR_POST_CONTRAST_PIVOT 0.18

static const float3 SHALLTEAR_POST_LUMA = float3(0.2126, 0.7152, 0.0722);

struct VSOutput {
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	VSOutput output;
	float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
	output.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);
	output.uv = float2(corner.x, 1.0 - corner.y);
	return output;
}

float3 ShalltearPostHueShift(float3 color, float radians) {
	float c = cos(radians);
	float s = sin(radians);
	float3x3 m = float3x3(
		0.299 + 0.701 * c + 0.168 * s, 0.587 - 0.587 * c + 0.330 * s, 0.114 - 0.114 * c - 0.497 * s,
		0.299 - 0.299 * c - 0.328 * s, 0.587 + 0.413 * c + 0.035 * s, 0.114 - 0.114 * c + 0.292 * s,
		0.299 - 0.300 * c + 1.250 * s, 0.587 - 0.588 * c - 1.050 * s, 0.114 + 0.886 * c - 0.203 * s);
	return mul(m, color);
}

float2 ShalltearBloomSourceTexel(uint step) {
	float2 texel = 1.0 / max(gPost.extentAndStep.xy, 1.0);
	return step == SHALLTEAR_POST_STEP_UPSAMPLE || step == SHALLTEAR_POST_STEP_COMPOSITE
			   ? texel * 2.0
			   : texel * 0.5;
}

float3 ShalltearBloomDownsample(Texture2D source, float2 uv, float2 texel) {
	float3 a = source.Sample(sltSceneSampler, uv + texel * float2(-2.0, -2.0)).rgb;
	float3 b = source.Sample(sltSceneSampler, uv + texel * float2(0.0, -2.0)).rgb;
	float3 c = source.Sample(sltSceneSampler, uv + texel * float2(2.0, -2.0)).rgb;
	float3 d = source.Sample(sltSceneSampler, uv + texel * float2(-1.0, -1.0)).rgb;
	float3 e = source.Sample(sltSceneSampler, uv + texel * float2(1.0, -1.0)).rgb;
	float3 f = source.Sample(sltSceneSampler, uv + texel * float2(-2.0, 0.0)).rgb;
	float3 g = source.Sample(sltSceneSampler, uv).rgb;
	float3 h = source.Sample(sltSceneSampler, uv + texel * float2(2.0, 0.0)).rgb;
	float3 i = source.Sample(sltSceneSampler, uv + texel * float2(-1.0, 1.0)).rgb;
	float3 j = source.Sample(sltSceneSampler, uv + texel * float2(1.0, 1.0)).rgb;
	float3 k = source.Sample(sltSceneSampler, uv + texel * float2(-2.0, 2.0)).rgb;
	float3 l = source.Sample(sltSceneSampler, uv + texel * float2(0.0, 2.0)).rgb;
	float3 m = source.Sample(sltSceneSampler, uv + texel * float2(2.0, 2.0)).rgb;
	float3 result = (d + e + i + j) * 0.125;
	result += (a + b + g + f) * 0.03125;
	result += (b + c + h + g) * 0.03125;
	result += (f + g + l + k) * 0.03125;
	result += (g + h + m + l) * 0.03125;
	return result;
}

// 9 tap tent upsample. levels should show no square edges
float3 ShalltearBloomUpsample(Texture2D source, float2 uv, float2 texel) {
	float3 result = source.Sample(sltSceneSampler, uv + texel * float2(-1.0, -1.0)).rgb;
	result += source.Sample(sltSceneSampler, uv + texel * float2(0.0, -1.0)).rgb * 2.0;
	result += source.Sample(sltSceneSampler, uv + texel * float2(1.0, -1.0)).rgb;
	result += source.Sample(sltSceneSampler, uv + texel * float2(-1.0, 0.0)).rgb * 2.0;
	result += source.Sample(sltSceneSampler, uv).rgb * 4.0;
	result += source.Sample(sltSceneSampler, uv + texel * float2(1.0, 0.0)).rgb * 2.0;
	result += source.Sample(sltSceneSampler, uv + texel * float2(-1.0, 1.0)).rgb;
	result += source.Sample(sltSceneSampler, uv + texel * float2(0.0, 1.0)).rgb * 2.0;
	result += source.Sample(sltSceneSampler, uv + texel * float2(1.0, 1.0)).rgb;
	return result * 0.0625;
}

float3 ShalltearBloomThreshold(float3 color) {
	float threshold = gPost.bloomParams.x;
	float knee = gPost.bloomParams.y;
	// knee should not amplify a firefly
	color = min(color, gPost.bloomParams.w);
	float brightness = max(color.r, max(color.g, color.b));
	float soft = brightness - threshold + knee;
	soft = clamp(soft, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee + 1e-4);
	float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
	return color * contribution;
}

float4 PSMain(VSOutput input) : SV_Target {
	uint step = (uint)(gPost.extentAndStep.z + 0.5);
	float2 sourceTexel = ShalltearBloomSourceTexel(step);

	// steps shall return before grading runs
	if (step == SHALLTEAR_POST_STEP_PREFILTER) {
		return float4(ShalltearBloomThreshold(ShalltearBloomDownsample(sltSceneColor, input.uv, sourceTexel)), 1.0);
	}
	if (step == SHALLTEAR_POST_STEP_DOWNSAMPLE) {
		return float4(ShalltearBloomDownsample(sltPassInput0, input.uv, sourceTexel), 1.0);
	}
	if (step == SHALLTEAR_POST_STEP_UPSAMPLE) {
		float3 lower = ShalltearBloomUpsample(sltPassInput0, input.uv, sourceTexel);
		float3 here = sltPassInput1.Sample(sltSceneSampler, input.uv).rgb;
		return float4(lerp(here, lower, gPost.bloomParams.z), 1.0);
	}

	float3 color = sltSceneColor.Sample(sltSceneSampler, input.uv).rgb;

	// bloom added before grade (filters should tint it too)
	if (gPost.bloomTintAndIntensity.w > 0.0) {
		float3 bloom = ShalltearBloomUpsample(sltPassInput0, input.uv, sourceTexel);
		color += bloom * gPost.bloomTintAndIntensity.rgb * gPost.bloomTintAndIntensity.w;
	}

	if (gPost.hueSaturationAndFlags.z > 0.5) {
		// after bloom, before curve
		color *= gPost.extentAndStep.w;
		color = (color - SHALLTEAR_POST_CONTRAST_PIVOT) * gPost.colorFilterAndContrast.w + SHALLTEAR_POST_CONTRAST_PIVOT;
		color *= gPost.colorFilterAndContrast.rgb;
		if (abs(gPost.hueSaturationAndFlags.x) > 1e-6) {
			color = ShalltearPostHueShift(color, gPost.hueSaturationAndFlags.x);
		}
		float luma = dot(color, SHALLTEAR_POST_LUMA);
		color = luma + (color - luma) * gPost.hueSaturationAndFlags.y;
		color = max(color, 0.0);
	}

	if (gPost.hueSaturationAndFlags.w > 0.5) {
		float2 d = abs(input.uv - gPost.vignetteCenterSmoothnessRounded.xy) * gPost.vignetteColorAndIntensity.w;
		float aspect = gPost.extentAndStep.y > 0.0 ? gPost.extentAndStep.x / gPost.extentAndStep.y : 1.0;
		d.x *= lerp(1.0, aspect, gPost.vignetteCenterSmoothnessRounded.w);
		float factor = pow(saturate(1.0 - dot(d, d)), gPost.vignetteCenterSmoothnessRounded.z);
		color = lerp(gPost.vignetteColorAndIntensity.rgb, color, factor);
	}

	return float4(color, 1.0);
}
