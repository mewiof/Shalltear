#include "GraphNodeLibrary.h"

namespace Shalltear {

	namespace {

		using P = GraphPortType;

		// sRGB 0.5 as linear
		constexpr f32 GREY_LINEAR = 0.21404114f;

		constexpr GraphPortDefinition BaseColor[] = {{"Value", P::Float3, {GREY_LINEAR, GREY_LINEAR, GREY_LINEAR, 0.0f}}};
		constexpr GraphPortDefinition Alpha[] = {{"Value", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition Metallic[] = {{"Value", P::Float, {0.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition Roughness[] = {{"Value", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition Reflectance[] = {{"Value", P::Float, {0.5f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition Occlusion[] = {{"Value", P::Float, {1.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition Emission[] = {{"Value", P::Float3, {0.0f, 0.0f, 0.0f, 0.0f}}};
		constexpr GraphPortDefinition NormalTs[] = {{"Value", P::Float3, {}, "TangentSpaceNormal"}};
		constexpr GraphPortDefinition NormalOs[] = {{"Value", P::Float3, {}, "ObjectSpaceNormal"}};
		constexpr GraphPortDefinition NormalWS[] = {{"Value", P::Float3, {}, "WorldSpaceNormal"}};
		constexpr GraphPortDefinition Position[] = {{"Value", P::Float3, {}, "ObjectSpacePosition"}};
		constexpr GraphPortDefinition VertexNormal[] = {{"Value", P::Float3, {}, "ObjectSpaceNormal"}};
		constexpr GraphPortDefinition VertexTangent[] = {{"Value", P::Float3, {}, "ObjectSpaceTangent"}};

		constexpr GraphSettingDefinition InterpolatorSettings[] = {{"Name", GraphSettingKind::Text, {}, 0, "CustomInterpolator"},
																   {"Type", GraphSettingKind::Choice, GraphInterpolatorTypes, 3},
																   {"Interpolation", GraphSettingKind::Choice, GraphInterpolatorModes}};
		constexpr GraphPortDefinition Interpolator[] = {{"Value", P::Float4, {}}};

		constexpr GraphPortDefinition Radiance[] = {{"Value", P::Float3, {}, "StandardRadiance"}};

		constexpr GraphNodeDefinition MasterStackDefinitions[] = {
			{"Context/Vertex", "Vertex", "Context", "", {}, {}, {}, nullptr},
			{"Context/Fragment", "Fragment", "Context", "", {}, {}, {}, nullptr},
			{"Context/Lighting", "Lighting", "Context", "", {}, {}, {}, nullptr},

			{"Block/Position", "Position", "Block", "", Position, {}, {}, nullptr},
			{"Block/Normal", "Normal", "Block", "", VertexNormal, {}, {}, nullptr},
			{"Block/Tangent", "Tangent", "Block", "", VertexTangent, {}, {}, nullptr},
			{"Block/CustomInterpolator", "Custom Interpolator", "Block", "", Interpolator, {}, InterpolatorSettings, nullptr},

			{"Block/BaseColor", "Base Color", "Block", "", BaseColor, {}, {}, nullptr},
			{"Block/NormalTS", "Normal (Tangent Space)", "Block", "", NormalTs, {}, {}, nullptr},
			{"Block/NormalOS", "Normal (Object Space)", "Block", "", NormalOs, {}, {}, nullptr},
			{"Block/NormalWS", "Normal (World Space)", "Block", "", NormalWS, {}, {}, nullptr},
			{"Block/Metallic", "Metallic", "Block", "", Metallic, {}, {}, nullptr},
			{"Block/Roughness", "Roughness", "Block", "smoothness", Roughness, {}, {}, nullptr},
			{"Block/Reflectance", "Reflectance", "Block", "specular", Reflectance, {}, {}, nullptr},
			{"Block/Occlusion", "Ambient Occlusion", "Block", "", Occlusion, {}, {}, nullptr},
			{"Block/Emission", "Emission", "Block", "emissive", Emission, {}, {}, nullptr},
			{"Block/Alpha", "Alpha", "Block", "", Alpha, {}, {}, nullptr},

			{"Block/Radiance", "Radiance", "Block", "", Radiance, {}, {}, nullptr},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphMasterStackDefinitions() { return MasterStackDefinitions; }

} // namespace Shalltear
