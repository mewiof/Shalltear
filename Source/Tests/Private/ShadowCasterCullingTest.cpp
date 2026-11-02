#include "TestsPCH.h"

#include "Graphics/ShadowCascades.h"
#include "Graphics/ShadowCasterCulling.h"

#include "CookHarness.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

using namespace Shalltear;
using Shalltear::GraphicsInternal::BuildCasterVolume;
using Shalltear::GraphicsInternal::CascadeReceiverDepths;
using Shalltear::GraphicsInternal::CascadeSplits;
using Shalltear::GraphicsInternal::CullVolume;
using Shalltear::GraphicsInternal::SolveCascadeSplits;

namespace {

	Mat4 CameraView() {
		return Mat4::LookAt(Vec3::Zero(), Vec3(0.0f, 0.0f, 1.0f));
	}

	Mat4 CameraProjection() {
		return Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	}

	Mat4 OpenMap() {
		return Mat4::OrthographicReverseZ(-2000.0f, 2000.0f, -2000.0f, 2000.0f, -2000.0f, 2000.0f);
	}

	bool Holds(const CullVolume& volume, const Vec3& point) {
		return volume.Intersects(AABB(point, point));
	}

	CullVolume SunFromAbove(f32 margin) {
		return BuildCasterVolume(OpenMap(), CameraView(), CameraProjection(), Vec2(1.0f, 10.0f), Vec4(0.0f, 1.0f, 0.0f, 0.0f), margin);
	}

} // namespace

TEST(ShadowCasterCullingTest, SunExtrudesTowardLight) {
	const CullVolume volume = SunFromAbove(0.1f);
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 0.0f, 5.0f)));
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 50.0f, 5.0f)));
	EXPECT_TRUE(Holds(volume, Vec3(2.0f, 300.0f, 8.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, -50.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(20.0f, 0.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 30.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 50.0f, 30.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 0.5f)));
}

TEST(ShadowCasterCullingTest, MarginWidensVolume) {
	const f32 margin = 0.5f;
	const CullVolume volume = SunFromAbove(margin);
	const f32 cos30 = std::cos(30.0f * Math::DEG_TO_RAD);
	auto rightOf = [&](f32 outside) {
		return Vec3((2.5f + outside) / cos30, 0.0f, 5.0f);
	};
	auto below = [&](f32 outside) {
		return Vec3(0.0f, -(2.5f + outside) / cos30, 5.0f);
	};
	EXPECT_TRUE(Holds(volume, rightOf(0.5f * margin)));
	EXPECT_FALSE(Holds(volume, rightOf(2.0f * margin)));
	EXPECT_TRUE(Holds(volume, below(0.5f * margin)));
	EXPECT_FALSE(Holds(volume, below(2.0f * margin)));
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 0.0f, 10.0f + 0.5f * margin)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 10.0f + 2.0f * margin)));
}

TEST(ShadowCasterCullingTest, LampHullSpansLightAndReceivers) {
	const Vec3 lamp(0.0f, 8.0f, 5.0f);
	const CullVolume volume =
		BuildCasterVolume(OpenMap(), CameraView(), CameraProjection(), Vec2(1.0f, 10.0f), Vec4(lamp, 1.0f), 0.1f);
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 4.0f, 5.0f)));
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 7.5f, 5.0f)));
	EXPECT_TRUE(Holds(volume, (lamp + Vec3(5.7f, 5.7f, 10.0f)) * 0.5f));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 20.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, -20.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(10.0f, 8.0f, 5.0f)));
}

TEST(ShadowCasterCullingTest, LampAmongReceiversKeepsThem) {
	const CullVolume volume = BuildCasterVolume(OpenMap(), CameraView(), CameraProjection(), Vec2(1.0f, 10.0f),
												Vec4(0.0f, 0.0f, 5.0f, 1.0f), 0.1f);
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 0.0f, 5.0f)));
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 2.0f, 8.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 10.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 20.0f)));
}

TEST(ShadowCasterCullingTest, MapPlanesStillApply) {
	const Mat4 map = Mat4::OrthographicReverseZ(-3.0f, 3.0f, -3.0f, 3.0f, -200.0f, 3.0f);
	const CullVolume volume =
		BuildCasterVolume(map, CameraView(), CameraProjection(), Vec2(1.0f, 10.0f), Vec4(0.0f, 1.0f, 0.0f, 0.0f), 0.1f);
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 0.0f, 2.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 5.0f)));
}

TEST(ShadowCasterCullingTest, NoReceiversRejectsAll) {
	const CullVolume volume = BuildCasterVolume(OpenMap(), CameraView(), CameraProjection(), Vec2(200.0f, 300.0f),
												Vec4(0.0f, 1.0f, 0.0f, 0.0f), 0.1f);
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 5.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, 250.0f)));
	EXPECT_FALSE(volume.Intersects(AABB(Vec3(-500.0f), Vec3(500.0f))));
}

TEST(ShadowCasterCullingTest, OrthographicKeepsReceiversBehindEye) {
	const Mat4 projection = Mat4::OrthographicReverseZ(-10.0f, 10.0f, -10.0f, 10.0f, -500.0f, 500.0f);
	const CullVolume volume =
		BuildCasterVolume(OpenMap(), CameraView(), projection, Vec2(-std::numeric_limits<f32>::infinity(), 20.0f),
						  Vec4(0.0f, 1.0f, 0.0f, 0.0f), 0.1f);
	EXPECT_TRUE(Holds(volume, Vec3(0.0f, 0.0f, -100.0f)));
	EXPECT_TRUE(Holds(volume, Vec3(9.0f, 40.0f, -480.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, 0.0f, -600.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(30.0f, 0.0f, 0.0f)));
	EXPECT_FALSE(Holds(volume, Vec3(0.0f, -30.0f, 0.0f)));
}

TEST(ShadowCasterCullingTest, RelevantCastersAlwaysKept) {
	std::mt19937 random(0x5eedu);
	auto uniform = [&](f32 low, f32 high) {
		return std::uniform_real_distribution<f32>(low, high)(random);
	};
	auto onSphere = [&]() {
		Vec3 v;
		do {
			v = Vec3(uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f));
		} while (v.LengthSquared() < 1e-3f || v.LengthSquared() > 1.0f);
		return v.Normalized();
	};

	u32 checked = 0;
	for (u32 config = 0; config < 300; ++config) {
		const Vec3 eye(uniform(-50.0f, 50.0f), uniform(-50.0f, 50.0f), uniform(-50.0f, 50.0f));
		const Mat4 view = Mat4::LookAt(eye, eye + onSphere());
		const bool orthographic = config % 4 == 3;
		const f32 nearPlane = orthographic ? uniform(-200.0f, 0.0f) : uniform(0.05f, 1.0f);
		const f32 farPlane = uniform(50.0f, 400.0f);
		const f32 tanHalfY = std::tan(uniform(10.0f, 60.0f) * Math::DEG_TO_RAD);
		const f32 aspect = uniform(0.5f, 2.5f);
		const f32 halfHeight = uniform(2.0f, 40.0f);
		const Mat4 projection = orthographic
									? Mat4::OrthographicReverseZ(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, nearPlane, farPlane)
									: Mat4::PerspectiveReverseZ(2.0f * std::atan(tanHalfY) / Math::DEG_TO_RAD, aspect, nearPlane, farPlane);
		const f32 sliceNear = uniform(nearPlane, farPlane * 0.5f);
		const f32 sliceFar = uniform(sliceNear + 0.5f, farPlane);
		const Vec2 depths(config % 5 == 0 ? -std::numeric_limits<f32>::infinity() : sliceNear, sliceFar);
		const bool directional = config % 2 == 0;
		const Vec3 lamp = eye + onSphere() * uniform(0.0f, 120.0f);
		const Vec4 light = directional ? Vec4(onSphere(), 0.0f) : Vec4(lamp, 1.0f);
		const f32 margin = uniform(0.01f, 1.0f);
		const CullVolume volume = BuildCasterVolume(OpenMap(), view, projection, depths, light, margin);

		const Mat4 inverseView = view.Inverse();
		const f32 lowest = Math::Max(depths.x, nearPlane);
		for (u32 sample = 0; sample < 40; ++sample) {
			const f32 depth = uniform(lowest, sliceFar);
			const f32 spread = orthographic ? 1.0f : depth;
			const f32 halfY = orthographic ? halfHeight : tanHalfY;
			const Vec4 local(uniform(-1.0f, 1.0f) * halfY * aspect * spread, uniform(-1.0f, 1.0f) * halfY * spread, depth, 1.0f);
			const Vec3 receiver = (inverseView * local).XYZ();
			const Vec3 jitter = onSphere() * (margin * uniform(0.0f, 0.9f));
			const Vec3 caster = directional ? receiver + light.XYZ() * uniform(0.0f, 300.0f)
											: receiver + (lamp - receiver) * uniform(0.0f, 1.0f);
			ASSERT_TRUE(Holds(volume, caster + jitter)) << "config " << config << " sample " << sample;
			++checked;
		}
	}
	EXPECT_EQ(checked, 300u * 40u);
}

TEST(ShadowCasterCullingTest, BlendBandReachesNextCascade) {
	const std::filesystem::path shader = CookHarness::FindRepoDir("EngineAssets/Shaders/Shadows.hlsli");
	ASSERT_FALSE(shader.empty());
	std::ifstream file(shader);
	std::stringstream text;
	text << file.rdbuf();
	const std::string source = text.str();
	const std::string define = "#define SHALLTEAR_CASCADE_BLEND_FRACTION ";
	const size_t at = source.find(define);
	ASSERT_NE(at, std::string::npos);
	const f32 fraction = std::stof(source.substr(at + define.size()));
	ASSERT_GT(fraction, 0.0f);

	const CascadeSplits splits = SolveCascadeSplits(0.1f, 150.0f, 4, 6.0f);
	ASSERT_EQ(splits.count, 4u);
	const Vec2 first = CascadeReceiverDepths(splits, 0);
	EXPECT_TRUE(std::isinf(first.x) && first.x < 0.0f);
	EXPECT_EQ(first.y, splits.depths[1]);
	for (u32 i = 1; i < splits.count; ++i) {
		const f32 previousNear = i == 1 ? 0.0f : splits.depths[i - 1];
		const Vec2 depths = CascadeReceiverDepths(splits, i);
		EXPECT_FLOAT_EQ(depths.x, splits.depths[i] - (splits.depths[i] - previousNear) * fraction) << i;
		EXPECT_EQ(depths.y, splits.depths[i + 1]) << i;
	}
}
