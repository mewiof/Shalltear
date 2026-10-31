// colors stay SRGB display encoded

#include "Color.hlsli"

#pragma shalltear_blend alpha
#pragma shalltear_depth off
#pragma shalltear_cull off

struct UIQuad {
	float4 rect;
	float4 uv;
	float4 color0;	 // SRGB, straight alpha
	float4 color1;
	float4 radii;
	float4 clipRect;
	float4 clipRadii;
	float softness;
	uint flags;
	float rotation; // rads clockwise about center (FLAG_ROTATED)
	float pad0;
};
#define UI_QUAD_STRIDE 128

#define UI_FLAG_GLYPH 1u
#define UI_FLAG_DITHER 2u
#define UI_FLAG_GRADIENT_VERTICAL 4u
#define UI_FLAG_SDF_FROM_UV 8u
#define UI_FLAG_PREMULTIPLIED 16u
#define UI_FLAG_RING 32u
#define UI_FLAG_ROTATED 64u

#define UI_TEXTURE_DECODES 1u

#define UI_PASS_COLOR 0u
#define UI_PASS_COVERAGE 1u
#define UI_PASS_BLUR_X 2u
#define UI_PASS_BLUR_Y 3u

struct UIPush {
	float2 viewportSize;
	float2 originPx;
	uint64_t quadAddress;
	uint textureFlags;
	uint passMode;
};
[[vk::push_constant]] UIPush gPush;

[[vk::binding(1, 1)]] Texture2D sltUITexture;
[[vk::binding(2, 1)]] SamplerState sltUISampler;

struct VSOutput {
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
	nointerpolation float4 rect : TEXCOORD1;
	nointerpolation float4 sdfBox : TEXCOORD2;
	nointerpolation float4 radii : TEXCOORD3;
	nointerpolation float4 clipRect : TEXCOORD4;
	nointerpolation float4 clipRadii : TEXCOORD5;
	nointerpolation float softness : TEXCOORD6;
	nointerpolation uint flags : TEXCOORD7;
	nointerpolation float4 innerBox : TEXCOORD8;
	nointerpolation float4 innerRadii : TEXCOORD9;
	nointerpolation float2 cosSin : TEXCOORD10;
};

VSOutput VSMain(uint vertexId : SV_VertexID) {
	uint quadIndex = vertexId / 6u;
	uint cornerIndex = vertexId - quadIndex * 6u;

	UIQuad quad = vk::RawBufferLoad<UIQuad>(gPush.quadAddress + uint64_t(quadIndex) * UI_QUAD_STRIDE, 4);

	float2 corner = float2(0.0, 0.0);
	if (cornerIndex == 1u) {
		corner = float2(1.0, 0.0);
	} else if (cornerIndex == 2u || cornerIndex == 4u) {
		corner = float2(1.0, 1.0);
	} else if (cornerIndex == 5u) {
		corner = float2(0.0, 1.0);
	}
	if (cornerIndex == 3u) {
		corner = float2(0.0, 0.0);
	}

	float2 cosSin = float2(1.0, 0.0);
	float2 positionPx = quad.rect.xy + corner * quad.rect.zw;
	if ((quad.flags & UI_FLAG_ROTATED) != 0u) {
		sincos(quad.rotation, cosSin.y, cosSin.x);
		float2 centre = quad.rect.xy + quad.rect.zw * 0.5;
		float2 offset = (corner - 0.5) * quad.rect.zw;
		positionPx = centre + float2(offset.x * cosSin.x - offset.y * cosSin.y,
									 offset.x * cosSin.y + offset.y * cosSin.x);
	}

	VSOutput output;
	float2 local = positionPx - gPush.originPx;
	output.positionCS = float4(
		2.0 * local.x / gPush.viewportSize.x - 1.0,
		1.0 - 2.0 * local.y / gPush.viewportSize.y,
		0.0, 1.0);
	output.uv = quad.uv.xy + corner * quad.uv.zw;
	float4 rampEnd = ((quad.flags & UI_FLAG_RING) != 0u) ? quad.color0 : quad.color1;
	output.color = ((quad.flags & UI_FLAG_GRADIENT_VERTICAL) != 0u)
					   ? lerp(quad.color0, rampEnd, corner.y)
					   : lerp(quad.color0, rampEnd, corner.x);
	output.rect = quad.rect;
	output.sdfBox = ((quad.flags & UI_FLAG_SDF_FROM_UV) != 0u) ? quad.uv : quad.rect;
	output.radii = quad.radii;
	output.clipRect = quad.clipRect;
	output.clipRadii = quad.clipRadii;
	output.softness = quad.softness;
	output.flags = quad.flags;
	output.innerBox = quad.uv;
	output.innerRadii = quad.color1;
	output.cosSin = cosSin;
	return output;
}

float4 ClampRadii(float4 radii, float2 size) {
	float w = max(size.x, 0.0);
	float h = max(size.y, 0.0);
	float top = radii.x + radii.y;
	float right = radii.y + radii.z;
	float bottom = radii.z + radii.w;
	float left = radii.w + radii.x;
	float scale = min(min(top > 0.0 ? w / top : 1.0, bottom > 0.0 ? w / bottom : 1.0),
					  min(right > 0.0 ? h / right : 1.0, left > 0.0 ? h / left : 1.0));
	return radii * min(scale, 1.0);
}

float RoundedBoxSDF(float2 p, float4 box, float4 radii, bool turned, float2 cosSin, out float support) {
	radii = ClampRadii(radii, box.zw);
	float2 center = box.xy + box.zw * 0.5;
	float2 fromCenter = p - center;
	float radius = (fromCenter.x <= 0.0)
					   ? ((fromCenter.y <= 0.0) ? radii.x : radii.w)
					   : ((fromCenter.y <= 0.0) ? radii.y : radii.z);
	float2 q = abs(fromCenter) - box.zw * 0.5 + radius;
	float2 outward = max(q, 0.0);
	float radial = length(outward);
	if (turned) {
		float2 screen = outward * sign(fromCenter);
		screen = float2(screen.x * cosSin.x - screen.y * cosSin.y,
						screen.x * cosSin.y + screen.y * cosSin.x);
		support = (radial > 0.0) ? (abs(screen.x) + abs(screen.y)) / radial : 1.0;
	} else {
		support = (radial > 0.0) ? (outward.x + outward.y) / radial : 1.0;
	}
	return min(max(q.x, q.y), 0.0) + radial - radius;
}

float RoundedBoxSDF(float2 p, float4 box, float4 radii) {
	float ignored;
	return RoundedBoxSDF(p, box, radii, false, float2(1.0, 0.0), ignored);
}

float BoxCoverage(float dist, float support) {
	float half = 0.5 * support;
	if (dist <= -half) {
		return 1.0;
	}
	if (dist >= half) {
		return 0.0;
	}
	float spread = sqrt(max(2.0 - support * support, 0.0));
	float hi = 0.5 * (support + spread);
	float lo = support - hi;
	if (abs(dist) <= 0.5 * spread) {
		return 0.5 - dist / hi;
	}
	float tail = half - abs(dist);
	float area = tail * tail / max(2.0 * hi * lo, 1e-6);
	return dist > 0.0 ? area : 1.0 - area;
}

float Coverage(float dist, float support, float softness) {
	if (softness > 0.0) {
		float halfWidth = 0.5 * (1.0 + softness);
		return 1.0 - smoothstep(-halfWidth, halfWidth, dist);
	}
	return BoxCoverage(dist, max(support, 1.0));
}

float ClippedCoverage(float dist, float support, float clipDist, float clipSupport,
					  float clipMask, float softness) {
	if (softness > 0.0) {
		return Coverage(dist, support, softness) * clipMask;
	}
	bool shapeWins = dist >= clipDist;
	return Coverage(shapeWins ? dist : clipDist, shapeWins ? support : clipSupport, 0.0);
}

float DitherNoise(float2 p) {
	return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453123);
}

float ShadowBlur(VSOutput input, float2 axis) {
	const float halfWidth = 0.5 * (1.0 + input.softness);
	const int reach = (int)ceil(halfWidth);
	const float2 low = input.rect.xy + 0.5;
	const float2 high = input.rect.xy + input.rect.zw - 0.5;
	const float2 centre = floor(input.positionCS.xy) + 0.5;
	float sum = 0.0;
	for (int tap = -reach; tap <= reach; ++tap) {
		const float weight = smoothstep(-halfWidth, halfWidth, tap + 0.5) - smoothstep(-halfWidth, halfWidth, tap - 0.5);
		const float2 at = clamp(centre + axis * tap, low, high);
		sum += weight * sltUITexture.SampleLevel(sltUISampler, at / gPush.viewportSize, 0).r;
	}
	return sum;
}

float4 PSMain(VSOutput input) : SV_Target {
	if (gPush.passMode == UI_PASS_BLUR_X || gPush.passMode == UI_PASS_BLUR_Y) {
		const float blurred = ShadowBlur(input, gPush.passMode == UI_PASS_BLUR_X ? float2(1.0, 0.0) : float2(0.0, 1.0));
		if (blurred <= 0.0) {
			discard;
		}
		return float4(1.0, 1.0, 1.0, blurred);
	}

	float2 contextPos = input.positionCS.xy + gPush.originPx;

	float clipSupport;
	float clipDist = RoundedBoxSDF(contextPos, input.clipRect, input.clipRadii, false, float2(1.0, 0.0), clipSupport);
	float clipMask = Coverage(clipDist, clipSupport, 0.0);
	if (clipMask <= 0.0) {
		discard;
	}

	const bool rotated = (input.flags & UI_FLAG_ROTATED) != 0u;
	float2 shapePos = contextPos;
	if (rotated) {
		float2 centre = input.rect.xy + input.rect.zw * 0.5;
		float2 d = contextPos - centre;
		shapePos = centre + float2(d.x * input.cosSin.x + d.y * input.cosSin.y,
								   -d.x * input.cosSin.y + d.y * input.cosSin.x);
	}

	float4 color = input.color;
	float4 texel = sltUITexture.Sample(sltUISampler, input.uv);
	if ((gPush.textureFlags & UI_TEXTURE_DECODES) != 0u) {
		texel.rgb = ShalltearLinearToSrgb(texel.rgb);
	}

	if ((input.flags & UI_FLAG_GLYPH) != 0u) {
		color.a *= texel.r * clipMask;
	} else {
		if ((input.flags & UI_FLAG_PREMULTIPLIED) != 0u && texel.a > 0.0) {
			texel.rgb /= texel.a;
		}
		color *= texel;
		float support;
		float dist = RoundedBoxSDF(shapePos, input.sdfBox, input.radii, rotated, input.cosSin, support);
		float coverage = ClippedCoverage(dist, support, clipDist, clipSupport, clipMask, input.softness);
		if ((input.flags & UI_FLAG_RING) != 0u && input.innerBox.z > 0.0 && input.innerBox.w > 0.0) {
			float innerSupport;
			float innerDist = RoundedBoxSDF(shapePos, input.innerBox, input.innerRadii, rotated, input.cosSin, innerSupport);
			coverage = max(coverage - ClippedCoverage(innerDist, innerSupport, clipDist, clipSupport, clipMask, input.softness), 0.0);
		}
		color.a *= coverage;
	}

	if ((input.flags & UI_FLAG_DITHER) != 0u) {
		color.rgb += (DitherNoise(contextPos) - 0.5) * (1.0 / 255.0);
	}

	if (color.a <= 0.0) {
		discard;
	}
	if (gPush.passMode == UI_PASS_COVERAGE) {
		return float4(1.0, 1.0, 1.0, color.a);
	}
	return color;
}
