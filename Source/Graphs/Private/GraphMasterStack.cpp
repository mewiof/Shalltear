#include "GraphNodeLibrary.h"

namespace Shalltear {

	namespace {

		using P = GraphPortType;

		// sRGB 0.5 as linear
		constexpr f32 GREY_LINEAR = 0.21404114f;

		constexpr GraphPortDefinition BASE_COLOR[] = {{"Value", P::Float3, {GREY_LINEAR, GREY_LINEAR, GREY_LINEAR, 0.0f}}};
		constexpr GraphPortDefinition ALPHA[] = {{"Value", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition METALLIC[] = {{"Value", P::Float, {0.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition ROUGHNESS[] = {{"Value", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition REFLECTANCE[] = {{"Value", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition OCCLUSION[] = {{"Value", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition EMISSION[] = {{"Value", P::Float3, {0.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition NORMAL_TS[] = {{"Value", P::Float3, {}, "TangentSpaceNormal"}};
		constexpr GraphPortDefinition NORMAL_OS[] = {{"Value", P::Float3, {}, "ObjectSpaceNormal"}};
		constexpr GraphPortDefinition NORMAL_WS[] = {{"Value", P::Float3, {}, "WorldSpaceNormal"}};
		constexpr GraphPortDefinition POSITION[] = {{"Value", P::Float3, {}, "ObjectSpacePosition"}};
		constexpr GraphPortDefinition VERTEX_NORMAL[] = {{"Value", P::Float3, {}, "ObjectSpaceNormal"}};
		constexpr GraphPortDefinition VERTEX_TANGENT[] = {{"Value", P::Float3, {}, "ObjectSpaceTangent"}};

		constexpr GraphSettingDefinition INTERPOLATOR_SETTINGS[] = {{"Name", GraphSettingKind::Text, {}, 0, "CustomInterpolator"},
																	{"Type", GraphSettingKind::Choice, GRAPH_INTERPOLATOR_TYPES, 3},
																	{"Interpolation", GraphSettingKind::Choice, GRAPH_INTERPOLATOR_MODES}};
		constexpr GraphPortDefinition INTERPOLATOR[] = {{"Value", P::Float4, {}}};

		constexpr GraphPortDefinition RADIANCE[] = {{"Value", P::Float3, {}, "StandardRadiance"}};

		constexpr GraphNodeDefinition MASTER_STACK_DEFINITIONS[] = {
			{"Context/Vertex", "Vertex", "Context", "", {}, {}, {}, nullptr},
			{"Context/Fragment", "Fragment", "Context", "", {}, {}, {}, nullptr},
			{"Context/Lighting", "Lighting", "Context", "", {}, {}, {}, nullptr},

			{"Block/Position", "Position", "Block", "", POSITION, {}, {}, nullptr},
			{"Block/Normal", "Normal", "Block", "", VERTEX_NORMAL, {}, {}, nullptr},
			{"Block/Tangent", "Tangent", "Block", "", VERTEX_TANGENT, {}, {}, nullptr},
			{"Block/CustomInterpolator", "Custom Interpolator", "Block", "", INTERPOLATOR, {}, INTERPOLATOR_SETTINGS, nullptr},

			{"Block/BaseColor", "Base Color", "Block", "", BASE_COLOR, {}, {}, nullptr},
			{"Block/NormalTS", "Normal (Tangent Space)", "Block", "", NORMAL_TS, {}, {}, nullptr},
			{"Block/NormalOS", "Normal (Object Space)", "Block", "", NORMAL_OS, {}, {}, nullptr},
			{"Block/NormalWS", "Normal (World Space)", "Block", "", NORMAL_WS, {}, {}, nullptr},
			{"Block/Metallic", "Metallic", "Block", "", METALLIC, {}, {}, nullptr},
			{"Block/Roughness", "Roughness", "Block", "smoothness", ROUGHNESS, {}, {}, nullptr},
			{"Block/Reflectance", "Reflectance", "Block", "specular", REFLECTANCE, {}, {}, nullptr},
			{"Block/Occlusion", "Ambient Occlusion", "Block", "", OCCLUSION, {}, {}, nullptr},
			{"Block/Emission", "Emission", "Block", "emissive", EMISSION, {}, {}, nullptr},
			{"Block/Alpha", "Alpha", "Block", "", ALPHA, {}, {}, nullptr},

			{"Block/Radiance", "Radiance", "Block", "", RADIANCE, {}, {}, nullptr},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphMasterStackDefinitions() { return MASTER_STACK_DEFINITIONS; }

} // namespace Shalltear
