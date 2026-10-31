#include "TestsPCH.h"

#include "EffectGraphGizmos.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphWindowParts.h"

#include "Shalltear/Editor/EditorGizmoOps.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>

using namespace Shalltear;

namespace {

	namespace Gizmos = EffectGraphGizmoOps;

	constexpr std::string_view Shapes = R"({"_type":"EffectGraph","version":4,"nextId":40,"nodes":[
		{"id":30,"def":"Math/Add"}],"contexts":[
		{"id":2,"def":"Context/Initialize","settings":{"Capacity":8},"blocks":[
			{"id":20,"def":"Block/PositionShape","settings":{"Shape":"Line"},"in":{"Start":{"value":[0,0,0]},"End":{"value":[0,2,0],"space":"World"}}}]},
		{"id":4,"def":"Context/Update","flow":[[2,"In"]],"blocks":[
			{"id":10,"def":"Block/CollisionShape","settings":{"Shape":"Plane"},"in":{"Position":{"value":[0,1,0]},"Normal":{"value":[0,0,1]}}},
			{"id":11,"def":"Block/CollisionShape","settings":{"Shape":"Sphere"},"in":{"Position":{"value":[1,0,0]},"Angles":{"value":[30,45,60]},"Radius":{"value":[2]}}},
			{"id":12,"def":"Block/CollisionShape","settings":{"Shape":"OrientedBox"},"in":{"Center":{"value":[0,0,0]},"Angles":{"value":[0,90,0]},"Size":{"value":[1,2,3]}}},
			{"id":13,"def":"Block/CollisionShape","settings":{"Shape":"Cone"},"in":{"Height":{"value":[2]}}},
			{"id":14,"def":"Block/CollisionShape","settings":{"Shape":"Sphere"},"in":{"Position":{"from":[30,"Out"]}}},
			{"id":15,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}}]}]})";

	GraphDocument Read() {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(Shapes, document, error)) << error;
		return document;
	}

	const Quat EFFECT_TURN = Quat::AngleAxis(90.0f, Vec3::Up());
	const Mat4 EFFECT = Mat4::TRS(Vec3(10.0f, 0.0f, 0.0f), EFFECT_TURN, Vec3(2.0f, 2.0f, 2.0f));

	bool Build(const GraphDocument& document, u32 block, GraphDefinitions& definitions, Gizmos::BlockGizmo& out) {
		return Gizmos::Build(document, block, definitions, EFFECT, EFFECT_TURN, Color32{255, 255, 255, 255}, Color32{255, 255, 255, 255}, "Shape", out);
	}

	const SceneHandle* OfKind(const Gizmos::BlockGizmo& gizmo, SceneHandleKind kind, u32 skip = 0) {
		for (const SceneHandle& handle : gizmo.handles) {
			if (handle.kind == kind && skip-- == 0) {
				return &handle;
			}
		}
		return nullptr;
	}

	bool Touches(const Gizmos::BlockGizmo& gizmo, const Vec3& point) {
		return std::any_of(gizmo.lines.begin(), gizmo.lines.end(), [&](const DebugLine& line) {
			return Vec3::Distance(line.a, point) < 1.0e-4f || Vec3::Distance(line.b, point) < 1.0e-4f;
		});
	}

	str Held(const GraphDocument& document, u32 block, std::string_view port) {
		for (const GraphContext& context : document.contexts) {
			for (const GraphNode& node : context.blocks) {
				if (node.id == block) {
					const GraphInput* input = GraphWindowParts::FindInput(node, port);
					return input ? input->value : str();
				}
			}
		}
		return {};
	}

	Vec3 HeldVec3(const GraphDocument& document, u32 block, std::string_view port) {
		f32 v[4] = {};
		GraphWindowParts::ReadNumbers(Held(document, block, port), v);
		return Vec3(v[0], v[1], v[2]);
	}

} // anonymous namespace

TEST(EffectGraphGizmosTest, PlaneGizmoMovesAndTurns) {
	GraphDocument document = Read();
	GraphDefinitions definitions;
	Gizmos::BlockGizmo gizmo;
	ASSERT_TRUE(Build(document, 10, definitions, gizmo));
	const SceneHandle* position = OfKind(gizmo, SceneHandleKind::Position);
	const SceneHandle* normal = OfKind(gizmo, SceneHandleKind::Rotation);
	ASSERT_TRUE(position && normal);
	EXPECT_EQ(gizmo.handles.size(), 2u);
	EXPECT_NEAR(Vec3::Distance(EditorGizmoOps::SceneHandleOrigin(*position), EFFECT.TransformPoint(Vec3(0.0f, 1.0f, 0.0f))), 0.0f, 1.0e-4f);
	EXPECT_TRUE(Touches(gizmo, EFFECT.TransformPoint(Vec3(5.0f, 6.0f, 0.0f))));

	SceneHandle moved = *position;
	moved.position = Vec3(0.0f, 3.0f, 0.0f);
	ASSERT_TRUE(Gizmos::Write(document, 10, definitions, EFFECT, moved));
	EXPECT_NEAR(HeldVec3(document, 10, "Position").y, 3.0f, 1.0e-5f);
	EXPECT_FALSE(Gizmos::Write(document, 10, definitions, EFFECT, moved));

	SceneHandle turned = *normal;
	turned.rotation = Quat::AngleAxis(90.0f, Vec3::Right());
	ASSERT_TRUE(Gizmos::Write(document, 10, definitions, EFFECT, turned));
	const Vec3 facing = Quat::AngleAxis(90.0f, Vec3::Right()) * Vec3::Forward();
	EXPECT_NEAR(Vec3::Distance(HeldVec3(document, 10, "Normal"), facing), 0.0f, 1.0e-4f);
	EXPECT_NEAR(HeldVec3(document, 10, "Position").y, 3.0f, 1.0e-5f);
}

TEST(EffectGraphGizmosTest, SphereGizmoMatchesCompiler) {
	GraphDocument document = Read();
	GraphDefinitions definitions;
	Gizmos::BlockGizmo gizmo;
	ASSERT_TRUE(Build(document, 11, definitions, gizmo));
	const SceneHandle* frame = OfKind(gizmo, SceneHandleKind::Transform);
	ASSERT_NE(frame, nullptr);
	ASSERT_EQ(gizmo.handles.size(), 4u);
	const f32 sx = std::sin(30.0f * Math::DEG_TO_RAD), cx = std::cos(30.0f * Math::DEG_TO_RAD);
	const f32 sy = std::sin(45.0f * Math::DEG_TO_RAD), cy = std::cos(45.0f * Math::DEG_TO_RAD);
	const f32 sz = std::sin(60.0f * Math::DEG_TO_RAD), cz = std::cos(60.0f * Math::DEG_TO_RAD);
	const Vec3 column0(cy * cz + sx * sy * sz, cx * sz, -cz * sy + cy * sx * sz);
	EXPECT_NEAR(Vec3::Distance(frame->rotation * Vec3::Right(), column0), 0.0f, 1.0e-4f);

	const SceneHandle* slider = OfKind(gizmo, SceneHandleKind::Slider);
	ASSERT_NE(slider, nullptr);
	EXPECT_NEAR(slider->distance, 2.0f, 1.0e-5f);
	const Vec3 dot = EditorGizmoOps::SliderDot(*slider).position;
	const Vec3 expected = EFFECT.TransformPoint(Vec3(1.0f, 0.0f, 0.0f) + frame->rotation * (slider->direction * 2.0f));
	EXPECT_NEAR(Vec3::Distance(dot, expected), 0.0f, 1.0e-4f);
	SceneHandle grown = *slider;
	grown.distance = -3.0f;
	ASSERT_TRUE(Gizmos::Write(document, 11, definitions, EFFECT, grown));
	EXPECT_EQ(Held(document, 11, "Radius"), "3");

	SceneHandle moved = *frame;
	moved.position = Vec3(1.0f, 4.0f, 0.0f);
	ASSERT_TRUE(Gizmos::Write(document, 11, definitions, EFFECT, moved));
	EXPECT_NEAR(HeldVec3(document, 11, "Position").y, 4.0f, 1.0e-5f);
	EXPECT_EQ(Held(document, 11, "Angles"), "[30,45,60]");
	EXPECT_TRUE(Held(document, 11, "Scale").empty());

	SceneHandle turned = moved;
	turned.rotation = Quat::FromEuler(Vec3(10.0f, 20.0f, 30.0f));
	ASSERT_TRUE(Gizmos::Write(document, 11, definitions, EFFECT, turned));
	const Vec3 angles = HeldVec3(document, 11, "Angles");
	EXPECT_NEAR(Vec3::Distance(angles, Vec3(10.0f, 20.0f, 30.0f)), 0.0f, 1.0e-3f);
}

TEST(EffectGraphGizmosTest, BoxFaceMovesAlone) {
	GraphDocument document = Read();
	GraphDefinitions definitions;
	Gizmos::BlockGizmo gizmo;
	ASSERT_TRUE(Build(document, 12, definitions, gizmo));
	EXPECT_NE(OfKind(gizmo, SceneHandleKind::Position), nullptr);
	EXPECT_NE(OfKind(gizmo, SceneHandleKind::Rotation), nullptr);
	EXPECT_EQ(OfKind(gizmo, SceneHandleKind::Transform), nullptr);
	EXPECT_EQ(gizmo.lines.size(), 12u);
	const SceneHandle* face = OfKind(gizmo, SceneHandleKind::Slider);
	ASSERT_NE(face, nullptr);
	ASSERT_EQ(face->direction, Vec3::Right());
	EXPECT_NEAR(face->distance, 0.5f, 1.0e-5f);

	SceneHandle pulled = *face;
	pulled.distance = 1.0f;
	ASSERT_TRUE(Gizmos::Write(document, 12, definitions, EFFECT, pulled));
	EXPECT_NEAR(HeldVec3(document, 12, "Size").x, 1.5f, 1.0e-5f);
	const Vec3 shifted = Quat::FromEuler(Vec3(0.0f, 90.0f, 0.0f)) * Vec3(0.25f, 0.0f, 0.0f);
	EXPECT_NEAR(Vec3::Distance(HeldVec3(document, 12, "Center"), shifted), 0.0f, 1.0e-4f);
}

TEST(EffectGraphGizmosTest, ConeGizmoHeightLines) {
	GraphDocument document = Read();
	GraphDefinitions definitions;
	Gizmos::BlockGizmo gizmo;
	ASSERT_TRUE(Build(document, 13, definitions, gizmo));
	const SceneHandle* height = OfKind(gizmo, SceneHandleKind::Slider, 8);
	ASSERT_NE(height, nullptr);
	EXPECT_EQ(height->direction, Vec3::Up());
	EXPECT_NEAR(height->distance, 2.0f, 1.0e-5f);
	SceneHandle taller = *height;
	taller.distance = 3.0f;
	ASSERT_TRUE(Gizmos::Write(document, 13, definitions, EFFECT, taller));
	EXPECT_EQ(Held(document, 13, "Height"), "3");
	EXPECT_TRUE(Held(document, 13, "Base Radius").empty());

	ASSERT_TRUE(Build(document, 20, definitions, gizmo));
	const SceneHandle* start = OfKind(gizmo, SceneHandleKind::Position, 0);
	const SceneHandle* end = OfKind(gizmo, SceneHandleKind::Position, 1);
	ASSERT_TRUE(start && end);
	EXPECT_NEAR(Vec3::Distance(EditorGizmoOps::SceneHandleOrigin(*start), EFFECT.TransformPoint(Vec3())), 0.0f, 1.0e-4f);
	EXPECT_NEAR(Vec3::Distance(EditorGizmoOps::SceneHandleOrigin(*end), Vec3(0.0f, 2.0f, 0.0f)), 0.0f, 1.0e-4f);
	EXPECT_TRUE(Touches(gizmo, Vec3(0.0f, 2.0f, 0.0f)));

	EXPECT_FALSE(Build(document, 14, definitions, gizmo));
	EXPECT_TRUE(gizmo.handles.empty());
	EXPECT_FALSE(Build(document, 15, definitions, gizmo));
}
