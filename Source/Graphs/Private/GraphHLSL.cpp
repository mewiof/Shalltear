#include "GraphHLSL.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory_resource>
#include <queue>
#include <unordered_map>

namespace Shalltear {

	namespace {

		constexpr std::string_view HASH_TCHOU = R"(uint ShalltearGraphHashTchou21(uint2 v)
{
	v.y ^= 1103515245u;
	v.x += v.y;
	v.x *= v.y;
	v.x ^= v.x >> 5u;
	v.x *= 0x27d4eb2du;
	return v.x;
}

uint2 ShalltearGraphHashTchou22(uint2 v)
{
	v.y ^= 1103515245u;
	v.x += v.y;
	v.x *= v.y;
	v.x ^= v.x >> 5u;
	v.x *= 0x27d4eb2du;
	v.y ^= (v.x << 3u);
	return v;
}

float ShalltearGraphHash21Deterministic(float2 i)
{
	uint2 v = (uint2)(int2)round(i);
	return (ShalltearGraphHashTchou21(v) >> 8) * (1.0 / float(0x00ffffff));
}

float2 ShalltearGraphHash22Deterministic(float2 i)
{
	uint2 v = (uint2)(int2)round(i);
	return (ShalltearGraphHashTchou22(v) >> 8) * (1.0 / float(0x00ffffff));
}

)";

		constexpr std::string_view HASH_LEGACY_SINE = R"(float ShalltearGraphHash21LegacySine(float2 i)
{
	float angle = dot(i, float2(12.9898, 78.233));
	return frac(sin(angle) * 43758.5453);
}

float2 ShalltearGraphHash22LegacySine(float2 i)
{
	float2x2 m = float2x2(15.27, 47.63, 99.41, 89.98);
	return frac(sin(mul(i, m)));
}

)";

		constexpr std::string_view HASH_LEGACY_MOD = R"(float ShalltearGraphHash21LegacyMod(float2 i)
{
	i = i % 289;
	float x = float(34 * i.x + 1) * i.x % 289 + i.y;
	x = (34 * x + 1) * x % 289;
	return frac(x / 41) * 2 - 1;
}

)";

		constexpr std::string_view VALUE_NOISE = R"(float ShalltearGraphValueNoise%HASH%(float2 uv)
{
	float2 i = floor(uv);
	float2 f = frac(uv);
	f = f * f * (3.0 - 2.0 * f);
	float r0 = ShalltearGraphHash21%HASH%(i + float2(0.0, 0.0));
	float r1 = ShalltearGraphHash21%HASH%(i + float2(1.0, 0.0));
	float r2 = ShalltearGraphHash21%HASH%(i + float2(0.0, 1.0));
	float r3 = ShalltearGraphHash21%HASH%(i + float2(1.0, 1.0));
	float bottomOfGrid = lerp(r0, r1, f.x);
	float topOfGrid = lerp(r2, r3, f.x);
	return lerp(bottomOfGrid, topOfGrid, f.y);
}

)";

		constexpr std::string_view GRADIENT_NOISE = R"(float2 ShalltearGraphGradientDirection%HASH%(float2 p)
{
	float x = ShalltearGraphHash21%HASH%(p);
	return normalize(float2(x - floor(x + 0.5), abs(x) - 0.5));
}

float ShalltearGraphGradientNoise%HASH%(float2 p)
{
	float2 ip = floor(p);
	float2 fp = frac(p);
	float d00 = dot(ShalltearGraphGradientDirection%HASH%(ip), fp);
	float d01 = dot(ShalltearGraphGradientDirection%HASH%(ip + float2(0, 1)), fp - float2(0, 1));
	float d10 = dot(ShalltearGraphGradientDirection%HASH%(ip + float2(1, 0)), fp - float2(1, 0));
	float d11 = dot(ShalltearGraphGradientDirection%HASH%(ip + float2(1, 1)), fp - float2(1, 1));
	fp = fp * fp * fp * (fp * (fp * 6 - 15) + 10);
	return lerp(lerp(d00, d01, fp.y), lerp(d10, d11, fp.y), fp.x) + 0.5;
}

)";

		constexpr std::string_view VORONOI = R"(float2 ShalltearGraphVoronoiRandomVector%HASH%(float2 uv, float offset)
{
	uv = ShalltearGraphHash22%HASH%(uv);
	return float2(sin(uv.y * offset), cos(uv.x * offset)) * 0.5 + 0.5;
}

float2 ShalltearGraphVoronoi%HASH%(float2 uv, float angleOffset, float cellDensity)
{
	float2 g = floor(uv * cellDensity);
	float2 f = frac(uv * cellDensity);
	float3 res = float3(8.0, 0.0, 0.0);
	for (int y = -1; y <= 1; y++)
	{
		for (int x = -1; x <= 1; x++)
		{
			float2 lattice = float2(x, y);
			float2 offset = ShalltearGraphVoronoiRandomVector%HASH%(lattice + g, angleOffset);
			float d = distance(lattice + offset, f);
			if (d < res.x)
			{
				res = float3(d, offset.x, offset.y);
			}
		}
	}
	return res.xy;
}

)";

		constexpr std::string_view TEXTURE_SIZE = R"(float4 ShalltearGraphTextureSize(Texture2D source)
{
	uint width;
	uint height;
	source.GetDimensions(width, height);
	return float4(width, height, 1.0 / float(width), 1.0 / float(height));
}

float4 ShalltearGraphTextureSize(Texture3D source)
{
	uint width;
	uint height;
	uint depth;
	source.GetDimensions(width, height, depth);
	return float4(width, height, depth, 0.0);
}

)";

		constexpr std::string_view RANDOM = R"(uint ShalltearGraphRandomHash(uint h)
{
	h = ((h >> 16) ^ h) * 0x45d9f3bu;
	h = ((h >> 16) ^ h) * 0x45d9f3bu;
	return (h >> 16) ^ h;
}

float ShalltearGraphRandom(float key, float seed, uint salt)
{
	uint h = ShalltearGraphRandomHash(asuint(key) ^ ShalltearGraphRandomHash(asuint(seed) + salt * 0x9e3779b9u));
	return asfloat(0x3f800000u | (h >> 9)) - 1.0;
}

)";

		constexpr std::string_view PARALLAX_OCCLUSION = R"(float3 ShalltearGraphParallaxOcclusion(Texture2D heightMap, SamplerState heightSampler, float2 uv, float3 viewDirUV, float3 parameters, uint channel)
{
	float lod = parameters.x;
	float lodThreshold = parameters.y;
	int numSteps = (int)parameters.z;
	float stepSize = 1.0 / (float)numSteps;
	float2 texOffsetPerStep = stepSize * (viewDirUV.xy / -viewDirUV.z);
	float2 texOffsetCurrent = float2(0.0, 0.0);
	float prevHeight = heightMap.SampleLevel(heightSampler, uv + texOffsetCurrent, lod)[channel];
	texOffsetCurrent += texOffsetPerStep;
	float currHeight = heightMap.SampleLevel(heightSampler, uv + texOffsetCurrent, lod)[channel];
	float rayHeight = 1.0 - stepSize;
	for (int stepIndex = 0; stepIndex < numSteps; ++stepIndex)
	{
		if (currHeight > rayHeight)
		{
			break;
		}
		prevHeight = currHeight;
		rayHeight -= stepSize;
		texOffsetCurrent += texOffsetPerStep;
		currHeight = heightMap.SampleLevel(heightSampler, uv + texOffsetCurrent, lod)[channel];
	}
	float pt0 = rayHeight + stepSize;
	float pt1 = rayHeight;
	float delta0 = pt0 - prevHeight;
	float delta1 = pt1 - currHeight;
	float2 offset = float2(0.0, 0.0);
	for (int i = 0; i < 3; ++i)
	{
		float intersectionHeight = (pt0 * delta1 - pt1 * delta0) / (delta1 - delta0);
		offset = (1.0 - intersectionHeight) * texOffsetPerStep * numSteps;
		currHeight = heightMap.SampleLevel(heightSampler, uv + offset, lod)[channel];
		float delta = intersectionHeight - currHeight;
		if ((delta >= -0.01) && (delta <= 0.01))
		{
			break;
		}
		if (delta < 0.0)
		{
			delta1 = delta;
			pt1 = intersectionHeight;
		}
		else
		{
			delta0 = delta;
			pt0 = intersectionHeight;
		}
	}
	offset *= 1.0 - saturate(lod - lodThreshold);
	return float3(offset, currHeight);
}

)";

		// the effect noise is Brian Sharpe's GPU-Noise-Lib (github.com/BrianSharpe/GPU-Noise-Lib)
		constexpr std::string_view EFFECT_NOISE_COMMON = R"(float3 ShalltearGraphNoiseC2(float3 x) { return x * x * x * (x * (x * 6.0 - 15.0) + 10.0); }
float4 ShalltearGraphNoiseC2Pair(float2 x) { return x.xyxy * x.xyxy * (x.xyxy * (x.xyxy * (x.xyxy * float2(6.0, 0.0).xxyy + float2(-15.0, 30.0).xxyy) + float2(10.0, -60.0).xxyy) + float2(0.0, 30.0).xxyy); }
float2 ShalltearGraphNoiseC2Pair(float x) { return x * x * (x * (x * (x * float2(6.0, 0.0) + float2(-15.0, 30.0)) + float2(10.0, -60.0)) + float2(0.0, 30.0)); }
float3 ShalltearGraphNoiseC2Derivative(float3 x) { return x * x * (x * (x * 30.0 - 60.0) + 30.0); }

float2 ShalltearGraphNoiseFraction(float2 x) { precise float2 fraction = saturate(x - floor(x)); return fraction; }
float4 ShalltearGraphNoiseFraction(float4 x) { precise float4 fraction = saturate(x - floor(x)); return fraction; }

float2 ShalltearGraphNoiseHash1D(float cell)
{
	precise float2 p = float2(cell, cell + 1.0);
	p = p - floor(p * (1.0 / 71.0)) * 71.0;
	precise float3 p3 = float3(p.x, 0.0, p.y) + float2(26.0, 161.0).xyx;
	p3 *= p3;
	precise float2 product = p3.xz * p3.y * (1.0 / 951.135664);
	return ShalltearGraphNoiseFraction(product);
}

float4 ShalltearGraphNoiseHash2D(float2 cell)
{
	precise float4 p = float4(cell.xy, cell.xy + 1.0);
	p = p - floor(p * (1.0 / 71.0)) * 71.0;
	p += float2(26.0, 161.0).xyxy;
	p *= p;
	precise float4 product = p.xzxz * p.yyww * (1.0 / 951.135664);
	return ShalltearGraphNoiseFraction(product);
}

void ShalltearGraphNoiseHash2D(float2 cell, out float4 hash0, out float4 hash1)
{
	precise float4 p = float4(cell.xy, cell.xy + 1.0);
	p = p - floor(p * (1.0 / 71.0)) * 71.0;
	p += float2(26.0, 161.0).xyxy;
	p *= p;
	p = p.xzxz * p.yyww;
	precise float4 product0 = p * (1.0 / 951.135664);
	precise float4 product1 = p * (1.0 / 642.949883);
	hash0 = ShalltearGraphNoiseFraction(product0);
	hash1 = ShalltearGraphNoiseFraction(product1);
}

void ShalltearGraphNoiseHash3D(float3 cell, out float4 lowz, out float4 highz)
{
	precise float3 wrapped = cell - floor(cell * (1.0 / 69.0)) * 69.0;
	precise float3 cell1 = step(wrapped, (float3)(69.0 - 1.5)) * (wrapped + 1.0);
	precise float4 p = float4(wrapped.xy, cell1.xy) + float2(50.0, 161.0).xyxy;
	p *= p;
	p = p.xzxz * p.yyww;
	precise float2 modulus = float2(1.0 / (635.298681 + float2(wrapped.z, cell1.z) * 48.500388));
	precise float4 low = p * modulus.xxxx;
	precise float4 high = p * modulus.yyyy;
	lowz = ShalltearGraphNoiseFraction(low);
	highz = ShalltearGraphNoiseFraction(high);
}

void ShalltearGraphNoiseHash3D(float3 cell, out float4 lowz0, out float4 lowz1, out float4 lowz2, out float4 highz0, out float4 highz1, out float4 highz2)
{
	precise float3 wrapped = cell - floor(cell * (1.0 / 69.0)) * 69.0;
	precise float3 cell1 = step(wrapped, (float3)(69.0 - 1.5)) * (wrapped + 1.0);
	precise float4 p = float4(wrapped.xy, cell1.xy) + float2(50.0, 161.0).xyxy;
	p *= p;
	p = p.xzxz * p.yyww;
	precise float3 lowMod = float3(1.0 / (float3(635.298681, 682.357502, 668.926525) + wrapped.zzz * float3(48.500388, 65.294118, 63.934599)));
	precise float3 highMod = float3(1.0 / (float3(635.298681, 682.357502, 668.926525) + cell1.zzz * float3(48.500388, 65.294118, 63.934599)));
	precise float4 low0 = p * lowMod.xxxx;
	precise float4 high0 = p * highMod.xxxx;
	precise float4 low1 = p * lowMod.yyyy;
	precise float4 high1 = p * highMod.yyyy;
	precise float4 low2 = p * lowMod.zzzz;
	precise float4 high2 = p * highMod.zzzz;
	lowz0 = ShalltearGraphNoiseFraction(low0);
	highz0 = ShalltearGraphNoiseFraction(high0);
	lowz1 = ShalltearGraphNoiseFraction(low1);
	highz1 = ShalltearGraphNoiseFraction(high1);
	lowz2 = ShalltearGraphNoiseFraction(low2);
	highz2 = ShalltearGraphNoiseFraction(high2);
}

float4 ShalltearGraphNoiseCellularWeight(float4 samples)
{
	samples = samples * 2.0 - 1.0;
	return (samples * samples * samples) - sign(samples);
}

)";

		constexpr std::string_view EFFECT_NOISE_BASES[3][3] = {
			{R"(float2 ShalltearGraphValueNoise1D(float coordinate)
{
	float i = floor(coordinate);
	float f = coordinate - i;
	float2 hash = ShalltearGraphNoiseHash1D(i);
	float2 blend = ShalltearGraphNoiseC2Pair(f);
	float delta = hash.y - hash.x;
	return float2(hash.x + delta * blend.x, delta * blend.y);
}

)",
			 R"(float3 ShalltearGraphValueNoise2D(float2 coordinate)
{
	float2 i = floor(coordinate);
	float2 f = coordinate - i;
	float4 hash = ShalltearGraphNoiseHash2D(i);
	float4 blend = ShalltearGraphNoiseC2Pair(f);
	float4 res0 = lerp(hash.xyxz, hash.zwyw, blend.yyxx);
	float2 delta = res0.yw - res0.xz;
	return float3(res0.x + delta.x * blend.x, delta * blend.zw);
}

)",
			 R"(float4 ShalltearGraphValueNoise3D(float3 coordinate)
{
	float3 i = floor(coordinate);
	float3 f = coordinate - i;
	float4 lowz, highz;
	ShalltearGraphNoiseHash3D(i, lowz, highz);
	float3 blend = ShalltearGraphNoiseC2(f);
	float4 res0 = lerp(lowz, highz, blend.z);
	float4 res1 = lerp(res0.xyxz, res0.zwyw, blend.yyxx);
	float4 res3 = lerp(float4(lowz.xy, highz.xy), float4(lowz.zw, highz.zw), blend.y);
	float2 res4 = lerp(res3.xz, res3.yw, blend.x);
	float3 delta = float3(res1.yw, res4.y) - float3(res1.xz, res4.x);
	return float4(res1.x + delta.x * blend.x, delta * ShalltearGraphNoiseC2Derivative(f));
}

)"},
			{R"(float2 ShalltearGraphPerlinNoise1D(float coordinate)
{
	float2 i = floor(float2(coordinate, 0.0));
	float4 f = float2(coordinate, 0.0).xyxy - float4(i, i + 1.0);
	float4 hashX, hashY;
	ShalltearGraphNoiseHash2D(i, hashX, hashY);
	float4 gradX = hashX - 0.49999;
	float4 gradY = hashY - 0.49999;
	float4 norm = rsqrt(gradX * gradX + gradY * gradY);
	gradX *= norm;
	gradY *= norm;
	float4 dots = gradX * f.xzxz + gradY * f.yyww;
	float2 d0 = float2(dots.x, gradX.x);
	float2 k0 = float2(dots.y, gradX.y) - d0;
	float2 k1 = float2(dots.z, gradX.z) - d0;
	float2 k2 = float2(dots.w, gradX.w) - float2(dots.z, gradX.z) - k0;
	float4 blend = ShalltearGraphNoiseC2Pair(f.xy);
	float2 result = d0 + blend.x * k0 + blend.y * (k1 + blend.x * k2);
	result.y += blend.z * (k0.x + blend.y * k2.x);
	return result * 2.0;
}

)",
			 R"(float3 ShalltearGraphPerlinNoise2D(float2 coordinate)
{
	float2 i = floor(coordinate);
	float4 f = coordinate.xyxy - float4(i, i + 1.0);
	float4 hashX, hashY;
	ShalltearGraphNoiseHash2D(i, hashX, hashY);
	float4 gradX = hashX - 0.49999;
	float4 gradY = hashY - 0.49999;
	float4 norm = rsqrt(gradX * gradX + gradY * gradY);
	gradX *= norm;
	gradY *= norm;
	float4 dots = gradX * f.xzxz + gradY * f.yyww;
	float3 d0 = float3(dots.x, gradX.x, gradY.x);
	float3 d2 = float3(dots.z, gradX.z, gradY.z);
	float3 k0 = float3(dots.y, gradX.y, gradY.y) - d0;
	float3 k1 = d2 - d0;
	float3 k2 = float3(dots.w, gradX.w, gradY.w) - d2 - k0;
	float4 blend = ShalltearGraphNoiseC2Pair(f.xy);
	float3 result = d0 + blend.x * k0 + blend.y * (k1 + blend.x * k2);
	result.yz += blend.zw * (float2(k0.x, k1.x) + blend.yx * k2.xx);
	return result * 1.4142135623730950488;
}

)",
			 R"(float4 ShalltearGraphPerlinNoise3D(float3 coordinate)
{
	float3 i = floor(coordinate);
	float3 f = coordinate - i;
	float3 f1 = f - 1.0;
	float4 hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1;
	ShalltearGraphNoiseHash3D(i, hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1);
	float4 gradX0 = hashX0 - 0.49999;
	float4 gradY0 = hashY0 - 0.49999;
	float4 gradZ0 = hashZ0 - 0.49999;
	float4 gradX1 = hashX1 - 0.49999;
	float4 gradY1 = hashY1 - 0.49999;
	float4 gradZ1 = hashZ1 - 0.49999;
	float4 norm0 = rsqrt(gradX0 * gradX0 + gradY0 * gradY0 + gradZ0 * gradZ0);
	float4 norm1 = rsqrt(gradX1 * gradX1 + gradY1 * gradY1 + gradZ1 * gradZ1);
	gradX0 *= norm0;
	gradY0 *= norm0;
	gradZ0 *= norm0;
	gradX1 *= norm1;
	gradY1 *= norm1;
	gradZ1 *= norm1;
	float4 dots0 = float2(f.x, f1.x).xyxy * gradX0 + float2(f.y, f1.y).xxyy * gradY0 + f.zzzz * gradZ0;
	float4 dots1 = float2(f.x, f1.x).xyxy * gradX1 + float2(f.y, f1.y).xxyy * gradY1 + f1.zzzz * gradZ1;
	float4 d0 = float4(dots0.x, gradX0.x, gradY0.x, gradZ0.x);
	float4 d1 = float4(dots0.y, gradX0.y, gradY0.y, gradZ0.y);
	float4 d2 = float4(dots0.z, gradX0.z, gradY0.z, gradZ0.z);
	float4 d3 = float4(dots0.w, gradX0.w, gradY0.w, gradZ0.w);
	float4 d4 = float4(dots1.x, gradX1.x, gradY1.x, gradZ1.x);
	float4 d5 = float4(dots1.y, gradX1.y, gradY1.y, gradZ1.y);
	float4 d6 = float4(dots1.z, gradX1.z, gradY1.z, gradZ1.z);
	float4 d7 = float4(dots1.w, gradX1.w, gradY1.w, gradZ1.w);
	float4 k0 = d1 - d0;
	float4 k1 = d2 - d0;
	float4 k2 = d4 - d0;
	float4 k3 = d3 - d2 - k0;
	float4 k4 = d5 - d4 - k0;
	float4 k5 = d6 - d4 - k1;
	float4 k6 = (d7 - d6) - (d5 - d4) - k3;
	float3 blend = ShalltearGraphNoiseC2(f);
	float3 blendDerivative = ShalltearGraphNoiseC2Derivative(f);
	float u = blend.x;
	float v = blend.y;
	float w = blend.z;
	float4 result = d0 + u * (k0 + v * k3) + v * (k1 + w * k5) + w * (k2 + u * (k4 + v * k6));
	result.y += dot(float4(k0.x, k3.x * v, float2(k4.x, k6.x * v) * w), blendDerivative.xxxx);
	result.z += dot(float4(k1.x, k3.x * u, float2(k5.x, k6.x * u) * w), blendDerivative.yyyy);
	result.w += dot(float4(k2.x, k4.x * u, float2(k5.x, k6.x * u) * v), blendDerivative.zzzz);
	return result * 1.1547005383792515290;
}

)"},
			{R"(float2 ShalltearGraphCellularNoise1D(float coordinate)
{
	float2 i = floor(float2(coordinate, 0.0));
	float2 f = float2(coordinate, 0.0) - i;
	float4 hashX, hashY;
	ShalltearGraphNoiseHash2D(i, hashX, hashY);
	hashX = ShalltearGraphNoiseCellularWeight(hashX) * 0.25 + float4(0.0, 1.0, 0.0, 1.0);
	hashY = ShalltearGraphNoiseCellularWeight(hashY) * 0.25 + float4(0.0, 0.0, 1.0, 1.0);
	float4 dx = f.xxxx - hashX;
	float4 dy = f.yyyy - hashY;
	float4 d = dx * dx + dy * dy;
	float2 t1 = d.x < d.y ? float2(d.x, dx.x) : float2(d.y, dx.y);
	float2 t2 = d.z < d.w ? float2(d.z, dx.z) : float2(d.w, dx.w);
	return (t1.x < t2.x ? t1 : t2) * float2(1.0, 2.0) * (1.0 / 1.125);
}

)",
			 R"(float3 ShalltearGraphCellularNoise2D(float2 coordinate)
{
	float2 i = floor(coordinate);
	float2 f = coordinate - i;
	float4 hashX, hashY;
	ShalltearGraphNoiseHash2D(i, hashX, hashY);
	hashX = ShalltearGraphNoiseCellularWeight(hashX) * 0.25 + float4(0.0, 1.0, 0.0, 1.0);
	hashY = ShalltearGraphNoiseCellularWeight(hashY) * 0.25 + float4(0.0, 0.0, 1.0, 1.0);
	float4 dx = f.xxxx - hashX;
	float4 dy = f.yyyy - hashY;
	float4 d = dx * dx + dy * dy;
	float3 t1 = d.x < d.y ? float3(d.x, dx.x, dy.x) : float3(d.y, dx.y, dy.y);
	float3 t2 = d.z < d.w ? float3(d.z, dx.z, dy.z) : float3(d.w, dx.w, dy.w);
	return (t1.x < t2.x ? t1 : t2) * float3(1.0, 2.0, 2.0) * (1.0 / 1.125);
}

)",
			 R"(float4 ShalltearGraphCellularNoise3D(float3 coordinate)
{
	float3 i = floor(coordinate);
	float3 f = coordinate - i;
	float4 hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1;
	ShalltearGraphNoiseHash3D(i, hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1);
	hashX0 = ShalltearGraphNoiseCellularWeight(hashX0) * 0.166666666 + float4(0.0, 1.0, 0.0, 1.0);
	hashY0 = ShalltearGraphNoiseCellularWeight(hashY0) * 0.166666666 + float4(0.0, 0.0, 1.0, 1.0);
	hashX1 = ShalltearGraphNoiseCellularWeight(hashX1) * 0.166666666 + float4(0.0, 1.0, 0.0, 1.0);
	hashY1 = ShalltearGraphNoiseCellularWeight(hashY1) * 0.166666666 + float4(0.0, 0.0, 1.0, 1.0);
	hashZ0 = ShalltearGraphNoiseCellularWeight(hashZ0) * 0.166666666 + float4(0.0, 0.0, 0.0, 0.0);
	hashZ1 = ShalltearGraphNoiseCellularWeight(hashZ1) * 0.166666666 + float4(1.0, 1.0, 1.0, 1.0);
	float4 dx1 = f.xxxx - hashX0;
	float4 dy1 = f.yyyy - hashY0;
	float4 dz1 = f.zzzz - hashZ0;
	float4 dx2 = f.xxxx - hashX1;
	float4 dy2 = f.yyyy - hashY1;
	float4 dz2 = f.zzzz - hashZ1;
	float4 d1 = dx1 * dx1 + dy1 * dy1 + dz1 * dz1;
	float4 d2 = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
	float4 r1 = d1.x < d1.y ? float4(d1.x, dx1.x, dy1.x, dz1.x) : float4(d1.y, dx1.y, dy1.y, dz1.y);
	float4 r2 = d1.z < d1.w ? float4(d1.z, dx1.z, dy1.z, dz1.z) : float4(d1.w, dx1.w, dy1.w, dz1.w);
	float4 r3 = d2.x < d2.y ? float4(d2.x, dx2.x, dy2.x, dz2.x) : float4(d2.y, dx2.y, dy2.y, dz2.y);
	float4 r4 = d2.z < d2.w ? float4(d2.z, dx2.z, dy2.z, dz2.z) : float4(d2.w, dx2.w, dy2.w, dz2.w);
	float4 t1 = r1.x < r2.x ? r1 : r2;
	float4 t2 = r3.x < r4.x ? r3 : r4;
	return (t1.x < t2.x ? t1 : t2) * float4(1.0, 2.0, 2.0, 2.0) * (9.0 / 12.0);
}

)"}};

		constexpr std::string_view EFFECT_NOISE_FRACTAL = R"(%R% ShalltearGraph%T%Fractal%N%D(%C% coordinate, float3 parameters, float octaves)
{
	%R% total = (%R%)0.0;
	float frequency = parameters.x;
	float amplitude = 1.0;
	float totalAmplitude = 0.0;
	for (int octave = 0; octave < (int)octaves; octave++)
	{
		total += ShalltearGraph%T%Noise%N%D(coordinate * frequency) * amplitude;
		totalAmplitude += amplitude;
		amplitude *= parameters.y;
		frequency *= parameters.z;
	}
	return total / totalAmplitude;
}

)";

		constexpr std::string_view EFFECT_NOISE_CURL_2D = R"(float2 ShalltearGraph%T%Curl2D(float2 coordinate, float3 parameters, float octaves)
{
	float2 total = float2(0.0, 0.0);
	float frequency = parameters.x;
	float amplitude = 1.0;
	float totalAmplitude = 0.0;
	for (int octave = 0; octave < (int)octaves; octave++)
	{
		total += ShalltearGraph%T%Noise2D(coordinate * frequency).yz * amplitude;
		totalAmplitude += amplitude;
		amplitude *= parameters.y;
		frequency *= parameters.z;
	}
	return float2(total.y, -total.x) / totalAmplitude;
}

)";

		constexpr std::string_view EFFECT_NOISE_CURL_3D = R"(float3 ShalltearGraph%T%Curl3D(float3 coordinate, float3 parameters, float octaves)
{
	float2 total0 = float2(0.0, 0.0);
	float2 total1 = float2(0.0, 0.0);
	float2 total2 = float2(0.0, 0.0);
	float frequency = parameters.x;
	float amplitude = 1.0;
	float totalAmplitude = 0.0;
	for (int octave = 0; octave < (int)octaves; octave++)
	{
		total0 += ShalltearGraph%T%Noise2D(coordinate.zy * frequency).yz * amplitude;
		total1 += ShalltearGraph%T%Noise2D((coordinate.xz + 100.0) * frequency).yz * amplitude;
		total2 += ShalltearGraph%T%Noise2D((coordinate.yx + 200.0) * frequency).yz * amplitude;
		totalAmplitude += amplitude;
		amplitude *= parameters.y;
		frequency *= parameters.z;
	}
	return float3(total2.x - total1.y, total0.x - total2.y, total1.x - total0.y) / totalAmplitude;
}

)";

		constexpr std::array<std::string_view, 3> EFFECT_NOISE_NAMES = {"Value", "Perlin", "Cellular"};

		str Substitute(std::string_view text, std::span<const std::pair<std::string_view, std::string_view>> pairs) {
			str out(text);
			for (const auto& [key, value] : pairs) {
				for (size_t at = out.find(key); at != str::npos; at = out.find(key, at + value.size())) {
					out.replace(at, key.size(), value);
				}
			}
			return out;
		}

		constexpr std::array<std::string_view, 3> HASH_NAMES = {"Deterministic", "LegacySine", "LegacyMod"};

		str Instantiate(std::string_view text, std::string_view hash) {
			str out;
			size_t at = 0;
			for (size_t found = text.find("%HASH%"); found != std::string_view::npos; found = text.find("%HASH%", at)) {
				out.append(text.substr(at, found - at));
				out.append(hash);
				at = found + 6;
			}
			out.append(text.substr(at));
			return out;
		}

		std::string_view NoiseFunction(GraphOp op) {
			switch (op) {
			case GraphOp::ValueNoise:
				return "ShalltearGraphValueNoise";
			case GraphOp::GradientNoise:
				return "ShalltearGraphGradientNoise";
			case GraphOp::Voronoi:
				return "ShalltearGraphVoronoi";
			default:
				return {};
			}
		}

		bool IsInline(const GraphValue& value) { return value.op == GraphOp::Constant || value.op == GraphOp::External; }

		bool IsCall(GraphOp op) {
			switch (op) {
			case GraphOp::Negate:
			case GraphOp::Add:
			case GraphOp::Subtract:
			case GraphOp::Multiply:
			case GraphOp::Divide:
			case GraphOp::Less:
			case GraphOp::LessEqual:
			case GraphOp::Equal:
			case GraphOp::Select:
			case GraphOp::Swizzle:
			case GraphOp::Construct:
			case GraphOp::MatrixRow:
			case GraphOp::BoolToFloat:
			case GraphOp::LogicalAnd:
			case GraphOp::LogicalOr:
			case GraphOp::LogicalNot:
			case GraphOp::Constant:
			case GraphOp::External:
				return false;
			default:
				return true;
			}
		}

		bool IsIdentifier(std::string_view text) {
			if (text.empty() || (text[0] >= '0' && text[0] <= '9')) {
				return false;
			}
			for (char c : text) {
				bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
				if (!word) {
					return false;
				}
			}
			return true;
		}

		str BareLiteral(f32 value) {
			if (!std::isfinite(value)) {
				char text[32];
				std::snprintf(text, sizeof(text), "asfloat(0x%08Xu)", std::bit_cast<u32>(value));
				return text;
			}
			char text[32];
			std::to_chars_result result = std::to_chars(text, text + sizeof(text), value);
			str out(text, result.ptr);
			if (out.find_first_of(".e") == str::npos) {
				out += ".0";
			}
			return out;
		}

		class Emitter {
		public:
			Emitter(const GraphProgram& program, GraphHLSLText& out, const GraphHLSLHelpers* alongside)
				: _program(program), _out(out), _alongside(alongside) {
				_names.resize(program.values.size());
			}

			void Run(GraphStageMask stage) {
				Vector<u8> needed{GetMemoryResource(MemTag::Editor)};
				needed.assign(_program.values.size(), 0);
				for (const GraphRoot& root : _program.roots) {
					if (root.value != GRAPH_NO_VALUE && (root.stage & stage) != 0) {
						needed[root.value] = 1;
					}
				}
				for (size_t i = _program.values.size(); i-- > 0;) {
					if (needed[i] != 0) {
						const GraphValue& value = _program.values[i];
						for (u32 operand = 0; operand < value.operandCount; ++operand) {
							needed[value.operands[operand]] = 1;
						}
					}
				}
				for (GraphValueId id = 0; id < _program.values.size(); ++id) {
					const GraphValue& value = _program.values[id];
					if (needed[id] != 0 && value.op == GraphOp::CallResult) {
						_results[(static_cast<u64>(value.operands[0]) << 32) | value.payload] = id;
					}
				}

				Vector<u32> waiting{GetMemoryResource(MemTag::Editor)};
				waiting.assign(_program.values.size(), 0);
				Vector<Vector<GraphValueId>> readers{GetMemoryResource(MemTag::Editor)};
				readers.resize(_program.values.size(), Vector<GraphValueId>{GetMemoryResource(MemTag::Editor)});
				using Key = std::pair<u32, GraphValueId>;
				std::priority_queue<Key, std::vector<Key>, std::greater<Key>> ready;
				for (GraphValueId id = 0; id < _program.values.size(); ++id) {
					if (needed[id] == 0) {
						continue;
					}
					const GraphValue& value = _program.values[id];
					for (u32 i = 0; i < value.operandCount; ++i) {
						GraphValueId operand = value.operands[i];
						bool repeated = false;
						for (u32 j = 0; j < i; ++j) {
							repeated = repeated || value.operands[j] == operand;
						}
						if (!repeated && !IsInline(_program.values[operand])) {
							++waiting[id];
							readers[operand].push_back(id);
						}
					}
					if (waiting[id] == 0) {
						ready.push({value.node, id});
					}
				}
				while (!ready.empty()) {
					GraphValueId id = ready.top().second;
					ready.pop();
					const GraphValue& value = _program.values[id];
					if (!IsInline(value)) {
						EmitStatement(id);
					}
					RequireFunctions(value);
					for (GraphValueId reader : readers[id]) {
						if (--waiting[reader] == 0) {
							ready.push({_program.values[reader].node, reader});
						}
					}
				}

				for (const GraphRoot& root : _program.roots) {
					bool emitted = root.value != GRAPH_NO_VALUE && (root.stage & stage) != 0;
					_out.roots.push_back(emitted ? Operand(root.value) : str());
				}
				WriteFunctions();
			}
		private:
			str NameOf(GraphValueId id) {
				if (_names[id].empty()) {
					const GraphValue& value = _program.values[id];
					str name = "n" + std::to_string(value.node) + "_";
					const str& label = _program.labels[id];
					if (!label.empty()) {
						for (char c : label) {
							bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
							name += word ? c : '_';
						}
					} else {
						name += std::to_string(_temporaries[value.node]++);
					}
					_names[id] = std::move(name);
				}
				return _names[id];
			}

			str Literal(GraphValueType type, std::span<const f32> components) {
				if (type == GraphValueType::Bool) {
					return components[0] != 0.0f ? "true" : "false";
				}
				if (components.size() == 1) {
					str literal = BareLiteral(components[0]);
					return literal[0] == '-' ? "(" + literal + ")" : literal;
				}
				str out(GraphTypes::HLSLName(type));
				out += "(";
				for (size_t i = 0; i < components.size(); ++i) {
					out += (i == 0 ? "" : ", ") + BareLiteral(components[i]);
				}
				return out + ")";
			}

			str Operand(GraphValueId id) {
				const GraphValue& value = _program.values[id];
				if (value.op == GraphOp::Constant) {
					return Literal(value.type, _program.ConstantOf(id));
				}
				if (value.op == GraphOp::External) {
					const str& expression = _program.externals[value.payload].expression;
					return IsIdentifier(expression) ? expression : "(" + expression + ")";
				}
				return NameOf(id);
			}

			str CallOperand(const GraphValue& call, u32 index) {
				GraphValueId id = call.operands[index];
				GraphValueType type = _program.values[id].type;
				if (type == GraphValueType::Float && GraphTypes::VectorWidth(call.type) > 1) {
					return "(" + str(GraphTypes::HLSLName(call.type)) + ")" + Operand(id);
				}
				return Operand(id);
			}

			str Expression(const GraphValue& value) {
				auto operand = [this, &value](u32 i) { return Operand(value.operands[i]); };
				switch (value.op) {
				case GraphOp::Negate:
					return "-" + operand(0);
				case GraphOp::Add:
					return operand(0) + " + " + operand(1);
				case GraphOp::Subtract:
					return operand(0) + " - " + operand(1);
				case GraphOp::Multiply:
					return operand(0) + " * " + operand(1);
				case GraphOp::Divide:
					return operand(0) + " / " + operand(1);
				case GraphOp::Less:
					return operand(0) + " < " + operand(1);
				case GraphOp::LessEqual:
					return operand(0) + " <= " + operand(1);
				case GraphOp::Equal:
					return operand(0) + " == " + operand(1);
				case GraphOp::Select:
					return operand(0) + " ? " + operand(1) + " : " + operand(2);
				case GraphOp::BoolToFloat:
					return "(float)" + operand(0);
				case GraphOp::LogicalAnd:
					return operand(0) + " && " + operand(1);
				case GraphOp::LogicalOr:
					return operand(0) + " || " + operand(1);
				case GraphOp::LogicalNot:
					return "!" + operand(0);
				case GraphOp::MatrixRow:
					return operand(0) + "[" + std::to_string(value.payload) + "]";
				case GraphOp::MatrixMultiply:
					return "mul(" + operand(0) + ", " + operand(1) + ")";
				case GraphOp::Swizzle: {
					const GraphValue& source = _program.values[value.operands[0]];
					if (source.op == GraphOp::Constant) {
						std::array<f32, 4> picked{};
						std::span<const f32> components = _program.ConstantOf(value.operands[0]);
						for (u32 i = 0; i < SwizzlePattern::Count(value.payload); ++i) {
							picked[i] = components[SwizzlePattern::Component(value.payload, i)];
						}
						return Literal(value.type, {picked.data(), SwizzlePattern::Count(value.payload)});
					}
					str out = operand(0) + ".";
					for (u32 i = 0; i < SwizzlePattern::Count(value.payload); ++i) {
						out += "xyzw"[SwizzlePattern::Component(value.payload, i)];
					}
					return out;
				}
				case GraphOp::Construct: {
					str out(GraphTypes::HLSLName(value.type));
					out += "(";
					for (u32 i = 0; i < value.operandCount; ++i) {
						out += (i == 0 ? "" : ", ") + operand(i);
					}
					return out + ")";
				}
				case GraphOp::ValueNoise:
				case GraphOp::GradientNoise:
				case GraphOp::Voronoi: {
					str out = str(NoiseFunction(value.op)) + str(HASH_NAMES[value.payload]) + "(";
					for (u32 i = 0; i < value.operandCount; ++i) {
						out += (i == 0 ? "" : ", ") + operand(i);
					}
					return out + ")";
				}
				case GraphOp::FractalNoise:
				case GraphOp::CurlNoise: {
					const u32 dimension = GraphTypes::VectorWidth(_program.values[value.operands[0]].type);
					return std::format("ShalltearGraph{}{}{}D({}, {}, {})", EFFECT_NOISE_NAMES[value.payload],
									   value.op == GraphOp::FractalNoise ? "Fractal" : "Curl", dimension, operand(0), operand(1), operand(2));
				}
				case GraphOp::SampleTexture:
					return operand(0) + ".Sample(" + SamplerOf(value) + ", " + operand(1) + ")";
				case GraphOp::SampleTextureLevel:
					return operand(0) + ".SampleLevel(" + SamplerOf(value) + ", " + operand(1) + ", " + operand(2) + ")";
				case GraphOp::SampleTextureBias:
					return operand(0) + ".SampleBias(" + SamplerOf(value) + ", " + operand(1) + ", " + operand(2) + ")";
				case GraphOp::SampleTextureGrad:
					return operand(0) + ".SampleGrad(" + SamplerOf(value) + ", " + operand(1) + ", " + operand(2) + ", " + operand(3) + ")";
				case GraphOp::TextureLevelOfDetail:
					return operand(0) + (value.payload != 0 ? ".CalculateLevelOfDetail(" : ".CalculateLevelOfDetailUnclamped(") +
						   SamplerOf(value) + ", " + operand(1) + ")";
				case GraphOp::TextureSize:
					return "ShalltearGraphTextureSize(" + operand(0) + ")";
				case GraphOp::LoadTexture:
					return operand(0) + ".Load(int3(int2(" + operand(1) + "), 0))";
				case GraphOp::MeshCount:
					return "float(SltParticleMeshCount(" + operand(0) + ", " + std::to_string(value.payload) + "u))";
				case GraphOp::MeshIndex:
					return "float(SltParticleMeshIndex(" + operand(0) + ", " + operand(1) + "))";
				case GraphOp::MeshVertex:
					return "SltParticleMeshVertex(" + operand(0) + ", " + operand(1) + ", " + std::to_string(value.payload) + "u)" +
						   (value.type == GraphValueType::Float3 ? ".xyz" : "");
				case GraphOp::Random:
					return "ShalltearGraphRandom(" + operand(0) + ", " + operand(1) + ", " + std::to_string(value.payload) + "u)";
				case GraphOp::ParallaxOcclusion:
					return "ShalltearGraphParallaxOcclusion(" + operand(0) + ", " + SamplerOf(value) + ", " + operand(1) + ", " + operand(2) + ", " +
						   operand(3) + ", " + std::to_string(value.payload) + "u)";
				default: {
					str out = str(GraphOps::Name(value.op)) + "(";
					for (u32 i = 0; i < value.operandCount; ++i) {
						out += (i == 0 ? "" : ", ") + CallOperand(value, i);
					}
					return out + ")";
				}
				}
			}

			void EmitStatement(GraphValueId id) {
				const GraphValue& value = _program.values[id];
				if (value.op == GraphOp::Argument || value.op == GraphOp::CallResult) {
					return;
				}
				if (value.op == GraphOp::Call) {
					EmitCall(id, value);
					return;
				}
				if (value.op == GraphOp::Keyword) {
					const str type(GraphTypes::HLSLName(value.type));
					_out.body += "#if defined(" + Operand(value.operands[0]) + ")\n";
					_out.body += "\t" + type + " " + NameOf(id) + " = " + Operand(value.operands[1]) + ";\n";
					_out.body += "#else\n";
					_out.body += "\t" + type + " " + NameOf(id) + " = " + Operand(value.operands[2]) + ";\n";
					_out.body += "#endif\n";
					for (int line = 0; line < 5; ++line) {
						_out.lineNodes.push_back(value.node);
					}
					return;
				}
				str expression = Expression(value);
				_out.body += "\t";
				_out.body += GraphTypes::HLSLName(value.type);
				_out.body += " " + NameOf(id) + " = " + expression + ";\n";
				_out.lineNodes.push_back(value.node);
			}

			void EmitCall(GraphValueId id, const GraphValue& value) {
				const GraphCall& call = _program.calls[value.payload];
				Vector<GraphValueId> arguments{GetMemoryResource(MemTag::Editor)};
				for (GraphValueId at = value.operandCount > 0 ? value.operands[0] : GRAPH_NO_VALUE; at != GRAPH_NO_VALUE;) {
					const GraphValue& argument = _program.values[at];
					arguments.push_back(argument.operands[0]);
					at = argument.operandCount > 1 ? argument.operands[1] : GRAPH_NO_VALUE;
				}
				str list;
				for (size_t i = arguments.size(); i-- > 0;) {
					list += (list.empty() ? "" : ", ") + Operand(arguments[i]);
				}
				for (u32 output = 0; output < call.outputs.size(); ++output) {
					auto read = _results.find((static_cast<u64>(id) << 32) | output);
					const str name = read != _results.end() ? NameOf(read->second) : "n" + std::to_string(value.node) + "_" + std::to_string(_temporaries[value.node]++);
					_out.body += "\t" + str(GraphTypes::HLSLName(call.outputs[output])) + " " + name + ";\n";
					_out.lineNodes.push_back(value.node);
					list += (list.empty() ? "" : ", ") + name;
				}
				_out.body += "\t" + call.function + "(" + list + ");\n";
				_out.lineNodes.push_back(value.node);
			}

			str SamplerOf(const GraphValue& value) {
				const GraphValue& texture = _program.values[value.operands[0]];
				return _program.externals[texture.payload].sampler;
			}

			void RequireFunctions(const GraphValue& value) {
				if (value.op == GraphOp::Call) {
					const GraphCall& call = _program.calls[value.payload];
					Vector<str>& written = call.include.empty() ? _definitions : _includes;
					const str& text = call.include.empty() ? call.function : call.include;
					if (std::find(written.begin(), written.end(), text) == written.end()) {
						written.push_back(text);
						(call.include.empty() ? _definitionNodes : _includeNodes).push_back(value.node);
						if (call.include.empty()) {
							_definitionTexts.push_back(call.definition);
						}
					}
					return;
				}
				if (value.op == GraphOp::TextureSize) {
					_textureSize = true;
					return;
				}
				if (value.op == GraphOp::Random) {
					_random = true;
					return;
				}
				if (value.op == GraphOp::ParallaxOcclusion) {
					_parallaxOcclusion = true;
					return;
				}
				if (value.op == GraphOp::FractalNoise || value.op == GraphOp::CurlNoise) {
					const u32 dimension = GraphTypes::VectorWidth(_program.values[value.operands[0]].type);
					const u32 type = value.payload;
					if (value.op == GraphOp::FractalNoise) {
						_effectFractals |= 1u << (type * 3 + dimension - 1);
						_effectBases |= 1u << (type * 3 + dimension - 1);
					} else {
						_effectCurls |= 1u << (type * 3 + dimension - 1);
						_effectBases |= 1u << (type * 3 + 1);
					}
					return;
				}
				if (value.op != GraphOp::ValueNoise && value.op != GraphOp::GradientNoise && value.op != GraphOp::Voronoi) {
					return;
				}
				u32 hash = value.payload;
				_hashes |= 1u << hash;
				u32 shift = value.op == GraphOp::ValueNoise ? 0 : (value.op == GraphOp::GradientNoise ? 3 : 6);
				_noises |= 1u << (shift + hash);
			}

			void Write(std::string_view piece, u32 node) {
				_out.functions += piece;
				for (char c : piece) {
					if (c == '\n') {
						_out.functionLineNodes.push_back(node);
					}
				}
			}

			void WriteFunctions() {
				if (_alongside) {
					const GraphHLSLHelpers& held = *_alongside;
					_out.helpers = held;
					const bool commonHeld = held.effectBases != 0;
					_hashes &= ~held.hashes;
					_noises &= ~held.noises;
					_textureSize = _textureSize && !held.textureSize;
					_random = _random && !held.random;
					_parallaxOcclusion = _parallaxOcclusion && !held.parallaxOcclusion;
					_effectBases &= ~held.effectBases;
					_effectFractals &= ~held.effectFractals;
					_effectCurls &= ~held.effectCurls;
					_writeEffectCommon = !commonHeld;
					for (size_t i = _includes.size(); i-- > 0;) {
						if (std::find(held.includes.begin(), held.includes.end(), _includes[i]) != held.includes.end()) {
							_includes.erase(_includes.begin() + static_cast<std::ptrdiff_t>(i));
							_includeNodes.erase(_includeNodes.begin() + static_cast<std::ptrdiff_t>(i));
						}
					}
					for (size_t i = _definitions.size(); i-- > 0;) {
						if (std::find(held.definitions.begin(), held.definitions.end(), _definitions[i]) != held.definitions.end()) {
							_definitions.erase(_definitions.begin() + static_cast<std::ptrdiff_t>(i));
							_definitionTexts.erase(_definitionTexts.begin() + static_cast<std::ptrdiff_t>(i));
							_definitionNodes.erase(_definitionNodes.begin() + static_cast<std::ptrdiff_t>(i));
						}
					}
				}
				GraphHLSLHelpers& helpers = _out.helpers;
				helpers.hashes |= _hashes;
				helpers.noises |= _noises;
				helpers.textureSize = helpers.textureSize || _textureSize;
				helpers.random = helpers.random || _random;
				helpers.parallaxOcclusion = helpers.parallaxOcclusion || _parallaxOcclusion;
				helpers.effectBases |= _effectBases;
				helpers.effectFractals |= _effectFractals;
				helpers.effectCurls |= _effectCurls;
				helpers.includes.insert(helpers.includes.end(), _includes.begin(), _includes.end());
				helpers.definitions.insert(helpers.definitions.end(), _definitions.begin(), _definitions.end());

				if ((_hashes & (1u << static_cast<u32>(GraphNoiseHash::Deterministic))) != 0) {
					Write(HASH_TCHOU, 0);
				}
				if ((_hashes & (1u << static_cast<u32>(GraphNoiseHash::LegacySine))) != 0) {
					Write(HASH_LEGACY_SINE, 0);
				}
				if ((_hashes & (1u << static_cast<u32>(GraphNoiseHash::LegacyMod))) != 0) {
					Write(HASH_LEGACY_MOD, 0);
				}
				constexpr std::array<std::string_view, 3> NOISES = {VALUE_NOISE, GRADIENT_NOISE, VORONOI};
				for (u32 noise = 0; noise < NOISES.size(); ++noise) {
					for (u32 hash = 0; hash < HASH_NAMES.size(); ++hash) {
						if ((_noises & (1u << (noise * 3 + hash))) != 0) {
							Write(Instantiate(NOISES[noise], HASH_NAMES[hash]), 0);
						}
					}
				}
				if (_textureSize) {
					Write(TEXTURE_SIZE, 0);
				}
				if (_random) {
					Write(RANDOM, 0);
				}
				if (_parallaxOcclusion) {
					Write(PARALLAX_OCCLUSION, 0);
				}
				if (_effectBases != 0 && _writeEffectCommon) {
					Write(EFFECT_NOISE_COMMON, 0);
				}
				constexpr std::string_view COORDINATES[] = {"float", "float2", "float3"};
				constexpr std::string_view RESULTS[] = {"float2", "float3", "float4"};
				for (u32 type = 0; type < 3; ++type) {
					for (u32 dimension = 1; dimension <= 3; ++dimension) {
						const u32 bit = 1u << (type * 3 + dimension - 1);
						if ((_effectBases & bit) != 0) {
							Write(EFFECT_NOISE_BASES[type][dimension - 1], 0);
						}
						const std::string_view dimensionText = std::string_view("123").substr(dimension - 1, 1);
						const std::pair<std::string_view, std::string_view> names[] = {
							{"%T%", EFFECT_NOISE_NAMES[type]}, {"%N%", dimensionText}, {"%C%", COORDINATES[dimension - 1]}, {"%R%", RESULTS[dimension - 1]}};
						if ((_effectFractals & bit) != 0) {
							Write(Substitute(EFFECT_NOISE_FRACTAL, names), 0);
						}
						if ((_effectCurls & bit) != 0) {
							Write(Substitute(dimension == 2 ? EFFECT_NOISE_CURL_2D : EFFECT_NOISE_CURL_3D, names), 0);
						}
					}
				}
				for (size_t i = 0; i < _includes.size(); ++i) {
					Write("#include \"" + _includes[i] + "\"\n", _includeNodes[i]);
				}
				for (size_t i = 0; i < _definitionTexts.size(); ++i) {
					Write(_definitionTexts[i], _definitionNodes[i]);
				}
			}

			const GraphProgram& _program;
			GraphHLSLText& _out;
			const GraphHLSLHelpers* _alongside = nullptr;
			bool _writeEffectCommon = true;
			Vector<str> _names{GetMemoryResource(MemTag::Editor)};
			std::pmr::unordered_map<u32, u32> _temporaries{GetMemoryResource(MemTag::Editor)};
			u32 _hashes = 0;
			u32 _noises = 0;
			bool _textureSize = false;
			bool _random = false;
			bool _parallaxOcclusion = false;
			u32 _effectBases = 0;
			u32 _effectFractals = 0;
			u32 _effectCurls = 0;
			std::pmr::unordered_map<u64, GraphValueId> _results{GetMemoryResource(MemTag::Editor)};
			Vector<str> _includes{GetMemoryResource(MemTag::Editor)};
			Vector<str> _definitions{GetMemoryResource(MemTag::Editor)};
			Vector<str> _definitionTexts{GetMemoryResource(MemTag::Editor)};
			Vector<u32> _includeNodes{GetMemoryResource(MemTag::Editor)};
			Vector<u32> _definitionNodes{GetMemoryResource(MemTag::Editor)};
		};

	} // anonymous namespace

	namespace {

		str PathKey(std::string_view path) {
			str key = std::filesystem::path(path).lexically_normal().generic_string();
			for (char& c : key) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return key;
		}

		bool IsNumber(std::string_view text) {
			return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
		}

		bool SplitPlace(std::string_view place, std::string_view& file, u32& line) {
			std::string_view numbers[2];
			u32 found = 0;
			while (found < 2) {
				const size_t colon = place.rfind(':');
				if (colon == std::string_view::npos || !IsNumber(place.substr(colon + 1))) {
					break;
				}
				numbers[found++] = place.substr(colon + 1);
				place = place.substr(0, colon);
			}
			if (found == 0) {
				return false;
			}
			const std::string_view number = numbers[found - 1];
			line = 0;
			for (char c : number) {
				line = line * 10 + static_cast<u32>(c - '0');
			}
			file = place;
			return true;
		}

	} // anonymous namespace

	namespace GraphHLSL {

		u32 MapCompilerMessages(std::string_view messages, std::string_view path, std::string_view text, std::span<const u32> lines,
								Vector<GraphDiagnostic>& out) {
			const str generated = PathKey(path);
			const std::filesystem::path directory = std::filesystem::path(path).parent_path();
			Vector<std::pair<str, u32>> includes{GetMemoryResource(MemTag::Editor)};
			constexpr std::string_view INCLUDE = "#include \"";
			u32 number = 1;
			for (std::string_view rest = text; !rest.empty(); ++number) {
				const size_t newline = rest.find('\n');
				const std::string_view line = rest.substr(0, newline);
				rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
				const size_t at = line.find(INCLUDE);
				const size_t close = at == std::string_view::npos ? at : line.find('"', at + INCLUDE.size());
				if (close != std::string_view::npos) {
					const std::filesystem::path named(line.substr(at + INCLUDE.size(), close - at - INCLUDE.size()));
					includes.emplace_back(PathKey((named.is_absolute() ? named : directory / named).generic_string()), number);
				}
			}
			u32 added = 0;
			u32 includedAt = 0;
			constexpr std::string_view INCLUDED = "In file included from ";
			constexpr std::string_view ERRORS[] = {": fatal error: ", ": error: "};
			while (!messages.empty()) {
				const size_t newline = messages.find('\n');
				std::string_view line = messages.substr(0, newline);
				messages = newline == std::string_view::npos ? std::string_view{} : messages.substr(newline + 1);
				if (!line.empty() && line.back() == '\r') {
					line.remove_suffix(1);
				}
				std::string_view file;
				u32 number = 0;
				if (line.starts_with(INCLUDED)) {
					std::string_view place = line.substr(INCLUDED.size());
					if (!place.empty() && place.back() == ':') {
						place.remove_suffix(1);
					}
					if (SplitPlace(place, file, number) && PathKey(file) == generated) {
						includedAt = number;
					}
					continue;
				}
				size_t at = std::string_view::npos;
				std::string_view severity;
				for (std::string_view marker : ERRORS) {
					at = line.find(marker);
					if (at != std::string_view::npos) {
						severity = marker;
						break;
					}
				}
				if (at == std::string_view::npos) {
					if (line.find(": warning: ") != std::string_view::npos || line.find(": note: ") != std::string_view::npos) {
						includedAt = 0;
					}
					continue;
				}
				const std::string_view message = line.substr(at + severity.size());
				u32 place = 0;
				if (SplitPlace(line.substr(0, at), file, number)) {
					const str key = PathKey(file);
					place = key == generated ? number : includedAt;
					for (const auto& [included, includedLine] : includes) {
						place = place == 0 && included == key ? includedLine : place;
					}
				}
				includedAt = 0;
				if (place == 0 || place > lines.size() || lines[place - 1] == 0) {
					continue;
				}
				GraphDiagnostic diagnostic{lines[place - 1], "does not compile: " + str(message)};
				const bool said = std::any_of(out.begin(), out.end(), [&](const GraphDiagnostic& held) {
					return held.node == diagnostic.node && held.message == diagnostic.message;
				});
				if (!said) {
					out.push_back(std::move(diagnostic));
					++added;
				}
			}
			return added;
		}

		void Emit(const GraphProgram& program, GraphStageMask stage, GraphHLSLText& out, const GraphHLSLHelpers* alongside) {
			out.functions.clear();
			out.helpers = GraphHLSLHelpers{};
			out.body.clear();
			out.lineNodes.clear();
			out.functionLineNodes.clear();
			out.roots.clear();
			Emitter emitter(program, out, alongside);
			emitter.Run(stage);
		}

		str FloatLiteral(f32 value) {
			str literal = BareLiteral(value);
			return literal[0] == '-' ? "(" + literal + ")" : literal;
		}

	} // namespace GraphHLSL

} // namespace Shalltear
