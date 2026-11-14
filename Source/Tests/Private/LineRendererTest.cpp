#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "ShadingHarness.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/LODGroup.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include "Shalltear/VisualEffects/LineRenderer.h"
#include "Shalltear/VisualEffects/TrailRenderer.h"

#include "EffectPass.h"
#include "ParticleSimulation.h"
#include "Ribbon.h"

#include <algorithm>

using namespace Shalltear;
using namespace Shalltear::VisualEffectsImpl;

SLT_DECLARE_LINKED_PACKAGE(VisualEffects)

namespace {

	using namespace ShadingTest;

	constexpr f32 EPSILON = 1e-4f;

	RibbonFacing FacingAxis(const Vec3& axis) {
		RibbonFacing facing;
		facing.axis = axis;
		return facing;
	}

	Vector<RibbonPoint> Points(std::initializer_list<Vec3> positions, f32 width, LineTextureMode mode = LineTextureMode::Stretch, bool loop = false,
							   const AnimationCurve* curve = nullptr, const Gradient* gradient = nullptr) {
		const Vector<Vec3> list(positions, GetMemoryResource(MemTag::Graphics));
		const RibbonStyle style{curve, width, gradient, mode};
		Vector<RibbonPoint> out{GetMemoryResource(MemTag::Graphics)};
		FillRibbonPoints(list, {}, style, loop, out);
		return out;
	}

	u32 BackFacing(const Vector<MeshVertex>& vertices) {
		u32 back = 0;
		for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
			const Vec3 face = Vec3::Cross(vertices[i + 1].position - vertices[i].position, vertices[i + 2].position - vertices[i].position);
			back += Vec3::Dot(face, vertices[i].normal) < -1e-7f ? 1u : 0u;
		}
		return back;
	}

	f32 DistanceToSegment(const Vec3& point, const Vec3& a, const Vec3& b) {
		const Vec3 span = b - a;
		const f32 t = Math::Clamp(Vec3::Dot(point - a, span) / span.LengthSquared(), 0.0f, 1.0f);
		return Vec3::Distance(point, a + span * t);
	}

} // anonymous namespace

TEST(RibbonGeometryTest, StraightLineFacesEye) {
	const Vector<RibbonPoint> points = Points({Vec3(-1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}, 0.2f);
	RibbonFacing facing;
	facing.fromEye = true;
	facing.eye = Vec3(0.0f, 0.0f, 3.0f);
	Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
	BuildRibbon(points, RibbonShape{}, facing, vertices);
	ASSERT_EQ(vertices.size(), 6u);
	EXPECT_EQ(BackFacing(vertices), 0u);
	for (const MeshVertex& vertex : vertices) {
		EXPECT_NEAR(std::abs(vertex.position.y), 0.1f, EPSILON);
		EXPECT_NEAR(vertex.position.z, 0.0f, EPSILON);
		EXPECT_NEAR(Vec3::Dot(vertex.normal, (facing.eye - Vec3(vertex.position.x, 0.0f, 0.0f)).Normalized()), 1.0f, EPSILON);
		EXPECT_NEAR(vertex.u, vertex.position.x > 0.0f ? 1.0f : 0.0f, EPSILON);
		EXPECT_NEAR(Vec3::Dot(vertex.tangent, vertex.normal), 0.0f, EPSILON);
		EXPECT_GT(vertex.tangent.x, 0.9f);
		EXPECT_NEAR(vertex.v, vertex.position.y > 0.0f ? 0.0f : 1.0f, EPSILON);
	}
}

TEST(RibbonGeometryTest, TextureModesSetU) {
	const auto us = [](LineTextureMode mode) {
		const Vector<RibbonPoint> points = Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(4.0f, 0.0f, 0.0f)}, 1.0f, mode);
		std::array<f32, 3> u{};
		for (size_t i = 0; i < 3; ++i) {
			u[i] = points[i].u;
		}
		return u;
	};
	const std::array<f32, 3> stretch = us(LineTextureMode::Stretch);
	EXPECT_NEAR(stretch[1], 0.25f, EPSILON);
	EXPECT_NEAR(stretch[2], 1.0f, EPSILON);
	const std::array<f32, 3> tile = us(LineTextureMode::Tile);
	EXPECT_NEAR(tile[1], 1.0f, EPSILON);
	EXPECT_NEAR(tile[2], 4.0f, EPSILON);
	const std::array<f32, 3> distribute = us(LineTextureMode::DistributePerSegment);
	EXPECT_NEAR(distribute[1], 0.5f, EPSILON);
	EXPECT_NEAR(distribute[2], 1.0f, EPSILON);
	const std::array<f32, 3> repeat = us(LineTextureMode::RepeatPerSegment);
	EXPECT_NEAR(repeat[1], 1.0f, EPSILON);
	EXPECT_NEAR(repeat[2], 2.0f, EPSILON);

	const Vector<RibbonPoint> points = Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(4.0f, 0.0f, 0.0f)}, 1.0f, LineTextureMode::Tile);
	RibbonShape shape;
	shape.textureScale = Vec2(0.5f, 3.0f);
	Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
	BuildRibbon(points, shape, FacingAxis(Vec3(0.0f, 0.0f, 1.0f)), vertices);
	f32 most = 0.0f;
	f32 tallest = 0.0f;
	for (const MeshVertex& vertex : vertices) {
		most = std::max(most, vertex.u);
		tallest = std::max(tallest, vertex.v);
	}
	EXPECT_NEAR(most, 2.0f, EPSILON);
	EXPECT_NEAR(tallest, 3.0f, EPSILON);
}

TEST(RibbonGeometryTest, WidthAndColorFollowLength) {
	const AnimationCurve curve{Keyframe{.time = 0.0f, .value = 0.0f, .outTangent = 1.0f}, Keyframe{.time = 1.0f, .value = 1.0f, .inTangent = 1.0f}};
	const Gradient gradient{{GradientColorKey{Color::Red(), 0.0f}, GradientColorKey{Color::Blue(), 1.0f}}, {GradientAlphaKey{1.0f, 0.0f}, GradientAlphaKey{0.0f, 1.0f}}};
	const Vector<RibbonPoint> points =
		Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f)}, 2.0f, LineTextureMode::Stretch, false, &curve, &gradient);
	ASSERT_EQ(points.size(), 3u);
	EXPECT_NEAR(points[0].width, 0.0f, EPSILON);
	EXPECT_NEAR(points[1].width, 1.0f, EPSILON);
	EXPECT_NEAR(points[2].width, 2.0f, EPSILON);
	EXPECT_NEAR(points[1].color.r, 0.5f, EPSILON);
	EXPECT_NEAR(points[1].color.b, 0.5f, EPSILON);
	EXPECT_NEAR(points[1].color.a, 0.5f, EPSILON);
	EXPECT_NEAR(points[2].color.b, 1.0f, EPSILON);
}

TEST(RibbonGeometryTest, RoundCornerFillsOuterSide) {
	const Vector<RibbonPoint> points = Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f)}, 0.2f);
	const Vec3 corner(1.0f, 0.0f, 0.0f);
	const auto build = [&](u32 cornerVertices) {
		RibbonShape shape;
		shape.cornerVertices = cornerVertices;
		Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
		BuildRibbon(points, shape, FacingAxis(Vec3(0.0f, 0.0f, 1.0f)), vertices);
		EXPECT_EQ(BackFacing(vertices), 0u);
		for (const MeshVertex& vertex : vertices) {
			const f32 offset = std::min(DistanceToSegment(vertex.position, points[0].position, corner),
										DistanceToSegment(vertex.position, corner, points[2].position));
			EXPECT_LE(offset, 0.1f + EPSILON);
		}
		return vertices;
	};
	const auto near = [](const Vector<MeshVertex>& vertices, const Vec3& at) {
		return std::any_of(vertices.begin(), vertices.end(), [&](const MeshVertex& vertex) { return Vec3::Distance(vertex.position, at) < EPSILON; });
	};
	const Vector<MeshVertex> sharp = build(0);
	EXPECT_EQ(sharp.size(), 12u);
	EXPECT_FALSE(near(sharp, Vec3(1.1f, 0.0f, 0.0f)));
	EXPECT_FALSE(near(sharp, Vec3(1.0f, -0.1f, 0.0f)));
	const Vector<MeshVertex> round = build(3);
	EXPECT_EQ(round.size(), 12u + 4u * 3u);
	EXPECT_TRUE(near(round, Vec3(1.1f, 0.0f, 0.0f)));
	EXPECT_TRUE(near(round, Vec3(1.0f, -0.1f, 0.0f)));
}

TEST(RibbonGeometryTest, CapsAddHalfDiscs) {
	const Vector<RibbonPoint> points = Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}, 0.2f);
	RibbonShape shape;
	shape.endCapVertices = 3;
	Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
	BuildRibbon(points, shape, FacingAxis(Vec3(0.0f, 0.0f, 1.0f)), vertices);
	EXPECT_EQ(vertices.size(), 6u + 2u * 4u * 3u);
	EXPECT_EQ(BackFacing(vertices), 0u);
	f32 left = 0.0f;
	f32 right = 1.0f;
	for (const MeshVertex& vertex : vertices) {
		left = std::min(left, vertex.position.x);
		right = std::max(right, vertex.position.x);
	}
	EXPECT_NEAR(left, -0.1f, EPSILON);
	EXPECT_NEAR(right, 1.1f, EPSILON);
}

TEST(RibbonGeometryTest, LoopClosesWithoutCaps) {
	const Vector<RibbonPoint> points =
		Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)}, 0.1f, LineTextureMode::Stretch, true);
	ASSERT_EQ(points.size(), 4u);
	EXPECT_NEAR(points[3].u, 1.0f, EPSILON);
	EXPECT_NEAR(Vec3::Distance(points[3].position, points[0].position), 0.0f, EPSILON);
	RibbonShape shape;
	shape.loop = true;
	shape.endCapVertices = 4;
	Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
	BuildRibbon(points, shape, FacingAxis(Vec3(0.0f, 0.0f, 1.0f)), vertices);
	EXPECT_EQ(vertices.size(), 18u);
	EXPECT_EQ(BackFacing(vertices), 0u);
	const MeshVertex& first = vertices[0];
	bool seamShared = false;
	for (const MeshVertex& vertex : vertices) {
		seamShared = seamShared || (Vec3::Distance(vertex.position, first.position) < EPSILON && std::abs(vertex.u - 1.0f) < EPSILON);
	}
	EXPECT_TRUE(seamShared);
}

TEST(RibbonGeometryTest, FacingFollowsAlignment) {
	const Mat4 view = Mat4::LookAt(Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 0.0f));
	const RibbonFacing perspective =
		FacingFor(LineAlignment::View, Vec3(0.0f, 0.0f, 1.0f), view, Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f), Vec3(0.0f, 0.0f, 3.0f));
	EXPECT_TRUE(perspective.fromEye);
	const RibbonFacing orthographic =
		FacingFor(LineAlignment::View, Vec3(0.0f, 0.0f, 1.0f), view, Mat4::OrthographicReverseZ(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f), Vec3{});
	EXPECT_FALSE(orthographic.fromEye);
	EXPECT_NEAR(Vec3::Dot(orthographic.axis, Vec3(0.0f, 0.0f, 1.0f)), 1.0f, EPSILON);
	const RibbonFacing axis = FacingFor(LineAlignment::TransformZ, Vec3(0.0f, 2.0f, 0.0f), view, Mat4::Identity(), Vec3{});
	EXPECT_FALSE(axis.fromEye);
	EXPECT_NEAR(Vec3::Dot(axis.axis, Vec3(0.0f, 1.0f, 0.0f)), 1.0f, EPSILON);
}

TEST(RibbonGeometryTest, ShadowBiasMovesAwayFromLight) {
	const Vector<RibbonPoint> points = Points({Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}, 0.2f);
	RibbonFacing facing = FacingAxis(Vec3(0.0f, 0.0f, 1.0f));
	facing.shadowBias = 0.5f;
	Vector<MeshVertex> vertices{GetMemoryResource(MemTag::Graphics)};
	BuildRibbon(points, RibbonShape{}, facing, vertices);
	ASSERT_EQ(vertices.size(), 6u);
	for (const MeshVertex& vertex : vertices) {
		EXPECT_NEAR(vertex.position.z, -0.1f, EPSILON);
	}
}

TEST(RibbonGeometryTest, GradientModes) {
	Gradient gradient{{GradientColorKey{Color::Red(), 0.0f}, GradientColorKey{Color::Blue(), 1.0f}}, {GradientAlphaKey{1.0f, 0.0f}}};
	EXPECT_NEAR(gradient.Evaluate(0.25f).r, 0.75f, EPSILON);
	EXPECT_NEAR(gradient.Evaluate(-1.0f).r, 1.0f, EPSILON);
	EXPECT_NEAR(gradient.Evaluate(2.0f).b, 1.0f, EPSILON);
	gradient.mode = GradientMode::Fixed;
	EXPECT_NEAR(gradient.Evaluate(0.0f).r, 1.0f, EPSILON);
	EXPECT_NEAR(gradient.Evaluate(0.25f).b, 1.0f, EPSILON);
	gradient.mode = GradientMode::PerceptualBlend;
	const Color middle = gradient.Evaluate(0.5f);
	EXPECT_GT(middle.r, 0.0f);
	EXPECT_GT(middle.b, 0.0f);
	EXPECT_GT(std::abs(middle.r - 0.5f), 0.02f);
	EXPECT_NEAR(gradient.Evaluate(1.0f).b, 1.0f, 1e-3f);
	EXPECT_NEAR(Gradient{}.Evaluate(0.5f).g, 1.0f, EPSILON);
}

namespace {

	class LineRendererFixture : public MaterialReadbackFixture {
	protected:
		char _token = 0;
		ShadingStage _stage;

		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
		}

		void TearDown() override {
			TearDownStage(_stage);
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			if (RenderSystem::IsRenderingAvailable()) {
				RenderSystem::RenderAll();
			}
			ShutdownEffects();
			ShutdownSimulation();
			ShutdownTrails();
			ShutdownRibbons();
			FramePasses::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			MaterialReadbackFixture::TearDown();
		}

		bool Ready() { return RenderSystem::IsRenderingAvailable() && CompilerAvailable(); }

		Handle<Scene> _scene;

		Handle<TrailRenderer> MakeTrail(f32 lifetime, f32 spacing) {
			if (!_scene.IsValid()) {
				_scene = SceneSystem::CreateScene("Trails");
			}
			Handle<SceneObject> object = SceneSystem::CreateObject("Trail", _scene);
			Handle<TrailRenderer> trail = object->AddComponent<TrailRenderer>();
			trail->time = lifetime;
			trail->minVertexDistance = spacing;
			SceneSystem::EndFrame();
			return trail;
		}

		MaterialAsset* LoadUnlit(const char* file, const Color& color) {
			Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			if (!unlit) {
				return nullptr;
			}
			const str body = "\t\"colors\": { \"baseColor\": [" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
							 std::to_string(color.b) + ", " + std::to_string(color.a) + "] }";
			return LoadMaterial(file, unlit->assetId, body);
		}

		Handle<LineRenderer> AddLine(MaterialAsset* material, std::initializer_list<Vec3> positions, f32 width) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Line", _stage.scene);
			Handle<LineRenderer> line = object->AddComponent<LineRenderer>();
			line->positions.assign(positions.begin(), positions.end());
			line->widthMultiplier = width;
			line->materials.push_back(Handle<MaterialAsset>(material));
			line->castShadows = ShadowCasting::Off;
			SceneSystem::EndFrame();
			return line;
		}

		bool Render(const ViewDesc& desc, Vector<u8>& pixels, Vec2I& size) {
			RenderSystem::SubmitView(_stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(_stage.target, pixels, size);
		}

		static Color32 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Color32{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
		}

		static u32 CountGreen(const Vector<u8>& pixels) {
			u32 count = 0;
			for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
				count += pixels[i + 1] > 200 && pixels[i] < 60 && pixels[i + 2] < 60 ? 1u : 0u;
			}
			return count;
		}
	};

} // anonymous namespace

TEST_F(LineRendererFixture, TrailVerticesFollowMinVertexDistance) {
	Handle<TrailRenderer> trail = MakeTrail(10.0f, 0.1f);
	ASSERT_TRUE(trail.IsValid());
	for (u32 frame = 0; frame < 10; ++frame) {
		trail->GetOwner()->SetLocalPosition(Vec3(0.04f * static_cast<f32>(frame), 0.0f, 0.0f));
		AdvanceTrails(1.0f / 60.0f);
	}
	ASSERT_EQ(trail->GetPositionCount(), 4u);
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_NEAR(trail->GetPosition(i).x, 0.12f * static_cast<f32>(i), EPSILON);
	}
}

TEST_F(LineRendererFixture, TrailPointsExpire) {
	Handle<TrailRenderer> trail = MakeTrail(0.5f, 0.0f);
	ASSERT_TRUE(trail.IsValid());
	for (u32 frame = 0; frame < 4; ++frame) {
		trail->GetOwner()->SetLocalPosition(Vec3(static_cast<f32>(frame), 0.0f, 0.0f));
		AdvanceTrails(0.2f);
	}
	EXPECT_EQ(trail->GetPositionCount(), 3u);
	EXPECT_NEAR(trail->GetPosition(0).x, 1.0f, EPSILON);
	AdvanceTrails(0.2f);
	EXPECT_EQ(trail->GetPositionCount(), 3u);
	EXPECT_NEAR(trail->GetPosition(0).x, 2.0f, EPSILON);
	trail->emitting = false;
	AdvanceTrails(1.0f);
	EXPECT_EQ(trail->GetPositionCount(), 0u);
	trail->AddPosition(Vec3(5.0f, 0.0f, 0.0f));
	EXPECT_EQ(trail->GetPositionCount(), 1u);
	trail->Clear();
	EXPECT_EQ(trail->GetPositionCount(), 0u);
}

TEST_F(LineRendererFixture, TrailAutodestructsWhenStill) {
	Handle<TrailRenderer> trail = MakeTrail(0.5f, 0.1f);
	ASSERT_TRUE(trail.IsValid());
	Handle<SceneObject> owner = trail->GetOwner();
	trail->autodestruct = true;
	AdvanceTrails(0.1f);
	AdvanceTrails(0.3f);
	SceneSystem::EndFrame();
	ASSERT_TRUE(owner.IsValid());
	owner->SetLocalPosition(Vec3(1.0f, 0.0f, 0.0f));
	AdvanceTrails(0.3f);
	AdvanceTrails(0.3f);
	SceneSystem::EndFrame();
	ASSERT_TRUE(owner.IsValid());
	AdvanceTrails(0.3f);
	SceneSystem::EndFrame();
	EXPECT_FALSE(owner.IsValid());
}

TEST_F(LineRendererFixture, LineSurvivesSerialization) {
	_scene = SceneSystem::CreateScene("Saved");
	Handle<SceneObject> object = SceneSystem::CreateObject("Line", _scene);
	Handle<LineRenderer> line = object->AddComponent<LineRenderer>();
	line->positions.push_back(Vec3(2.0f, 3.0f, 4.0f));
	line->width = AnimationCurve{Keyframe{.time = 0.0f, .value = 0.25f}, Keyframe{.time = 1.0f, .value = 2.0f}};
	line->color = Gradient{{GradientColorKey{Color::Red(), 0.0f}, GradientColorKey{Color::Green(), 0.5f}, GradientColorKey{Color::Blue(), 1.0f}},
						   {GradientAlphaKey{0.5f, 0.0f}}};
	line->color.mode = GradientMode::PerceptualBlend;
	line->textureMode = LineTextureMode::RepeatPerSegment;
	line->castShadows = ShadowCasting::TwoSided;
	const str text = TextSerializer::Serialize(line.Get(), line->GetType());

	Handle<LineRenderer> copy = SceneSystem::CreateObject("Copy", _scene)->AddComponent<LineRenderer>();
	ASSERT_TRUE(TextSerializer::Deserialize(copy.Get(), copy->GetType(), text));
	EXPECT_EQ(copy->positions.size(), 3u);
	EXPECT_EQ(copy->width, line->width);
	EXPECT_EQ(copy->color, line->color);
	EXPECT_EQ(copy->textureMode, LineTextureMode::RepeatPerSegment);
	EXPECT_EQ(copy->castShadows, ShadowCasting::TwoSided);

	Handle<TrailRenderer> trail = object->AddComponent<TrailRenderer>();
	trail->color = line->color;
	trail->time = 2.5f;
	Handle<TrailRenderer> trailCopy = SceneSystem::CreateObject("TrailCopy", _scene)->AddComponent<TrailRenderer>();
	ASSERT_TRUE(TextSerializer::Deserialize(trailCopy.Get(), trailCopy->GetType(), TextSerializer::Serialize(trail.Get(), trail->GetType())));
	EXPECT_EQ(trailCopy->color, line->color);
	EXPECT_FLOAT_EQ(trailCopy->time, 2.5f);
}

TEST_F(LineRendererFixture, LineDrawsWithMeshMaterial) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f));
	MaterialAsset* white = LoadUnlit("White.mat", Color::White());
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(white, nullptr);
	ASSERT_TRUE(BuildStage(hidden, _stage));
	_stage.quad->SetActive(false);
	Handle<LineRenderer> line = AddLine(white, {Vec3(-0.8f, 0.0f, 0.5f), Vec3(0.8f, 0.0f, 0.5f)}, 0.3f);
	line->color = Gradient{{GradientColorKey{Color(1.0f, 0.0f, 0.0f), 0.0f}, GradientColorKey{Color(0.0f, 0.0f, 1.0f), 1.0f}},
						   {GradientAlphaKey{1.0f, 0.0f}}};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	const Color32 start = Texel(pixels, size, size.x * 3 / 4, size.y / 2);
	const Color32 end = Texel(pixels, size, size.x / 4, size.y / 2);
	EXPECT_GT(start.r, 150);
	EXPECT_GT(start.r, start.b);
	EXPECT_GT(end.b, 150);
	EXPECT_GT(end.b, end.r);
	const Color32 above = Texel(pixels, size, size.x / 2, size.y / 4);
	EXPECT_LT(std::max({above.r, above.g, above.b}), 20);

	line->materials.clear();
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_LT(Texel(pixels, size, size.x / 4, size.y / 2).r, 20);
}

TEST_F(LineRendererFixture, AlignmentFacesEachView) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f));
	MaterialAsset* green = LoadUnlit("Green.mat", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(green, nullptr);
	ASSERT_TRUE(BuildStage(hidden, _stage));
	_stage.quad->SetActive(false);
	Handle<LineRenderer> line = AddLine(green, {Vec3(-0.8f, 0.0f, 0.0f), Vec3(0.8f, 0.0f, 0.0f)}, 0.3f);

	ViewDesc side = QuadView();
	ViewDesc top = QuadView();
	top.cameraPosition = Vec3(0.0f, 3.0f, 0.01f);
	top.view = Mat4::LookAt(top.cameraPosition, Vec3(0.0f, 0.0f, 0.0f));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(side, pixels, size));
	EXPECT_GT(CountGreen(pixels), 100u);
	ASSERT_TRUE(Render(top, pixels, size));
	EXPECT_GT(CountGreen(pixels), 100u);

	line->alignment = LineAlignment::TransformZ;
	ASSERT_TRUE(Render(side, pixels, size));
	EXPECT_GT(CountGreen(pixels), 100u);
	ASSERT_TRUE(Render(top, pixels, size));
	EXPECT_LT(CountGreen(pixels), 10u);
}

TEST_F(LineRendererFixture, LineCastsShadow) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	constexpr const char* TALLY_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn float3(light.shadow, 0.0, 0.25);\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = float3(0.0, 0.0, 0.0);\n"
		"\tsurface.reflectance = 0.0;\n"
		"}\n";
	WriteProjectFile("Tally.hlsl", TALLY_SHADER);
	Asset* tally = AssetDatabase::LoadAsset(Path("project://Tally.hlsl"));
	ASSERT_NE(tally, nullptr);
	MaterialAsset* ground = LoadMaterial("Tally.mat", tally->assetId, "");
	MaterialAsset* green = LoadUnlit("Green.mat", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_NE(ground, nullptr);
	ASSERT_NE(green, nullptr);
	ASSERT_TRUE(BuildStage(ground, _stage));
	_stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
	Handle<LineRenderer> line = AddLine(green, {Vec3(-0.3f, 0.0f, 1.0f), Vec3(0.3f, 0.0f, 1.0f)}, 0.3f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto shadowed = [&] {
		EXPECT_TRUE(Render(QuadView(), pixels, size));
		u32 count = 0;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			count += pixels[i + 2] > 100 && pixels[i + 2] < 180 && pixels[i] < 128 ? 1u : 0u;
		}
		return count;
	};
	EXPECT_EQ(shadowed(), 0u);
	line->castShadows = ShadowCasting::On;
	EXPECT_GT(shadowed(), 20u);
	line->castShadows = ShadowCasting::ShadowsOnly;
	EXPECT_GT(shadowed(), 20u);
	EXPECT_EQ(CountGreen(pixels), 0u);
}

TEST_F(LineRendererFixture, TrailDrawsBehindMovingObject) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f));
	MaterialAsset* green = LoadUnlit("Green.mat", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(green, nullptr);
	ASSERT_TRUE(BuildStage(hidden, _stage));
	_stage.quad->SetActive(false);
	Handle<SceneObject> object = SceneSystem::CreateObject("Trail", _stage.scene);
	Handle<TrailRenderer> trail = object->AddComponent<TrailRenderer>();
	trail->materials.push_back(Handle<MaterialAsset>(green));
	trail->widthMultiplier = 0.2f;
	trail->castShadows = ShadowCasting::Off;
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_EQ(CountGreen(pixels), 0u);
	for (u32 frame = 0; frame <= 24; ++frame) {
		object->SetLocalPosition(Vec3(-1.2f + 0.1f * static_cast<f32>(frame), 0.0f, 0.0f));
		AdvanceTrails(1.0f / 60.0f);
	}
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_GT(Texel(pixels, size, size.x / 4, size.y / 2).g, 200);
	EXPECT_GT(Texel(pixels, size, size.x * 3 / 4, size.y / 2).g, 200);
	EXPECT_LT(Texel(pixels, size, size.x / 2, size.y / 4).g, 20);
}

TEST_F(LineRendererFixture, TrailHiddenPastItsLODLevel) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f));
	MaterialAsset* green = LoadUnlit("Green.mat", Color(0.0f, 1.0f, 0.0f, 1.0f));
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(green, nullptr);
	ASSERT_TRUE(BuildStage(hidden, _stage));
	_stage.quad->SetActive(false);
	Handle<SceneObject> object = SceneSystem::CreateObject("Trail", _stage.scene);
	Handle<TrailRenderer> trail = object->AddComponent<TrailRenderer>();
	trail->materials.push_back(Handle<MaterialAsset>(green));
	trail->widthMultiplier = 0.2f;
	trail->castShadows = ShadowCasting::Off;
	Handle<LODGroup> group = SceneSystem::CreateObject("Group", _stage.scene)->AddComponent<LODGroup>();
	group->AddLevel(0.2f).objects.push_back(object);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (u32 frame = 0; frame <= 24; ++frame) {
		object->SetLocalPosition(Vec3(-1.2f + 0.1f * static_cast<f32>(frame), 0.0f, 0.0f));
		AdvanceTrails(1.0f / 60.0f);
	}
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_GT(CountGreen(pixels), 100u);

	group->EditLevel(0).screenRelativeHeight = 0.5f;
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_EQ(CountGreen(pixels), 0u);

	group->EditLevel(0).screenRelativeHeight = 0.2f;
	ASSERT_TRUE(Render(QuadView(), pixels, size));
	EXPECT_GT(CountGreen(pixels), 100u);
}

#endif
