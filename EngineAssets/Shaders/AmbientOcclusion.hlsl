#pragma shalltear_blend opaque
#pragma shalltear_depth off
#pragma shalltear_cull off

[[vk::binding(0, 2)]] Texture2D<float> sltSceneDepth;
[[vk::binding(1, 2)]] SamplerState sltSceneSampler;
[[vk::binding(2, 2)]] Texture2D sltSceneNormals;
[[vk::binding(3, 2)]] Texture2D<float> sltPassInput0;
// present only when the row asked for afterOpaque
[[vk::binding(4, 2)]] Texture2D sltSceneColor;

// must match AmbientOcclusionPush in AmbientOcclusionPass.cpp. float for float
struct ShalltearAmbientOcclusionPush {
	float4x4 invViewProjection;
	float4 projHalfAndLinearize;
	float4 radiusFalloffIntensityBias;
	// xy is this step's extent in pixels
	float4 extentStepDirections;
};

[[vk::push_constant]] ShalltearAmbientOcclusionPush gAO;

#define SHALLTEAR_AO_STEP_ESTIMATE 0
#define SHALLTEAR_AO_STEP_BLUR_X 1
#define SHALLTEAR_AO_STEP_BLUR_Y 2
#define SHALLTEAR_AO_STEP_COMPOSITE 3
#define SHALLTEAR_AO_STEP_UPSAMPLE 4

#define SHALLTEAR_AO_STEPS 4

// blur's half width in texels
#define SHALLTEAR_AO_BLUR_RADIUS 3

#define SHALLTEAR_AO_DEPTH_SHARPNESS 24.0

#define SHALLTEAR_AO_UPSAMPLE_SHARPNESS 24.0

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

// inverse of the vertex stage's flip
float2 ShalltearAOClipFromUV(float2 uv) {
	return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float2 ShalltearAOSnapToDepthTexel(float2 uv) {
	uint width, height;
	sltSceneDepth.GetDimensions(width, height);
	float2 extent = float2(width, height);
	return (floor(uv * extent) + 0.5) / extent;
}

// positive, in world units
float ShalltearAOViewDepth(float rawDepth) {
	return gAO.projHalfAndLinearize.z / max(rawDepth + gAO.projHalfAndLinearize.w, 1.0e-6);
}

float3 ShalltearAOWorldPosition(float2 uv, float rawDepth) {
	float4 clip = mul(gAO.invViewProjection, float4(ShalltearAOClipFromUV(uv), rawDepth, 1.0));
	return clip.xyz / clip.w;
}

float ShalltearAODither(float2 positionSS) {
	return frac(52.9829189 * frac(dot(positionSS, float2(0.06711056, 0.00583715))));
}

float ShalltearAOEstimate(float2 uv, float2 positionSS) {
	uv = ShalltearAOSnapToDepthTexel(uv);
	float rawDepth = sltSceneDepth.SampleLevel(sltSceneSampler, uv, 0);
	if (rawDepth <= 0.0) {
		return 1.0;
	}
	float viewDepth = ShalltearAOViewDepth(rawDepth);
	if (viewDepth > gAO.radiusFalloffIntensityBias.y) {
		return 1.0;
	}

	float3 positionWS = ShalltearAOWorldPosition(uv, rawDepth);
	float3 normalWS = normalize(sltSceneNormals.SampleLevel(sltSceneSampler, uv, 0).xyz * 2.0 - 1.0);

	float2 radiusUV = gAO.radiusFalloffIntensityBias.x * gAO.projHalfAndLinearize.xy / viewDepth;

	float directions = gAO.extentStepDirections.w;
	float rotation = ShalltearAODither(positionSS) * 6.2831853;
	float bias = gAO.radiusFalloffIntensityBias.w;
	float occlusion = 0.0;

	for (float d = 0.0; d < directions; d += 1.0) {
		float angle = rotation + d * (6.2831853 / directions);
		float2 direction = float2(cos(angle), sin(angle));
		float horizon = bias;
		for (int s = 1; s <= SHALLTEAR_AO_STEPS; ++s) {
			float2 sampleUV = ShalltearAOSnapToDepthTexel(uv + direction * radiusUV * (float(s) / float(SHALLTEAR_AO_STEPS)));
			float sampleRaw = sltSceneDepth.SampleLevel(sltSceneSampler, sampleUV, 0);
			if (sampleRaw <= 0.0) {
				continue;
			}
			float3 samplePosition = ShalltearAOWorldPosition(sampleUV, sampleRaw);
			float3 toSample = samplePosition - positionWS;
			float distance = length(toSample);
			if (distance < 1.0e-5) {
				continue;
			}
			float height = dot(normalWS, toSample);
			float elevation = height / distance;
			float3 tangential = toSample - normalWS * height;
			float reach = length(tangential);
			float falloff = saturate(1.0 - (reach * reach) / max(gAO.radiusFalloffIntensityBias.x * gAO.radiusFalloffIntensityBias.x, 1.0e-6));
			horizon = max(horizon, elevation * falloff);
		}
		occlusion += horizon - bias;
	}

	occlusion = saturate(occlusion / max(directions, 1.0) * gAO.radiusFalloffIntensityBias.z);
	return 1.0 - occlusion;
}

float ShalltearAOBlur(float2 uv, float2 axis) {
	float centerRaw = sltSceneDepth.SampleLevel(sltSceneSampler, ShalltearAOSnapToDepthTexel(uv), 0);
	float centerDepth = ShalltearAOViewDepth(centerRaw);
	float2 texel = axis / gAO.extentStepDirections.xy;

	float total = 0.0;
	float weightSum = 0.0;
	for (int i = -SHALLTEAR_AO_BLUR_RADIUS; i <= SHALLTEAR_AO_BLUR_RADIUS; ++i) {
		float2 sampleUV = uv + texel * float(i);
		float sampleRaw = sltSceneDepth.SampleLevel(sltSceneSampler, ShalltearAOSnapToDepthTexel(sampleUV), 0);
		float spatial = exp(-float(i * i) / (2.0 * float(SHALLTEAR_AO_BLUR_RADIUS) * float(SHALLTEAR_AO_BLUR_RADIUS)));
		float depthDelta = (ShalltearAOViewDepth(sampleRaw) - centerDepth) / max(centerDepth, 1.0e-4) * SHALLTEAR_AO_DEPTH_SHARPNESS;
		float weight = spatial * exp(-depthDelta * depthDelta);
		total += sltPassInput0.SampleLevel(sltSceneSampler, sampleUV, 0) * weight;
		weightSum += weight;
	}
	return total / max(weightSum, 1.0e-6);
}

float ShalltearAOUpsample(float2 uv) {
	uint sourceWidth, sourceHeight;
	sltPassInput0.GetDimensions(sourceWidth, sourceHeight);
	float2 sourceExtent = float2(sourceWidth, sourceHeight);

	float centerDepth = ShalltearAOViewDepth(sltSceneDepth.SampleLevel(sltSceneSampler, ShalltearAOSnapToDepthTexel(uv), 0));

	// 4 source texels UV falls between
	float2 coord = uv * sourceExtent - 0.5;
	float2 base = floor(coord);
	float2 frac2 = coord - base;

	float total = 0.0;
	float weightSum = 0.0;
	for (int j = 0; j <= 1; ++j) {
		for (int i = 0; i <= 1; ++i) {
			float2 texel = base + float2(float(i), float(j));
			float2 sourceUV = (texel + 0.5) / sourceExtent;
			float bilinear = (i == 0 ? 1.0 - frac2.x : frac2.x) * (j == 0 ? 1.0 - frac2.y : frac2.y);
			float sampleDepth =
				ShalltearAOViewDepth(sltSceneDepth.SampleLevel(sltSceneSampler, ShalltearAOSnapToDepthTexel(sourceUV), 0));
			// one filter applies at every depth
			float delta = (sampleDepth - centerDepth) / max(centerDepth, 1.0e-4) * SHALLTEAR_AO_UPSAMPLE_SHARPNESS;
			float weight = bilinear * exp(-delta * delta) + 1.0e-4;
			total += sltPassInput0.SampleLevel(sltSceneSampler, sourceUV, 0) * weight;
			weightSum += weight;
		}
	}
	return total / weightSum;
}

float4 PSMain(VSOutput input) : SV_Target {
	uint step = (uint)gAO.extentStepDirections.z;
	if (step == SHALLTEAR_AO_STEP_UPSAMPLE) {
		float resolved = ShalltearAOUpsample(input.uv);
		return float4(resolved, resolved, resolved, 1.0);
	}
	if (step == SHALLTEAR_AO_STEP_COMPOSITE) {
		float occluded = sltPassInput0.SampleLevel(sltSceneSampler, input.uv, 0);
		float3 scene = sltSceneColor.SampleLevel(sltSceneSampler, input.uv, 0).rgb;
		return float4(scene * occluded, 1.0);
	}

	float occlusion;
	if (step == SHALLTEAR_AO_STEP_ESTIMATE) {
		occlusion = ShalltearAOEstimate(input.uv, input.positionCS.xy);
	} else if (step == SHALLTEAR_AO_STEP_BLUR_X) {
		occlusion = ShalltearAOBlur(input.uv, float2(1.0, 0.0));
	} else {
		occlusion = ShalltearAOBlur(input.uv, float2(0.0, 1.0));
	}
	return float4(occlusion, occlusion, occlusion, 1.0);
}
