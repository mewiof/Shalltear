#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/PostProcessing/LensFlare.h"
#include "Shalltear/PostProcessing/LensFlareAsset.h"
#include "Shalltear/PostProcessing/LensFlarePass.h"

#include "LensFlareOps.h"
#include "LensFlareShader.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace Shalltear;

SLT_DECLARE_LINKED_PACKAGE(PostProcessing)

namespace {

	namespace Ops = Shalltear::LensFlareOps;

	constexpr f32 FOV = 60.0f;
	constexpr f32 NEAR_PLANE = 0.1f;
	constexpr f32 FAR_PLANE = 500.0f;

	Ops::ViewInput ForwardView(f32 aspect = 1.0f) {
		Ops::ViewInput view;
		view.cameraPosition = Vec3(0.0f, 0.0f, 0.0f);
		view.view = Mat4::LookAt(view.cameraPosition, Vec3(0.0f, 0.0f, 1.0f));
		view.projection = Mat4::PerspectiveReverseZ(FOV, aspect, NEAR_PLANE, FAR_PLANE);
		view.extent = Vec2I(static_cast<i32>(128.0f * aspect), 128);
		return view;
	}

	Ops::ProjectedFlare Centered() {
		Ops::ProjectedFlare flare;
		flare.screen = Vec2(0.5f, 0.25f);
		return flare;
	}

	LensFlareElement Circle() {
		LensFlareElement element;
		element.shape = LensFlareShape::Circle;
		return element;
	}

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 8 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

} // namespace

TEST(LensFlareOpsTest, PointProjectsThroughCamera) {
	Ops::FlareInput input;
	input.position = Vec3(1.0f, 0.5f, 10.0f);
	Ops::ProjectedFlare out;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));

	const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
	EXPECT_NEAR(out.screen.x, focal * 1.0f / 10.0f, 1e-4f);
	EXPECT_NEAR(out.screen.y, focal * 0.5f / 10.0f, 1e-4f);
	EXPECT_EQ(out.depthSign, 1.0f);
	EXPECT_GT(out.depth, 0.0f);
	EXPECT_EQ(out.samples, 0u);

	input.position = Vec3(0.0f, 0.0f, -10.0f);
	EXPECT_FALSE(Ops::Project(ForwardView(), input, out));
}

TEST(LensFlareOpsTest, OcclusionPlaneMovesTowardCamera) {
	Ops::FlareInput input;
	input.position = Vec3(0.0f, 0.0f, 10.0f);
	input.occlusion = true;
	input.occlusionOffset = 0.0f;
	Ops::ProjectedFlare atLight;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, atLight));
	input.occlusionOffset = 2.0f;
	Ops::ProjectedFlare nearer;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, nearer));

	const Mat4 projection = Mat4::PerspectiveReverseZ(FOV, 1.0f, NEAR_PLANE, FAR_PLANE);
	auto depthAt = [&](f32 z) { return (projection(2, 2) * z + projection(2, 3)) / z; };
	EXPECT_NEAR(atLight.depth, depthAt(10.0f), 1e-6f);
	EXPECT_NEAR(nearer.depth, depthAt(8.0f), 1e-6f);
	EXPECT_EQ(nearer.samples, 32u);

	const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
	EXPECT_NEAR(atLight.occlusionRadius.y, 0.1f * focal / 10.0f, 1e-5f);
}

TEST(LensFlareOpsTest, DirectionalFollowsRotationNotPosition) {
	Ops::FlareInput input;
	input.hasLight = true;
	input.lightType = LightType::Directional;
	input.forward = Vec3(0.0f, 0.0f, -1.0f);
	input.position = Vec3(40.0f, -7.0f, 3.0f);
	Ops::ProjectedFlare out;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.screen.x, 0.0f, 1e-5f);
	EXPECT_NEAR(out.screen.y, 0.0f, 1e-5f);
	EXPECT_EQ(out.depth, 0.0f);
	EXPECT_NEAR(out.fade, 1.0f, 1e-5f);

	input.forward = Vec3(0.0f, 0.0f, 1.0f);
	EXPECT_FALSE(Ops::Project(ForwardView(), input, out));

	input.forward = Vec3(-0.2f, 0.0f, -1.0f).Normalized();
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
	EXPECT_NEAR(out.screen.x, focal * 0.2f, 1e-4f);
	EXPECT_NEAR(out.fade, Vec3(0.2f, 0.0f, 1.0f).Normalized().z, 1e-4f);

	input.occlusionRadius = 1.0f;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.occlusionRadius.y, std::tan(Math::DEG_TO_RAD) * focal / Vec3(0.2f, 0.0f, 1.0f).Normalized().z, 1e-4f);
}

TEST(LensFlareOpsTest, OffScreenHiddenUnlessAllowed) {
	Ops::FlareInput input;
	input.position = Vec3(20.0f, 0.0f, 10.0f);
	Ops::ProjectedFlare out;
	EXPECT_FALSE(Ops::Project(ForwardView(), input, out));
	input.allowOffScreen = true;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_GT(out.screen.x, 1.0f);
	EXPECT_TRUE(out.offScreenVisible);
}

TEST(LensFlareOpsTest, DistanceCurvesFadeAndShrink) {
	const AnimationCurve linear{{Keyframe{.time = 0.0f, .value = 1.0f, .inTangent = -1.0f, .outTangent = -1.0f},
								 Keyframe{.time = 1.0f, .value = 0.0f, .inTangent = -1.0f, .outTangent = -1.0f}}};
	Ops::FlareInput input;
	input.position = Vec3(0.0f, 0.0f, 25.0f);
	input.attenuationDistance = 100.0f;
	input.attenuationDistanceCurve = &linear;
	input.scaleDistance = 50.0f;
	input.scaleDistanceCurve = &linear;
	Ops::ProjectedFlare out;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.fade, 0.75f, 1e-3f);
	EXPECT_NEAR(out.scale, 0.5f, 1e-3f);

	input.position = Vec3(0.0f, 0.0f, 120.0f);
	EXPECT_FALSE(Ops::Project(ForwardView(), input, out));
}

TEST(LensFlareOpsTest, SpotSeenFromBehindIsHidden) {
	Ops::FlareInput input;
	input.hasLight = true;
	input.lightType = LightType::Spot;
	input.spotAngleDeg = 60.0f;
	input.position = Vec3(0.0f, 0.0f, 10.0f);
	input.forward = Vec3(0.0f, 0.0f, -1.0f);
	Ops::ProjectedFlare out;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.fade, 1.0f, 1e-4f);

	input.forward = Vec3(0.0f, 0.0f, 1.0f);
	EXPECT_FALSE(Ops::Project(ForwardView(), input, out));

	input.attenuationByLightShape = false;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.fade, 1.0f, 1e-4f);
}

TEST(LensFlareOpsTest, ScreenCurveReadsMaxAxis) {
	const AnimationCurve edge{{Keyframe{.time = 0.0f, .value = 1.0f, .inTangent = -1.0f, .outTangent = -1.0f},
							   Keyframe{.time = 1.0f, .value = 0.0f, .inTangent = -1.0f, .outTangent = -1.0f}}};
	Ops::FlareInput input;
	const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
	input.position = Vec3(0.6f * 10.0f / focal, 0.2f * 10.0f / focal, 10.0f);
	input.screenAttenuationCurve = &edge;
	Ops::ProjectedFlare out;
	ASSERT_TRUE(Ops::Project(ForwardView(), input, out));
	EXPECT_NEAR(out.fade, 0.4f, 1e-3f);
}

TEST(LensFlareOpsTest, ElementsRideLightToCenterAxis) {
	const Ops::ProjectedFlare flare = Centered();
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	for (f32 position : {0.0f, 1.0f, 2.0f}) {
		LensFlareElement element = Circle();
		element.position = position;
		quads.clear();
		Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
		ASSERT_EQ(quads.size(), 1u);
		EXPECT_NEAR(quads[0].center.x, 0.5f * (1.0f - position), 1e-5f);
		EXPECT_NEAR(quads[0].center.y, 0.25f * (1.0f - position), 1e-5f);
	}

	LensFlareElement horizontal = Circle();
	horizontal.position = 2.0f;
	horizontal.translationScale = Vec2(1.0f, 0.0f);
	horizontal.positionOffset = Vec2(0.0f, -0.1f);
	quads.clear();
	Ops::ExpandElement(horizontal, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].center.x, -0.5f, 1e-5f);
	EXPECT_NEAR(quads[0].center.y, 0.15f, 1e-5f);
}

TEST(LensFlareOpsTest, AngularOffsetTurnsRayInPixels) {
	Ops::ProjectedFlare flare;
	flare.screen = Vec2(0.5f, 0.0f);
	LensFlareElement element = Circle();
	element.position = 1.0f;
	element.angularOffset = 90.0f;
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 2.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].center.x, 0.5f, 1e-5f);
	EXPECT_NEAR(quads[0].center.y, 1.0f, 1e-5f);
}

TEST(LensFlareOpsTest, SizeUnitSpansTenthOfHeight) {
	Ops::ProjectedFlare flare = Centered();
	flare.scale = 2.0f;
	LensFlareElement element;
	element.shape = LensFlareShape::Image;
	element.size = Vec2(1.0f, 1.5f);
	element.scale = 0.5f;
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 2.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, 0.1f, 1e-6f);
	EXPECT_NEAR(quads[0].halfSize.y, 0.15f, 1e-6f);

	element.preserveAspectRatio = true;
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 2.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, 0.3f, 1e-6f);
	EXPECT_NEAR(quads[0].halfSize.y, 0.15f, 1e-6f);
}

TEST(LensFlareOpsTest, ColorTakesIntensityLightAndFade) {
	Ops::ProjectedFlare flare = Centered();
	flare.intensity = 2.0f;
	flare.fade = 0.5f;
	flare.hasLight = true;
	flare.lightColor = Color(1.0f, 0.5f, 0.25f, 1.0f);
	LensFlareElement element = Circle();
	element.tint = Color(0.5f, 1.0f, 1.0f, 0.8f);
	element.intensity = 3.0f;
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].color.r, 0.5f * 6.0f * 1.0f, 1e-5f);
	EXPECT_NEAR(quads[0].color.g, 1.0f * 6.0f * 0.5f, 1e-5f);
	EXPECT_NEAR(quads[0].color.b, 1.0f * 6.0f * 0.25f, 1e-5f);
	EXPECT_NEAR(quads[0].color.a, 0.8f * 0.5f, 1e-5f);

	element.modulateByLightColor = false;
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].color.b, 6.0f, 1e-5f);
}

TEST(LensFlareOpsTest, UniformCopiesSpanTwiceSpread) {
	const Ops::ProjectedFlare flare = Centered();
	LensFlareElement element = Circle();
	element.multiple = true;
	element.count = 5;
	element.lengthSpread = 1.0f;
	element.rotationStep = 10.0f;
	element.colors = Gradient({GradientColorKey{Color::Black(), 0.0f}, GradientColorKey{Color::White(), 1.0f}},
							  {GradientAlphaKey{1.0f, 0.0f}, GradientAlphaKey{1.0f, 1.0f}});
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 5u);
	for (u32 i = 0; i < 5; ++i) {
		const f32 position = 0.5f * static_cast<f32>(i);
		EXPECT_NEAR(quads[i].center.x, 0.5f * (1.0f - position), 1e-5f);
		EXPECT_NEAR(quads[i].rotation, 10.0f * static_cast<f32>(i) * Math::DEG_TO_RAD, 1e-5f);
		EXPECT_NEAR(quads[i].color.g, element.colors.Evaluate(static_cast<f32>(i) / 4.0f).g, 1e-5f);
	}
}

TEST(LensFlareOpsTest, CurveCopiesReadTheirCurves) {
	const Ops::ProjectedFlare flare = Centered();
	LensFlareElement element = Circle();
	element.multiple = true;
	element.count = 3;
	element.distribution = LensFlareDistribution::Curve;
	element.lengthSpread = 0.5f;
	element.scaleCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 2.0f}, Keyframe{.time = 1.0f, .value = 2.0f}}};
	element.rotationCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 0.5f}, Keyframe{.time = 1.0f, .value = 0.5f}}};
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 3u);
	EXPECT_NEAR(quads[1].center.x, 0.5f * (1.0f - element.positionCurve.Evaluate(0.5f)), 1e-4f);
	EXPECT_NEAR(quads[2].center.x, 0.0f, 1e-4f);
	EXPECT_NEAR(quads[0].halfSize.y, 2.0f * Ops::SIZE_UNIT, 1e-5f);
	EXPECT_NEAR(quads[0].rotation, 0.5f * Math::PI, 1e-5f);
}

TEST(LensFlareOpsTest, RandomCopiesRepeatPerSeed) {
	const Ops::ProjectedFlare flare = Centered();
	LensFlareElement element = Circle();
	element.multiple = true;
	element.count = 8;
	element.distribution = LensFlareDistribution::Random;
	element.intensityVariation = 0.5f;
	Vector<Ops::Quad> first{GetMemoryResource(MemTag::Debug)};
	Vector<Ops::Quad> again{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, first);
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, again);
	ASSERT_EQ(first.size(), 8u);
	ASSERT_EQ(again.size(), first.size());
	bool varied = false;
	for (size_t i = 0; i < first.size(); ++i) {
		EXPECT_EQ(first[i].center, again[i].center);
		EXPECT_EQ(first[i].halfSize, again[i].halfSize);
		EXPECT_EQ(first[i].rotation, again[i].rotation);
		varied = varied || first[i].halfSize != first[0].halfSize;
	}
	EXPECT_TRUE(varied);

	element.seed = 7;
	Vector<Ops::Quad> other{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, other);
	ASSERT_FALSE(other.empty());
	EXPECT_NE(other[0].rotation, first[0].rotation);
}

TEST(LensFlareOpsTest, AutoRotateFacesLight) {
	Ops::ProjectedFlare flare;
	flare.screen = Vec2(0.0f, 0.5f);
	LensFlareElement element = Circle();
	element.autoRotate = true;
	element.rotation = 15.0f;
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].rotation, 15.0f * Math::DEG_TO_RAD - 0.5f * Math::PI, 1e-5f);
}

TEST(LensFlareOpsTest, RadialDistortionGrowsAwayFromFlare) {
	Ops::ProjectedFlare flare;
	flare.screen = Vec2(0.6f, 0.0f);
	LensFlareElement element = Circle();
	element.radialDistortion = true;
	element.distortionTargetSize = Vec2(4.0f, 2.0f);
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	for (f32 position : {0.0f, 1.0f, 2.0f}) {
		element.position = position;
		quads.clear();
		Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
		ASSERT_EQ(quads.size(), 1u);
		const f32 blend = 0.5f * 0.6f * position;
		EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT * (1.0f + 3.0f * blend), 1e-5f) << position;
		EXPECT_NEAR(quads[0].halfSize.y, Ops::SIZE_UNIT * (1.0f + 1.0f * blend), 1e-5f) << position;
	}

	element.position = 2.0f;
	element.distortionCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 0.0f}, Keyframe{.time = 0.5f, .value = 1.0f}}};
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT * 4.0f, 1e-5f);

	element.radialDistortion = false;
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT, 1e-6f);
}

TEST(LensFlareOpsTest, CenterRelativeDistortionReadsScreenRadius) {
	Ops::ProjectedFlare flare;
	flare.screen = Vec2(0.6f, 0.8f);
	LensFlareElement element = Circle();
	element.radialDistortion = true;
	element.distortionRelativeToCenter = true;
	element.distortionTargetSize = Vec2(3.0f, 3.0f);
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT * 3.0f, 1e-5f);

	element.position = 1.0f;
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT, 1e-5f);

	element.position = 0.5f;
	quads.clear();
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].halfSize.x, Ops::SIZE_UNIT * 2.0f, 1e-5f);
}

TEST(LensFlareOpsTest, CopyScaleAppliesAfterDistortion) {
	Ops::ProjectedFlare flare;
	flare.screen = Vec2(0.8f, 0.0f);
	LensFlareElement element = Circle();
	element.multiple = true;
	element.count = 2;
	element.distribution = LensFlareDistribution::Curve;
	element.lengthSpread = 0.5f;
	element.positionCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 0.0f, .inTangent = 1.0f, .outTangent = 1.0f},
											Keyframe{.time = 1.0f, .value = 1.0f, .inTangent = 1.0f, .outTangent = 1.0f}}};
	element.scaleCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 2.0f}, Keyframe{.time = 1.0f, .value = 2.0f}}};
	element.radialDistortion = true;
	element.distortionTargetSize = Vec2(5.0f, 5.0f);
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, flare, 1.0f, 0.0f, quads);
	ASSERT_EQ(quads.size(), 2u);
	EXPECT_NEAR(quads[0].halfSize.x, 2.0f * Ops::SIZE_UNIT, 1e-5f);
	const f32 blend = 0.5f * 0.8f * 1.0f;
	EXPECT_NEAR(quads[1].halfSize.x, 2.0f * Ops::SIZE_UNIT * (1.0f + 4.0f * blend), 1e-4f);
}

TEST(LensFlareOpsTest, DisabledElementDrawsNothing) {
	LensFlareElement element = Circle();
	element.enabled = false;
	Vector<Ops::Quad> quads{GetMemoryResource(MemTag::Debug)};
	Ops::ExpandElement(element, Centered(), 1.0f, 0.0f, quads);
	EXPECT_TRUE(quads.empty());
}

TEST(LensFlareOpsTest, OcclusionSamplesFillUnitDisk) {
	constexpr u32 COUNT = 32;
	Vec2 mean{};
	u32 inner = 0;
	for (u32 i = 0; i < COUNT; ++i) {
		const Vec2 sample = Ops::OcclusionSample(i, COUNT);
		EXPECT_LE(sample.Length(), 1.0f);
		mean = mean + sample;
		if (sample.Length() < std::sqrt(0.5f)) {
			++inner;
		}
	}
	EXPECT_NEAR(mean.x / COUNT, 0.0f, 0.05f);
	EXPECT_NEAR(mean.y / COUNT, 0.0f, 0.05f);
	EXPECT_EQ(inner, COUNT / 2);
}

TEST(LensFlareOpsTest, FalloffOneIsLinear) {
	EXPECT_EQ(Ops::FalloffExponent(1.0f), 1.0f);
	EXPECT_EQ(Ops::FalloffExponent(0.0f), 16.0f);
}

TEST(LensFlareRowTest, RowSitsBeforePost) {
	LensFlarePass pass;
	EXPECT_EQ(pass.point, PassPoint::BeforePost);
	EXPECT_EQ(pass.GetAssetTypeId(), StringHash("LensFlarePass"));
}

TEST(LensFlareRowTest, CookNotesShader) {
	LensFlarePass pass;
	PakStringPool strings;
	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Debug)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Debug)};
	CookWriter descriptor(descriptorBytes, strings);
	CookWriter payload(payloadBytes, strings);
	ASSERT_TRUE(pass.Cook(descriptor, payload));
	ASSERT_EQ(descriptor.GetReferences().size(), 1u);
	EXPECT_EQ(descriptor.GetReferences()[0], LensFlareImpl::LensFlareShader);
}

TEST(LensFlareRowTest, CommittedSidecarCarriesShaderGuid) {
	std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
	if (engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	const std::filesystem::path meta = engineAssets / "Shaders/LensFlare.hlsl.meta";
	str text = FileSystem::ReadTextFile(Path(meta.generic_string()), MemTag::Asset);
	ASSERT_FALSE(text.empty());
	LinearAllocator arena(text.size() + 64, MemTag::Asset);
	JSONValue root = JSONParse(text, arena);
	EXPECT_EQ(Guid::FromString(root["assetId"].AsString()), LensFlareImpl::LensFlareShader);
}

TEST(LensFlareRowTest, AssetRoundTripsThroughItsDocument) {
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "shalltear_lensflare_roundtrip";
	std::filesystem::create_directories(dir);
	const Path file((dir / "Sun.slflare").generic_string());

	LensFlareAsset written;
	LensFlareElement& ring = written.elements.emplace_back();
	ring.shape = LensFlareShape::Ring;
	ring.ringThickness = 0.3f;
	ring.colorMode = LensFlareColorMode::Radial;
	ring.tintGradient = Gradient({GradientColorKey{Color(1.0f, 0.0f, 0.0f), 0.0f}, GradientColorKey{Color(0.0f, 0.0f, 1.0f), 1.0f}},
								 {GradientAlphaKey{1.0f, 0.0f}, GradientAlphaKey{0.5f, 1.0f}});
	ring.position = 1.5f;
	ring.translationScale = Vec2(1.0f, 0.25f);
	ring.blend = LensFlareBlend::Lerp;
	LensFlareElement& ghosts = written.elements.emplace_back();
	ghosts.shape = LensFlareShape::Polygon;
	ghosts.sideCount = 5;
	ghosts.multiple = true;
	ghosts.count = 7;
	ghosts.distribution = LensFlareDistribution::Curve;
	ghosts.scaleCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 0.5f}, Keyframe{.time = 1.0f, .value = 3.0f}}};
	ghosts.seed = 42;
	ghosts.radialDistortion = true;
	ghosts.distortionTargetSize = Vec2(2.5f, 0.5f);
	ghosts.distortionCurve = AnimationCurve{{Keyframe{.time = 0.0f, .value = 0.25f}, Keyframe{.time = 1.0f, .value = 0.75f}}};
	ghosts.distortionRelativeToCenter = true;
	ASSERT_TRUE(written.Export(file));

	LensFlareAsset read;
	ASSERT_TRUE(read.Import(file));
	ASSERT_EQ(read.elements.size(), 2u);
	EXPECT_EQ(read.elements[0].shape, LensFlareShape::Ring);
	EXPECT_NEAR(read.elements[0].ringThickness, 0.3f, 1e-5f);
	EXPECT_EQ(read.elements[0].colorMode, LensFlareColorMode::Radial);
	EXPECT_EQ(read.elements[0].tintGradient, written.elements[0].tintGradient);
	EXPECT_NEAR(read.elements[0].position, 1.5f, 1e-5f);
	EXPECT_NEAR(read.elements[0].translationScale.y, 0.25f, 1e-5f);
	EXPECT_EQ(read.elements[0].blend, LensFlareBlend::Lerp);
	EXPECT_EQ(read.elements[1].shape, LensFlareShape::Polygon);
	EXPECT_EQ(read.elements[1].sideCount, 5);
	EXPECT_TRUE(read.elements[1].multiple);
	EXPECT_EQ(read.elements[1].count, 7);
	EXPECT_EQ(read.elements[1].distribution, LensFlareDistribution::Curve);
	EXPECT_TRUE(read.elements[1].radialDistortion);
	EXPECT_NEAR(read.elements[1].distortionTargetSize.x, 2.5f, 1e-5f);
	EXPECT_NEAR(read.elements[1].distortionCurve.Evaluate(1.0f), 0.75f, 1e-5f);
	EXPECT_TRUE(read.elements[1].distortionRelativeToCenter);
	EXPECT_FALSE(read.elements[0].radialDistortion);
	EXPECT_EQ(read.elements[1].scaleCurve, written.elements[1].scaleCurve);
	EXPECT_EQ(read.elements[1].seed, 42);

	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

namespace {

	class LensFlareLifetimeFixture : public ::testing::Test {
	protected:
		Handle<Scene> _scene;
		LensFlareAsset _asset;

		void SetUp() override {
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("LensFlareLifetime");
			_asset.elements.emplace_back();
		}

		void TearDown() override { SceneSystem::Terminate(); }
	};

} // namespace

TEST_F(LensFlareLifetimeFixture, FlareLeavesLiveList) {
	const u32 before = LensFlare::GetEnabledCount();
	Handle<SceneObject> object = SceneSystem::CreateObject("Flare", _scene);
	Handle<LensFlare> flare = object->AddComponent<LensFlare>();
	ASSERT_TRUE(flare.Get());
	EXPECT_EQ(LensFlare::GetEnabledCount(), before + 1);

	flare->SetEnabled(false);
	EXPECT_EQ(LensFlare::GetEnabledCount(), before);
	flare->SetEnabled(true);
	object->SetActive(false);
	EXPECT_EQ(LensFlare::GetEnabledCount(), before);
	object->SetActive(true);
	EXPECT_EQ(LensFlare::GetEnabledCount(), before + 1);

	SceneSystem::Destroy(object);
	SceneSystem::EndFrame();
	EXPECT_EQ(LensFlare::GetEnabledCount(), before);
}

TEST_F(LensFlareLifetimeFixture, DepthAskedOnlyForOcclusion) {
	LensFlarePass pass;
	EXPECT_EQ(pass.GetRequests(), PassRequests::None);

	Handle<SceneObject> object = SceneSystem::CreateObject("Flare", _scene);
	Handle<LensFlare> flare = object->AddComponent<LensFlare>();
	flare->occlusion = true;
	EXPECT_EQ(pass.GetRequests(), PassRequests::None);

	flare->flare = Handle<LensFlareAsset>(&_asset);
	EXPECT_EQ(pass.GetRequests(), PassRequests::SceneDepth);

	flare->occlusion = false;
	EXPECT_EQ(pass.GetRequests(), PassRequests::None);

	flare->occlusion = true;
	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("Preview");
	ASSERT_TRUE(isolated.IsValid());
	SceneSystem::Destroy(object);
	SceneSystem::EndFrame();
	Handle<SceneObject> preview = SceneSystem::CreateObject("Flare", isolated);
	Handle<LensFlare> previewed = preview->AddComponent<LensFlare>();
	previewed->flare = Handle<LensFlareAsset>(&_asset);
	EXPECT_EQ(pass.GetRequests(), PassRequests::None);
	SceneSystem::UnloadScene(isolated);
	SceneSystem::EndFrame();
}

namespace {

	f32 ByteToLinear(u8 value) {
		const f32 encoded = static_cast<f32>(value) / 255.0f;
		return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
	}

	f32 LinearToByte(f32 linear) {
		linear = Math::Clamp01(linear);
		const f32 encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		return encoded * 255.0f;
	}

	void WriteSolidTGA(const std::filesystem::path& path, u8 r, u8 g, u8 b) {
		constexpr u16 SIDE = 8;
		std::ofstream file(path, std::ios::binary);
		const u8 header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, SIDE, 0, SIDE, 0, 24, 0};
		file.write(reinterpret_cast<const char*>(header), sizeof(header));
		for (u32 i = 0; i < SIDE * SIDE; ++i) {
			const u8 bgr[3] = {b, g, r};
			file.write(reinterpret_cast<const char*>(bgr), 3);
		}
	}

	class LensFlareRenderFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;
		bool _projectMounted = false;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{128, 128};
		Handle<Scene> _scene;
		LensFlareAsset _asset;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_lensflare_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");

			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			_projectMounted = true;

			AssetDatabase::Initialize();
			SLT_CALL_LINKED_PACKAGE(PostProcessing, &_token);
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}
			_target = RenderSystem::CreateViewTarget(_size);
			_scene = SceneSystem::CreateScene("LensFlareRender");
		}

		void TearDown() override {
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
			}
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool Ready() const { return RenderSystem::IsRenderingAvailable() && _target != INVALID_VIEW_TARGET; }

		bool CompilerAvailable() {
			FileSystem::WriteTextFile(Path((_root / "CompilerProbe.hlsl").generic_string()),
									  "struct VSOutput { float4 positionCS : SV_Position; };\n"
									  "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
									  "\tVSOutput output; output.positionCS = float4(float(vertexId), 0, 0, 1); return output;\n"
									  "}\n"
									  "float4 PSMain(VSOutput input) : SV_Target { return float4(1, 0, 1, 1); }\n");
			ShaderAsset probe;
			return probe.Import(FileSystem::Resolve(Path("project://CompilerProbe.hlsl")));
		}

		Handle<LensFlare> AddFlare(const Vec3& position, f32 intensity) {
			LensFlareElement& disc = _asset.elements.emplace_back();
			disc.shape = LensFlareShape::Circle;
			disc.gradient = 0.0f;
			disc.intensity = intensity;
			Handle<SceneObject> object = SceneSystem::CreateObject("Flare", _scene);
			object->SetLocalPosition(position);
			Handle<LensFlare> flare = object->AddComponent<LensFlare>();
			flare->flare = Handle<LensFlareAsset>(&_asset);
			flare->occlusion = false;
			SceneSystem::EndFrame();
			return flare;
		}

		bool AddOccluder(f32 rightEdgeNdc, f32 depth) {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* cube = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
			if (!unlitShader || !cube) {
				return false;
			}
			FileSystem::WriteTextFile(Path((_root / "Occluder.mat").generic_string()),
									  str("{\n\t\"shader\": \"") + unlitShader->assetId.ToString() + "\"\n}\n");
			auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Occluder.mat")));
			if (!material) {
				return false;
			}
			material->SetColor("baseColor", Color(0.05f, 0.05f, 0.05f, 1.0f));
			const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
			constexpr f32 THICKNESS = 0.02f;
			const f32 rightEdge = rightEdgeNdc * (depth - THICKNESS * 0.5f) / focal;
			Handle<SceneObject> object = SceneSystem::CreateObject("Occluder", _scene);
			object->SetLocalPosition(Vec3(rightEdge - 2.0f, 0.0f, depth));
			object->SetLocalScale(Vec3(4.0f, 4.0f, THICKNESS));
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cube)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}

		bool Render(LensFlarePass* row, Vector<u8>& outPixels, Vec2I& outSize) {
			ViewDesc desc;
			desc.cameraPosition = Vec3(0.0f, 0.0f, 0.0f);
			desc.view = Mat4::LookAt(desc.cameraPosition, Vec3(0.0f, 0.0f, 1.0f));
			desc.projection = Mat4::PerspectiveReverseZ(FOV, static_cast<f32>(_size.x) / static_cast<f32>(_size.y), NEAR_PLANE, FAR_PLANE);
			desc.clearColor = Color(0.02f, 0.02f, 0.02f);
			const Handle<RenderPass> rows[] = {Handle<RenderPass>(row)};
			if (row) {
				desc.passes = rows;
			}
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(_target, outPixels, outSize);
		}

		static const u8* Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			return pixels.data() + (static_cast<size_t>(y) * size.x + x) * 4;
		}

		static f32 DistanceFade(const LensFlare& flare, f32 distance) {
			return flare.attenuationDistanceCurve.Evaluate(distance / flare.attenuationDistance);
		}

		static f32 Added(const Vector<u8>& before, const Vector<u8>& after, Vec2I size, i32 x, i32 y, i32 channel) {
			return ByteToLinear(Texel(after, size, x, y)[channel]) - ByteToLinear(Texel(before, size, x, y)[channel]);
		}
	};

} // namespace

TEST_F(LensFlareRenderFixture, DiscAddsPredictedLight) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Handle<LensFlare> flare = AddFlare(Vec3(0.0f, 0.0f, 10.0f), 0.5f);
	const f32 added = 0.5f * DistanceFade(*flare, 10.0f);
	ASSERT_LT(added, 0.5f);

	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(nullptr, before, size));
	LensFlarePass row;
	ASSERT_TRUE(Render(&row, after, size));

	const i32 center = size.x / 2;
	for (i32 channel = 0; channel < 3; ++channel) {
		const f32 predicted = LinearToByte(ByteToLinear(Texel(before, size, center, center)[channel]) + added);
		EXPECT_NEAR(static_cast<f32>(Texel(after, size, center, center)[channel]), predicted, 2.0f) << channel;
	}
	const i32 edge = center + static_cast<i32>(Ops::SIZE_UNIT * 0.5f * static_cast<f32>(size.y)) + 3;
	EXPECT_EQ(std::memcmp(Texel(after, size, edge, center), Texel(before, size, edge, center), 4), 0);
	EXPECT_EQ(std::memcmp(Texel(after, size, 2, 2), Texel(before, size, 2, 2), 4), 0);
}

TEST_F(LensFlareRenderFixture, OccluderFadesByPredictedFraction) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	constexpr f32 EDGE_NDC = 4.0f / 64.0f;
	constexpr u32 SAMPLES = 32;
	constexpr f32 RADIUS = 1.0f;
	constexpr f32 DISTANCE = 10.0f;
	Handle<LensFlare> flare = AddFlare(Vec3(0.0f, 0.0f, DISTANCE), 0.5f);
	flare->occlusion = true;
	flare->occlusionRadius = RADIUS;
	flare->sampleCount = static_cast<i32>(SAMPLES);
	flare->occlusionOffset = 0.0f;
	ASSERT_TRUE(AddOccluder(EDGE_NDC, 5.0f));

	const f32 focal = 1.0f / std::tan(FOV * 0.5f * Math::DEG_TO_RAD);
	const f32 radiusNdc = RADIUS * focal / DISTANCE;
	u32 visible = 0;
	for (u32 i = 0; i < SAMPLES; ++i) {
		const f32 x = Ops::OcclusionSample(i, SAMPLES).x * radiusNdc;
		const i32 column = static_cast<i32>(std::floor((x * 0.5f + 0.5f) * static_cast<f32>(_size.x)));
		const f32 centerNdc = (static_cast<f32>(column) + 0.5f) / static_cast<f32>(_size.x) * 2.0f - 1.0f;
		if (centerNdc > EDGE_NDC) {
			++visible;
		}
	}
	const f32 fraction = static_cast<f32>(visible) / static_cast<f32>(SAMPLES);
	ASSERT_GT(visible, 0u);
	ASSERT_LT(visible, SAMPLES / 2);

	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> occluded{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> open{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(nullptr, before, size));
	LensFlarePass row;
	ASSERT_TRUE(Render(&row, occluded, size));
	Vector<u8> again{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(Render(&row, again, size));
	ASSERT_EQ(again.size(), occluded.size());
	EXPECT_EQ(std::memcmp(again.data(), occluded.data(), occluded.size()), 0);
	flare->occlusion = false;
	ASSERT_TRUE(Render(&row, open, size));

	const i32 x = size.x / 2;
	const i32 y = size.y / 2;
	const f32 predicted = 0.5f * DistanceFade(*flare, DISTANCE);
	const f32 full = Added(before, open, size, x, y, 1);
	const f32 faded = Added(before, occluded, size, x, y, 1);
	EXPECT_NEAR(full, predicted, 0.01f);
	EXPECT_NEAR(faded, predicted * fraction, 0.005f);
	EXPECT_NEAR(faded / full, fraction, 0.012f) << visible;
}

TEST_F(LensFlareRenderFixture, ImageElementSamplesTexture) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteSolidTGA(_root / "Ghost.tga", 255, 0, 0);
	auto* texture = static_cast<TextureAsset*>(AssetDatabase::LoadAsset(Path("project://Ghost.tga")));
	ASSERT_NE(texture, nullptr);

	LensFlareElement& image = _asset.elements.emplace_back();
	image.shape = LensFlareShape::Image;
	image.texture = Handle<TextureAsset>(texture);
	image.intensity = 0.5f;
	Handle<SceneObject> object = SceneSystem::CreateObject("Flare", _scene);
	object->SetLocalPosition(Vec3(0.0f, 0.0f, 10.0f));
	Handle<LensFlare> flare = object->AddComponent<LensFlare>();
	flare->flare = Handle<LensFlareAsset>(&_asset);
	flare->occlusion = false;
	SceneSystem::EndFrame();

	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(nullptr, before, size));
	LensFlarePass row;
	ASSERT_TRUE(Render(&row, after, size));

	const i32 center = size.x / 2;
	EXPECT_NEAR(Added(before, after, size, center, center, 0), 0.5f * DistanceFade(*flare, 10.0f), 0.02f);
	EXPECT_EQ(Texel(after, size, center, center)[1], Texel(before, size, center, center)[1]);
	EXPECT_EQ(Texel(after, size, center, center)[2], Texel(before, size, center, center)[2]);

	const Guid textureId = texture->assetId;
	AssetDatabase::UnloadAsset(texture);
	for (u32 pass = 0; pass < 4 && AssetDatabase::CollectGarbage() > 0; ++pass) {
	}
	ASSERT_EQ(AssetDatabase::FindLoadedByGuid(textureId), nullptr);
	ASSERT_EQ(image.texture.Get(), nullptr);
	Vector<u8> gone{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(Render(&row, gone, size));
	ASSERT_EQ(gone.size(), before.size());
	EXPECT_EQ(std::memcmp(gone.data(), before.data(), before.size()), 0);
}

TEST_F(LensFlareRenderFixture, NoFlareLeavesFrameIdentical) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(nullptr, before, size));
	LensFlarePass row;
	ASSERT_TRUE(Render(&row, after, size));
	ASSERT_EQ(before.size(), after.size());
	EXPECT_EQ(std::memcmp(before.data(), after.data(), before.size()), 0);

	Handle<LensFlare> behind = AddFlare(Vec3(0.0f, 0.0f, -10.0f), 1.0f);
	ASSERT_TRUE(Render(&row, after, size));
	EXPECT_EQ(std::memcmp(before.data(), after.data(), before.size()), 0);
}

#endif
