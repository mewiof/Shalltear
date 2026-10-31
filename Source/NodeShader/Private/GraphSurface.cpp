#include "GraphSurface.h"

#include "GraphDefinitions.h"
#include "GraphHLSL.h"
#include "GraphNodeLibrary.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace Shalltear {

	namespace {

		struct LineSegment {
			size_t start = 0;
			const Vector<u32>* nodes = nullptr;
		};

		size_t LineOf(const str& text) { return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')); }

		void LayLines(const str& text, std::span<const LineSegment> segments, Vector<u32>* out) {
			if (!out) {
				return;
			}
			out->assign(LineOf(text) + 1, 0u);
			for (const LineSegment& segment : segments) {
				for (size_t i = 0; i < segment.nodes->size() && segment.start + i < out->size(); ++i) {
					(*out)[segment.start + i] = (*segment.nodes)[i];
				}
			}
		}

		enum class Target : u8 {
			Lit,
			Unlit,
			CustomLighting,
			Fullscreen,
			Sky
		};

		enum class NormalSpace : u8 {
			Tangent,
			World,
			Object
		};

		struct Settings {
			Target target = Target::Lit;
			RHI::BlendMode blend = RHI::BlendMode::Opaque;
			RHI::CullMode cull = RHI::CullMode::Back;
			RHI::DepthMode depth = RHI::DepthMode::TestWrite;
			NormalSpace normalSpace = NormalSpace::Tangent;
		};

		str SettingText(const GraphDocument& document, std::string_view key) {
			for (const GraphSetting& setting : document.settings) {
				if (setting.key == key) {
					LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
					JSONValue value = JSONParse(setting.value, arena);
					return value.IsString() ? str(value.AsString()) : str("?");
				}
			}
			return {};
		}

		bool SettingToggle(const GraphDocument& document, std::string_view key) {
			for (const GraphSetting& setting : document.settings) {
				if (setting.key == key) {
					LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
					JSONValue value = JSONParse(setting.value, arena);
					return value.IsBool() && value.AsBool();
				}
			}
			return false;
		}

		constexpr std::string_view SUPPORT_EFFECTS = "supportEffects";

		bool ReadSettings(const GraphDocument& document, Settings& out, Vector<GraphDiagnostic>& diagnostics) {
			bool valid = true;
			auto refuse = [&](str message) {
				diagnostics.push_back({0, std::move(message)});
				valid = false;
			};
			str target = SettingText(document, "target");
			if (target == "unlit") {
				out.target = Target::Unlit;
			} else if (target == "customLighting") {
				out.target = Target::CustomLighting;
			} else if (target == "fullscreen") {
				out.target = Target::Fullscreen;
			} else if (target == "sky") {
				out.target = Target::Sky;
			} else if (!target.empty() && target != "lit") {
				refuse("the graph's target '" + target + "' isn't one of lit, unlit, customLighting, fullscreen, sky");
			}
			if (str blend = SettingText(document, "blend"); !blend.empty() && !RHI::ParseBlendMode(blend, out.blend)) {
				refuse("the graph's blend '" + blend + "' isn't one of opaque, alpha, premultiplied, additive, multiply");
			}
			if (str cull = SettingText(document, "cull"); !cull.empty() && !RHI::ParseCullMode(cull, out.cull)) {
				refuse("the graph's cull '" + cull + "' isn't one of back, front, off");
			}
			if (str depth = SettingText(document, "depth"); !depth.empty() && !RHI::ParseDepthMode(depth, out.depth)) {
				refuse("the graph's depth '" + depth + "' isn't one of test_write, test_only, off");
			}
			str space = SettingText(document, "normalSpace");
			if (space == "world") {
				out.normalSpace = NormalSpace::World;
			} else if (space == "object") {
				refuse("the graph's Fragment Normal Space is object (a surface shader here has no object space)");
			} else if (!space.empty() && space != "tangent") {
				refuse("the graph's Fragment Normal Space '" + space + "' isn't one of tangent, world, object");
			}
			return valid;
		}

		struct Block {
			std::string_view id;
			std::string_view field;
			bool lit;
			bool unlit;
			bool sky;
		};

		constexpr Block FRAGMENT_BLOCKS[] = {
			{"Block/BaseColor", "baseColor", true, true, true},
			{"Block/Alpha", "alpha", true, true, false},
			{"Block/Metallic", "metallic", true, false, false},
			{"Block/Roughness", "roughness", true, false, false},
			{"Block/Reflectance", "reflectance", true, false, false},
			{"Block/NormalTS", "normal", true, false, false},
			{"Block/NormalWS", "normal", true, false, false},
			{"Block/NormalOS", "normal", true, false, false},
			{"Block/Occlusion", "occlusion", true, false, false},
			{"Block/Emission", "emissive", true, false, false},
		};

		bool Active(const Block& block, const Settings& settings) {
			if (settings.target == Target::Unlit || settings.target == Target::Fullscreen) {
				return block.unlit;
			}
			if (settings.target == Target::Sky) {
				return block.sky;
			}
			if (block.id == "Block/NormalTS") {
				return settings.normalSpace == NormalSpace::Tangent;
			}
			if (block.id == "Block/NormalWS") {
				return settings.normalSpace == NormalSpace::World;
			}
			if (block.id == "Block/NormalOS") {
				return settings.normalSpace == NormalSpace::Object;
			}
			return block.lit;
		}

		constexpr const char* SURFACE_NO_SCENE = "a material's draw is handed no image of the scene (a Fullscreen graph's is)";
		constexpr const char* NO_MOTION_VECTORS = "nothing renders motion vectors";
		constexpr const char* SKY_ONLY = "only a Sky graph is handed the sky's ray and its sun";

		const GraphBinding SURFACE_BINDINGS[] = {
			{"UV0", "input.uv", GraphValueType::Float2, GraphStageAll},
			{"VertexColor", "input.color", GraphValueType::Float4, GraphStageAll},

			{"WorldSpacePosition", "input.positionWS", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpacePosition", "ShalltearGraphObjectPosition(input)", GraphValueType::Float3, GraphStageAll},
			{"ViewSpacePosition", "mul(ShalltearViewMatrix(), float4(input.positionWS, 1.0)).xyz", GraphValueType::Float3, GraphStageAll},
			{"TangentSpacePosition", "float3(0.0, 0.0, 0.0)", GraphValueType::Float3, GraphStageAll},
			{"AbsoluteWorldSpacePosition", "input.positionWS", GraphValueType::Float3, GraphStageAll},

			{"WorldSpacePositionPredisplacement", "ShalltearGraphPredisplacementWS(input)", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpacePositionPredisplacement", "ShalltearGraphPredisplacementOS(input)", GraphValueType::Float3, GraphStageAll},
			{"ViewSpacePositionPredisplacement", "mul(ShalltearViewMatrix(), float4(ShalltearGraphPredisplacementWS(input), 1.0)).xyz",
			 GraphValueType::Float3, GraphStageAll},
			{"TangentSpacePositionPredisplacement", "float3(0.0, 0.0, 0.0)", GraphValueType::Float3, GraphStageAll},
			{"AbsoluteWorldSpacePositionPredisplacement", "ShalltearGraphPredisplacementWS(input)", GraphValueType::Float3, GraphStageAll},

			{"WorldSpaceNormal", "input.normalWS", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpaceNormal", "ShalltearGraphObjectNormal(input)", GraphValueType::Float3, GraphStageAll},
			{"ViewSpaceNormal", "mul(input.normalWS, (float3x3)ShalltearInverseViewMatrix())", GraphValueType::Float3, GraphStageAll},
			{"TangentSpaceNormal", "float3(0.0, 0.0, 1.0)", GraphValueType::Float3, GraphStageAll},

			{"WorldSpaceTangent", "ShalltearGraphTangent(input)", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpaceTangent", "ShalltearGraphObjectTangent(input)", GraphValueType::Float3, GraphStageAll},
			{"ViewSpaceTangent", "mul((float3x3)ShalltearViewMatrix(), ShalltearGraphTangent(input))", GraphValueType::Float3, GraphStageAll},
			{"TangentSpaceTangent", "float3(1.0, 0.0, 0.0)", GraphValueType::Float3, GraphStageAll},

			{"WorldSpaceBiTangent", "ShalltearGraphBitangent(input)", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpaceBiTangent", "normalize(mul((float3x3)input.worldToObject, ShalltearGraphBitangent(input)))", GraphValueType::Float3, GraphStageAll},
			{"ViewSpaceBiTangent", "mul((float3x3)ShalltearViewMatrix(), ShalltearGraphBitangent(input))", GraphValueType::Float3, GraphStageAll},
			{"TangentSpaceBiTangent", "float3(0.0, 1.0, 0.0)", GraphValueType::Float3, GraphStageAll},

			{"WorldSpaceViewDirection", "input.viewDirectionWS", GraphValueType::Float3, GraphStageAll},
			{"ObjectSpaceViewDirection", "normalize(mul((float3x3)input.worldToObject, input.viewDirectionWS))", GraphValueType::Float3, GraphStageAll},
			{"ViewSpaceViewDirection", "mul((float3x3)ShalltearViewMatrix(), input.viewDirectionWS)", GraphValueType::Float3, GraphStageAll},
			{"TangentSpaceViewDirection", "ShalltearGraphTangentSpaceViewDirection(input)", GraphValueType::Float3, GraphStageAll},

			{"ScreenPosition", "ShalltearGraphScreenPosition(input)", GraphValueType::Float4, GraphStageAll},
			{"NDCPosition", "(ShalltearGraphPixelPosition(input) / ShalltearScreenParameters().xy)", GraphValueType::Float2, GraphStageAll},
			{"PixelPosition", "ShalltearGraphPixelPosition(input)", GraphValueType::Float2, GraphStageAll},
			{"ClipPosition", "ShalltearGraphClipPosition(input)", GraphValueType::Float2, GraphStageAll},

			{"ObjectToWorld", "input.objectToWorld", GraphValueType::Float4x4, GraphStageAll},
			{"WorldToObject", "input.worldToObject", GraphValueType::Float4x4, GraphStageAll},
			{"View", "ShalltearViewMatrix()", GraphValueType::Float4x4, GraphStageAll},
			{"InverseView", "ShalltearInverseViewMatrix()", GraphValueType::Float4x4, GraphStageAll},
			{"Projection", "ShalltearProjectionMatrix()", GraphValueType::Float4x4, GraphStageAll},
			{"InverseProjection", "ShalltearInverseProjectionMatrix()", GraphValueType::Float4x4, GraphStageAll},
			{"ViewProjection", "mul(ShalltearProjectionMatrix(), ShalltearViewMatrix())", GraphValueType::Float4x4, GraphStageAll},
			{"InverseViewProjection", "ShalltearInverseViewProjectionMatrix()", GraphValueType::Float4x4, GraphStageAll},

			{"CameraPosition", "sltCameraPosition", GraphValueType::Float3, GraphStageAll},
			{"ProjectionParams", "ShalltearProjectionParameters()", GraphValueType::Float4, GraphStageAll},
			{"ScreenParams", "ShalltearScreenParameters()", GraphValueType::Float4, GraphStageAll},
			{"TimeParameters", "ShalltearTimeParameters()", GraphValueType::Float4, GraphStageAll},

			{"InstanceID", "0.0", GraphValueType::Float, GraphStageAll},
			{"EyeIndex", "0.0", GraphValueType::Float, GraphStageAll},
			{"VertexID", "float(input.vertexId)", GraphValueType::Float, GraphStageVertex},
			{"IsFrontFace", "input.isFrontFace", GraphValueType::Bool, static_cast<GraphStageMask>(GraphStageFragment | GraphStageLighting)},
			{"SmoothDeltaTime", "0.0", GraphValueType::Float, GraphStageAll, "the frame clock has no smoothed delta"},
			{"RendererBoundsMin", "0.0", GraphValueType::Float3, GraphStageAll, "a draw does not provide its bounds"},
			{"RendererBoundsMax", "0.0", GraphValueType::Float3, GraphStageAll, "a draw does not provide its bounds"},
			{"SceneColor", "", GraphValueType::Texture2D, GraphStageAll, SURFACE_NO_SCENE},
			{"SceneDepth", "", GraphValueType::Texture2D, GraphStageAll, SURFACE_NO_SCENE},
			{"SceneNormals", "", GraphValueType::Texture2D, GraphStageAll, SURFACE_NO_SCENE},
			{"MotionVectors", "", GraphValueType::Texture2D, GraphStageAll, NO_MOTION_VECTORS},
			{"SkyDirection", "0.0", GraphValueType::Float3, GraphStageAll, SKY_ONLY},
			{"SkySunDirection", "0.0", GraphValueType::Float3, GraphStageAll, SKY_ONLY},
			{"SkySunColor", "0.0", GraphValueType::Float3, GraphStageAll, SKY_ONLY},
			{"SkySunAngularDiameter", "0.0", GraphValueType::Float, GraphStageAll, SKY_ONLY},
			{"SkySunEnabled", "false", GraphValueType::Bool, GraphStageAll, SKY_ONLY},
			{"SkyCapturing", "false", GraphValueType::Bool, GraphStageAll, SKY_ONLY},
			{"SkySunDisc", "0.0", GraphValueType::Float3, GraphStageAll, SKY_ONLY},
		};

		const GraphBinding LIGHTING_BINDINGS[] = {
			{"LightDirection", "light.direction", GraphValueType::Float3, GraphStageLighting},
			{"LightColor", "light.color", GraphValueType::Float3, GraphStageLighting},
			{"LightAttenuation", "light.attenuation", GraphValueType::Float, GraphStageLighting},
			{"LightShadow", "light.shadow", GraphValueType::Float, GraphStageLighting},
			{"SurfaceBaseColor", "surface.baseColor", GraphValueType::Float3, GraphStageLighting},
			{"SurfaceAlpha", "surface.alpha", GraphValueType::Float, GraphStageLighting},
			{"SurfaceMetallic", "surface.metallic", GraphValueType::Float, GraphStageLighting},
			{"SurfaceRoughness", "surface.roughness", GraphValueType::Float, GraphStageLighting},
			{"SurfaceReflectance", "surface.reflectance", GraphValueType::Float, GraphStageLighting},
			{"SurfaceOcclusion", "surface.occlusion", GraphValueType::Float, GraphStageLighting},
			{"SurfaceEmission", "surface.emissive", GraphValueType::Float3, GraphStageLighting},
			{"SurfaceNormal", "normalWS", GraphValueType::Float3, GraphStageLighting},
			{"StandardRadiance", "ShalltearStandardLighting(surface, normalWS, light, input.viewDirectionWS)", GraphValueType::Float3,
			 GraphStageLighting},
		};

		constexpr std::string_view FRAME = R"(template <typename Input>
float3 ShalltearGraphTangent(Input input)
{
	float3 tangent = input.tangentWS.xyz - input.normalWS * dot(input.normalWS, input.tangentWS.xyz);
	float tangentLength = length(tangent);
	return tangentLength < 1e-6 ? float3(0.0, 0.0, 0.0) : tangent / tangentLength;
}

template <typename Input>
float3 ShalltearGraphBitangent(Input input)
{
	return cross(input.normalWS, ShalltearGraphTangent(input)) * input.tangentWS.w;
}

float3 ShalltearGraphWorldToTangent(ShalltearSurfaceInput input, float3 normalWS)
{
	float3 tangent = ShalltearGraphTangent(input);
	if (dot(tangent, tangent) == 0.0)
	{
		return float3(0.0, 0.0, 1.0);
	}
	return float3(dot(normalWS, tangent), dot(normalWS, ShalltearGraphBitangent(input)), dot(normalWS, input.normalWS));
}

template <typename Input>
float3 ShalltearGraphTangentSpaceViewDirection(Input input)
{
	float3 direction = float3(dot(input.viewDirectionWS, ShalltearGraphTangent(input)), dot(input.viewDirectionWS, ShalltearGraphBitangent(input)),
							  dot(input.viewDirectionWS, input.normalWS));
	return direction * rsqrt(max(dot(direction, direction), 1.175494351e-38));
}

template <typename Input>
float4 ShalltearGraphScreenPosition(Input input)
{
	float4 clip = mul(mul(ShalltearProjectionMatrix(), ShalltearViewMatrix()), float4(input.positionWS, 1.0));
	return float4(clip.x * 0.5 + clip.w * 0.5, clip.w * 0.5 - clip.y * 0.5, clip.z, clip.w);
}

float3 ShalltearGraphObjectPosition(ShalltearVertexInput input)
{
	return input.positionOS;
}

float3 ShalltearGraphObjectPosition(ShalltearSurfaceInput input)
{
	return mul(input.worldToObject, float4(input.positionWS, 1.0)).xyz;
}

float3 ShalltearGraphObjectNormal(ShalltearVertexInput input)
{
	return input.normalOS;
}

float3 ShalltearGraphObjectNormal(ShalltearSurfaceInput input)
{
	return normalize(mul(input.normalWS, (float3x3)input.objectToWorld));
}

float3 ShalltearGraphObjectTangent(ShalltearVertexInput input)
{
	return input.tangentOS.xyz;
}

float3 ShalltearGraphObjectTangent(ShalltearSurfaceInput input)
{
	return normalize(mul((float3x3)input.worldToObject, ShalltearGraphTangent(input)));
}

float2 ShalltearGraphPixelPosition(ShalltearVertexInput input)
{
	float4 screen = ShalltearGraphScreenPosition(input);
	return screen.xy / screen.w * ShalltearScreenParameters().xy;
}

float2 ShalltearGraphPixelPosition(ShalltearSurfaceInput input)
{
	return input.positionSS;
}

float2 ShalltearGraphClipPosition(ShalltearVertexInput input)
{
	return mul(mul(ShalltearProjectionMatrix(), ShalltearViewMatrix()), float4(input.positionWS, 1.0)).xy;
}

float2 ShalltearGraphClipPosition(ShalltearSurfaceInput input)
{
	return input.positionSS;
}

float3 ShalltearGraphPredisplacementWS(ShalltearVertexInput input)
{
	return input.positionWS;
}

float3 ShalltearGraphPredisplacementOS(ShalltearVertexInput input)
{
	return input.positionOS;
}

)";

		constexpr const char* FULLSCREEN_NO_SURFACE = "a full-screen draw shades the scene (it has a normal, no tangent frame)";
		constexpr std::string_view FULLSCREEN_IDENTITY = "float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)";
		constexpr GraphStageMask FULLSCREEN = GraphStageFragment;

		const GraphBinding FULLSCREEN_BINDINGS[] = {
			{"UV0", "input.uv", GraphValueType::Float2, FULLSCREEN},
			{"VertexColor", "float4(1.0, 1.0, 1.0, 1.0)", GraphValueType::Float4, FULLSCREEN, "a full-screen draw has no vertices to color"},

			{"WorldSpacePosition", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ObjectSpacePosition", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ViewSpacePosition", "mul(ShalltearPassViewMatrix(), float4(ShalltearFullscreenPositionWS(input), 1.0)).xyz", GraphValueType::Float3, FULLSCREEN},
			{"TangentSpacePosition", "float3(0.0, 0.0, 0.0)", GraphValueType::Float3, FULLSCREEN},
			{"AbsoluteWorldSpacePosition", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"WorldSpacePositionPredisplacement", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ObjectSpacePositionPredisplacement", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ViewSpacePositionPredisplacement", "mul(ShalltearPassViewMatrix(), float4(ShalltearFullscreenPositionWS(input), 1.0)).xyz",
			 GraphValueType::Float3, FULLSCREEN},
			{"TangentSpacePositionPredisplacement", "float3(0.0, 0.0, 0.0)", GraphValueType::Float3, FULLSCREEN},
			{"AbsoluteWorldSpacePositionPredisplacement", "ShalltearFullscreenPositionWS(input)", GraphValueType::Float3, FULLSCREEN},

			{"WorldSpaceNormal", "ShalltearFullscreenNormalWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ObjectSpaceNormal", "ShalltearFullscreenNormalWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ViewSpaceNormal", "mul(ShalltearFullscreenNormalWS(input), (float3x3)ShalltearPassInverseViewMatrix())", GraphValueType::Float3, FULLSCREEN},
			{"TangentSpaceNormal", "float3(0.0, 0.0, 1.0)", GraphValueType::Float3, FULLSCREEN},

			{"WorldSpaceTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"ObjectSpaceTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"ViewSpaceTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"TangentSpaceTangent", "float3(1.0, 0.0, 0.0)", GraphValueType::Float3, FULLSCREEN},

			{"WorldSpaceBiTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"ObjectSpaceBiTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"ViewSpaceBiTangent", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},
			{"TangentSpaceBiTangent", "float3(0.0, 1.0, 0.0)", GraphValueType::Float3, FULLSCREEN},

			{"WorldSpaceViewDirection", "ShalltearFullscreenViewDirectionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ObjectSpaceViewDirection", "ShalltearFullscreenViewDirectionWS(input)", GraphValueType::Float3, FULLSCREEN},
			{"ViewSpaceViewDirection", "mul((float3x3)ShalltearPassViewMatrix(), ShalltearFullscreenViewDirectionWS(input))", GraphValueType::Float3, FULLSCREEN},
			{"TangentSpaceViewDirection", "0.0", GraphValueType::Float3, FULLSCREEN, FULLSCREEN_NO_SURFACE},

			{"ScreenPosition", "float4(input.uv, 0.0, 1.0)", GraphValueType::Float4, FULLSCREEN},
			{"NDCPosition", "input.uv", GraphValueType::Float2, FULLSCREEN},
			{"PixelPosition", "input.positionCS.xy", GraphValueType::Float2, FULLSCREEN},
			{"ClipPosition", "input.positionCS.xy", GraphValueType::Float2, FULLSCREEN},

			{"ObjectToWorld", str(FULLSCREEN_IDENTITY), GraphValueType::Float4x4, FULLSCREEN},
			{"WorldToObject", str(FULLSCREEN_IDENTITY), GraphValueType::Float4x4, FULLSCREEN},
			{"View", "ShalltearPassViewMatrix()", GraphValueType::Float4x4, FULLSCREEN},
			{"InverseView", "ShalltearPassInverseViewMatrix()", GraphValueType::Float4x4, FULLSCREEN},
			{"Projection", "ShalltearPassProjectionMatrix()", GraphValueType::Float4x4, FULLSCREEN},
			{"InverseProjection", "ShalltearPassInverseProjectionMatrix()", GraphValueType::Float4x4, FULLSCREEN},
			{"ViewProjection", "mul(ShalltearPassProjectionMatrix(), ShalltearPassViewMatrix())", GraphValueType::Float4x4, FULLSCREEN},
			{"InverseViewProjection", "ShalltearPassInverseViewProjectionMatrix()", GraphValueType::Float4x4, FULLSCREEN},

			{"CameraPosition", "sltPassView.inverseView[3].xyz", GraphValueType::Float3, FULLSCREEN},
			{"ProjectionParams", "sltPassView.projectionParams", GraphValueType::Float4, FULLSCREEN},
			{"ScreenParams", "sltPassView.screenParams", GraphValueType::Float4, FULLSCREEN},
			{"TimeParameters", "sltPassView.time", GraphValueType::Float4, FULLSCREEN},

			{"InstanceID", "0.0", GraphValueType::Float, FULLSCREEN},
			{"EyeIndex", "0.0", GraphValueType::Float, FULLSCREEN},
			{"VertexID", "0.0", GraphValueType::Float, FULLSCREEN, "a Fullscreen graph has no vertex stage"},
			{"IsFrontFace", "true", GraphValueType::Bool, FULLSCREEN},
			{"SmoothDeltaTime", "0.0", GraphValueType::Float, FULLSCREEN, "the frame clock has no smoothed delta"},
			{"RendererBoundsMin", "0.0", GraphValueType::Float3, FULLSCREEN, "a full-screen draw has no bounds"},
			{"RendererBoundsMax", "0.0", GraphValueType::Float3, FULLSCREEN, "a full-screen draw has no bounds"},
			{"SceneColor", "sltSceneColor", GraphValueType::Texture2D, FULLSCREEN, {}, "sltSceneSampler"},
			{"SceneDepth", "sltSceneDepth", GraphValueType::Texture2D, FULLSCREEN, {}, "sltSceneSampler"},
			{"SceneNormals", "sltSceneNormals", GraphValueType::Texture2D, FULLSCREEN, {}, "sltSceneSampler"},
			{"MotionVectors", "", GraphValueType::Texture2D, FULLSCREEN, NO_MOTION_VECTORS},
			{"SkyDirection", "0.0", GraphValueType::Float3, FULLSCREEN, SKY_ONLY},
			{"SkySunDirection", "0.0", GraphValueType::Float3, FULLSCREEN, SKY_ONLY},
			{"SkySunColor", "0.0", GraphValueType::Float3, FULLSCREEN, SKY_ONLY},
			{"SkySunAngularDiameter", "0.0", GraphValueType::Float, FULLSCREEN, SKY_ONLY},
			{"SkySunEnabled", "false", GraphValueType::Bool, FULLSCREEN, SKY_ONLY},
			{"SkyCapturing", "false", GraphValueType::Bool, FULLSCREEN, SKY_ONLY},
			{"SkySunDisc", "0.0", GraphValueType::Float3, FULLSCREEN, SKY_ONLY},
		};

		struct FullscreenResource {
			std::string_view name;
			std::array<std::string_view, 2> needs;
			std::string_view text;
		};

		constexpr FullscreenResource FULLSCREEN_RESOURCES[] = {
			{"sltPassView", {}, "#include \"ViewData.hlsli\"\n"
								"[[vk::binding(0, 2)]] cbuffer sltViewRecord\n{\n\tShalltearViewData sltPassView;\n};\n\n"},
			{"ShalltearPass", {"sltPassView"}, "float4x4 ShalltearPassViewMatrix() { return ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]); }\n"
											   "float4x4 ShalltearPassInverseViewMatrix()\n{\n"
											   "\treturn ShalltearViewDataMatrix(sltPassView.inverseView[0], sltPassView.inverseView[1], sltPassView.inverseView[2], sltPassView.inverseView[3]);\n}\n"
											   "float4x4 ShalltearPassProjectionMatrix()\n{\n"
											   "\treturn ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1], sltPassView.projection[2], sltPassView.projection[3]);\n}\n"
											   "float4x4 ShalltearPassInverseProjectionMatrix()\n{\n"
											   "\treturn ShalltearViewDataMatrix(sltPassView.inverseProjection[0], sltPassView.inverseProjection[1], sltPassView.inverseProjection[2],\n"
											   "\t\tsltPassView.inverseProjection[3]);\n}\n"
											   "float4x4 ShalltearPassInverseViewProjectionMatrix()\n{\n"
											   "\treturn ShalltearViewDataMatrix(sltPassView.inverseViewProjection[0], sltPassView.inverseViewProjection[1],\n"
											   "\t\tsltPassView.inverseViewProjection[2], sltPassView.inverseViewProjection[3]);\n}\n\n"},
			{"sltSceneSampler", {}, "[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"},
			{"sltSceneDepth", {"sltSceneSampler"}, "[[vk::binding(2, 2)]] Texture2D sltSceneDepth;\n"},
			{"sltSceneNormals", {"sltSceneSampler"}, "[[vk::binding(3, 2)]] Texture2D sltSceneNormals;\n"},
			{"sltSceneColor", {"sltSceneSampler"}, "[[vk::binding(4, 2)]] Texture2D sltSceneColor;\n"},
			{"ShalltearFullscreenPositionWS", {"sltSceneDepth", "ShalltearPass"}, "\nfloat3 ShalltearFullscreenPositionWS(ShalltearFullscreenVaryings input)\n{\n"
																				  "\tfloat depth = sltSceneDepth.SampleLevel(sltSceneSampler, input.uv, 0.0).r;\n"
																				  "\tfloat4 world = mul(ShalltearPassInverseViewProjectionMatrix(), float4(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0, depth, 1.0));\n"
																				  "\treturn world.xyz / world.w;\n}\n"},
			{"ShalltearFullscreenNormalWS", {"sltSceneNormals"}, "\nfloat3 ShalltearFullscreenNormalWS(ShalltearFullscreenVaryings input)\n{\n"
																 "\treturn normalize(sltSceneNormals.SampleLevel(sltSceneSampler, input.uv, 0.0).rgb * 2.0 - 1.0);\n}\n"},
			{"ShalltearFullscreenViewDirectionWS", {"ShalltearPass"}, "\nfloat3 ShalltearFullscreenViewDirectionWS(ShalltearFullscreenVaryings input)\n{\n"
																	  "\tif (sltPassView.projectionParams.z != 0.0) {\n"
																	  "\t\treturn -normalize(sltPassView.inverseView[2].xyz);\n\t}\n"
																	  "\tfloat4 far = mul(ShalltearPassInverseViewProjectionMatrix(), float4(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0, 0.0, 1.0));\n"
																	  "\treturn normalize(sltPassView.inverseView[3].xyz - far.xyz / far.w);\n}\n"},
		};

		constexpr std::string_view FULLSCREEN_TRIANGLE = R"(struct ShalltearFullscreenVaryings
{
	float4 positionCS : SV_Position;
	float2 uv : TEXCOORD0;
};

ShalltearFullscreenVaryings VSMain(uint vertexId : SV_VertexID)
{
	ShalltearFullscreenVaryings output;
	float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
	output.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);
	output.uv = float2(corner.x, 1.0 - corner.y);
	return output;
}
)";

		constexpr const char* SKY_NO_POSITION = "a sky lies at infinity along its ray (no position), the Sky node's Direction is the ray";
		constexpr const char* SKY_NO_SURFACE = "a sky has no surface/mesh, the Sky node's Direction is the ray";
		constexpr GraphStageMask SKY = GraphStageFragment;

		const GraphBinding SKY_BINDINGS[] = {
			{"UV0", "0.0", GraphValueType::Float2, SKY, SKY_NO_SURFACE},
			{"VertexColor", "0.0", GraphValueType::Float4, SKY, SKY_NO_SURFACE},

			{"WorldSpacePosition", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"ObjectSpacePosition", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"ViewSpacePosition", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"TangentSpacePosition", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"AbsoluteWorldSpacePosition", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"WorldSpacePositionPredisplacement", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"ObjectSpacePositionPredisplacement", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"ViewSpacePositionPredisplacement", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"TangentSpacePositionPredisplacement", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},
			{"AbsoluteWorldSpacePositionPredisplacement", "0.0", GraphValueType::Float3, SKY, SKY_NO_POSITION},

			{"WorldSpaceNormal", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ObjectSpaceNormal", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ViewSpaceNormal", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"TangentSpaceNormal", "float3(0.0, 0.0, 1.0)", GraphValueType::Float3, SKY},

			{"WorldSpaceTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ObjectSpaceTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ViewSpaceTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"TangentSpaceTangent", "float3(1.0, 0.0, 0.0)", GraphValueType::Float3, SKY},

			{"WorldSpaceBiTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ObjectSpaceBiTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"ViewSpaceBiTangent", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},
			{"TangentSpaceBiTangent", "float3(0.0, 1.0, 0.0)", GraphValueType::Float3, SKY},

			{"WorldSpaceViewDirection", "(-input.directionWS)", GraphValueType::Float3, SKY},
			{"ObjectSpaceViewDirection", "(-input.directionWS)", GraphValueType::Float3, SKY},
			{"ViewSpaceViewDirection", "mul((float3x3)ShalltearViewMatrix(), -input.directionWS)", GraphValueType::Float3, SKY},
			{"TangentSpaceViewDirection", "0.0", GraphValueType::Float3, SKY, SKY_NO_SURFACE},

			{"ScreenPosition", "float4(input.screenUV, 0.0, 1.0)", GraphValueType::Float4, SKY},
			{"NDCPosition", "input.screenUV", GraphValueType::Float2, SKY},
			{"PixelPosition", "(input.screenUV * ShalltearScreenParameters().xy)", GraphValueType::Float2, SKY},
			{"ClipPosition", "(input.screenUV * ShalltearScreenParameters().xy)", GraphValueType::Float2, SKY},

			{"ObjectToWorld", str(FULLSCREEN_IDENTITY), GraphValueType::Float4x4, SKY},
			{"WorldToObject", str(FULLSCREEN_IDENTITY), GraphValueType::Float4x4, SKY},
			{"View", "ShalltearViewMatrix()", GraphValueType::Float4x4, SKY},
			{"InverseView", "ShalltearInverseViewMatrix()", GraphValueType::Float4x4, SKY},
			{"Projection", "ShalltearProjectionMatrix()", GraphValueType::Float4x4, SKY},
			{"InverseProjection", "ShalltearInverseProjectionMatrix()", GraphValueType::Float4x4, SKY},
			{"ViewProjection", "mul(ShalltearProjectionMatrix(), ShalltearViewMatrix())", GraphValueType::Float4x4, SKY},
			{"InverseViewProjection", "ShalltearInverseViewProjectionMatrix()", GraphValueType::Float4x4, SKY},

			{"CameraPosition", "input.cameraPositionWS", GraphValueType::Float3, SKY},
			{"ProjectionParams", "ShalltearProjectionParameters()", GraphValueType::Float4, SKY},
			{"ScreenParams", "ShalltearScreenParameters()", GraphValueType::Float4, SKY},
			{"TimeParameters", "input.time", GraphValueType::Float4, SKY},

			{"InstanceID", "0.0", GraphValueType::Float, SKY},
			{"EyeIndex", "0.0", GraphValueType::Float, SKY},
			{"VertexID", "0.0", GraphValueType::Float, SKY, "a Sky graph has no vertex stage"},
			{"IsFrontFace", "true", GraphValueType::Bool, SKY},
			{"SmoothDeltaTime", "0.0", GraphValueType::Float, SKY, "the frame clock has no smoothed delta"},
			{"RendererBoundsMin", "0.0", GraphValueType::Float3, SKY, "a sky has no bounds"},
			{"RendererBoundsMax", "0.0", GraphValueType::Float3, SKY, "a sky has no bounds"},
			{"SceneColor", "", GraphValueType::Texture2D, SKY, "a sky's draw is handed no image of the scene"},
			{"SceneDepth", "", GraphValueType::Texture2D, SKY, "a sky's draw is handed no image of the scene"},
			{"SceneNormals", "", GraphValueType::Texture2D, SKY, "a sky's draw is handed no image of the scene"},
			{"MotionVectors", "", GraphValueType::Texture2D, SKY, NO_MOTION_VECTORS},

			{"SkyDirection", "input.directionWS", GraphValueType::Float3, SKY},
			{"SkySunDirection", "input.sunDirectionWS", GraphValueType::Float3, SKY},
			{"SkySunColor", "input.sunColor", GraphValueType::Float3, SKY},
			{"SkySunAngularDiameter", "input.sunAngularDiameter", GraphValueType::Float, SKY},
			{"SkySunEnabled", "input.sunEnabled", GraphValueType::Bool, SKY},
			{"SkyCapturing", "input.capturing", GraphValueType::Bool, SKY},
			{"SkySunDisc", "ShalltearSkySunDisc(input)", GraphValueType::Float3, SKY},
		};

		str Number(f32 value) {
			char text[32];
			std::to_chars_result result = std::to_chars(text, text + sizeof(text), value);
			return str(text, result.ptr);
		}

		u32 Components(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Vector2:
				return 2;
			case GraphPropertyKind::Vector3:
				return 3;
			case GraphPropertyKind::Vector4:
			case GraphPropertyKind::Color:
				return 4;
			case GraphPropertyKind::Texture2D:
			case GraphPropertyKind::Texture2DArray:
			case GraphPropertyKind::Texture3D:
			case GraphPropertyKind::Cubemap:
				return 0;
			default:
				return 1;
			}
		}

		constexpr std::string_view RADIANCE_BLOCK = GraphSurface::RADIANCE_BLOCK_ID;

		str NodeSettingText(const GraphNode& node, std::string_view key) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key == key) {
					LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
					JSONValue value = JSONParse(setting.value, arena);
					return value.IsString() ? str(value.AsString()) : str();
				}
			}
			return {};
		}

		struct VertexBlock {
			std::string_view id;
			std::string_view field;
		};

		constexpr VertexBlock VERTEX_BLOCKS[] = {
			{"Block/Position", "positionOS"},
			{"Block/Normal", "normalOS"},
			{"Block/Tangent", "tangentOS"},
		};

		constexpr u32 MAX_INTERPOLATOR_SLOTS = 16;

		constexpr std::string_view INTERPOLATOR_COUNTS[] = {"SHALLTEAR_INTERPOLATORS_LINEAR", "SHALLTEAR_INTERPOLATORS_NO_PERSPECTIVE",
															"SHALLTEAR_INTERPOLATORS_FLAT"};

		struct Interpolant {
			u32 block = 0;
			u32 node = 0;
			str name;
			u32 width = 4;
			u32 mode = 0;
			u32 slot = 0;
			u32 offset = 0;
		};

		std::string_view ComponentsAt(u32 offset, u32 width) { return std::string_view("xyzw").substr(offset, width); }

		void Pack(Vector<Interpolant>& interpolants, u32 (&counts)[3], size_t& overflow) {
			Vector<u32> used[3] = {Vector<u32>{GetMemoryResource(MemTag::Editor)}, Vector<u32>{GetMemoryResource(MemTag::Editor)},
								   Vector<u32>{GetMemoryResource(MemTag::Editor)}};
			Vector<u32> local{GetMemoryResource(MemTag::Editor)};
			for (Interpolant& interpolant : interpolants) {
				Vector<u32>& slots = used[interpolant.mode];
				u32 slot = 0;
				while (slot < slots.size() && slots[slot] + interpolant.width > 4) {
					++slot;
				}
				if (slot == slots.size()) {
					slots.push_back(0);
				}
				interpolant.offset = slots[slot];
				slots[slot] += interpolant.width;
				local.push_back(slot);
			}
			for (u32 mode = 0; mode < 3; ++mode) {
				counts[mode] = static_cast<u32>(used[mode].size());
			}
			overflow = SIZE_MAX;
			for (size_t i = 0; i < interpolants.size(); ++i) {
				Interpolant& interpolant = interpolants[i];
				u32 base = 0;
				for (u32 mode = 0; mode < interpolant.mode; ++mode) {
					base += counts[mode];
				}
				interpolant.slot = base + local[i];
				if (interpolant.slot >= MAX_INTERPOLATOR_SLOTS && overflow == SIZE_MAX) {
					overflow = i;
				}
			}
		}

		str InlineInterpolator(const GraphNode& block, u32 width) {
			f32 values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			for (const GraphInput& input : block.inputs) {
				if (input.port != "Value" || input.value.empty()) {
					continue;
				}
				LinearAllocator arena(input.value.size() + 64, MemTag::Editor);
				JSONValue json = JSONParse(input.value, arena);
				if (json.IsNumber()) {
					values[0] = static_cast<f32>(json.AsNumber());
				}
				for (size_t i = 0; json.IsArray() && i < json.Size() && i < 4; ++i) {
					values[i] = static_cast<f32>(json[i].AsNumber(0.0));
				}
			}
			if (width == 1) {
				return GraphHLSL::FloatLiteral(values[0]);
			}
			str text = "float" + std::to_string(width) + "(";
			for (u32 i = 0; i < width; ++i) {
				text += (i == 0 ? "" : ", ") + GraphHLSL::FloatLiteral(values[i]);
			}
			return text + ")";
		}

		str PropertyPragmas(std::span<const GraphPropertyInfo> properties) {
			str text;
			for (const GraphPropertyInfo& property : properties) {
				if (GraphProperties::IsKeyword(property.kind)) {
					const str member = GraphProperties::MemberName(property);
					if (property.kind == GraphPropertyKind::BooleanKeyword) {
						text += "#pragma shalltear_keyword " + property.reference + " " + member + "\n";
					}
					for (size_t i = 0; i < property.entries.size(); ++i) {
						text += "#pragma shalltear_keyword " + GraphProperties::EntryKeyword(property, i) + " " + member + " " + std::to_string(i) +
								" \"" + property.entries[i].name + "\"\n";
					}
					const i32 value =
						property.kind == GraphPropertyKind::EnumKeyword ? static_cast<i32>(property.defaultValue[0]) : (property.defaultValue[0] != 0.0f ? 1 : 0);
					text += "#pragma shalltear_default " + member + " " + std::to_string(value) + "\n";
					text += "#pragma shalltear_hint " + member + " hidden\n";
					if (!property.name.empty()) {
						text += "#pragma shalltear_label " + member + " \"" + property.name + "\"\n";
					}
					continue;
				}
				if (property.kind == GraphPropertyKind::Texture2D) {
					text += "#pragma shalltear_default " + property.reference + " " + property.fallback + "\n";
				} else if (!GraphProperties::IsTexture(property.kind)) {
					text += "#pragma shalltear_default " + property.reference;
					for (u32 i = 0; i < Components(property.kind); ++i) {
						text += " " + Number(property.defaultValue[i]);
					}
					text += "\n";
				}
				if (property.slider) {
					text += "#pragma shalltear_range " + property.reference + " " + Number(property.minValue) + " " + Number(property.maxValue) + "\n";
				}
				std::string_view hint;
				if (property.hidden) {
					hint = "hidden";
				} else if (property.kind == GraphPropertyKind::Color) {
					hint = property.hdr ? "hdr" : "color";
				} else if (property.kind == GraphPropertyKind::Boolean) {
					hint = "toggle";
				}
				if (!hint.empty()) {
					text += "#pragma shalltear_hint " + property.reference + " " + str(hint) + "\n";
				}
				if (!property.name.empty()) {
					text += "#pragma shalltear_label " + property.reference + " \"" + property.name + "\"\n";
				}
			}
			Vector<str> groups{GetMemoryResource(MemTag::Editor)};
			for (const GraphPropertyInfo& property : properties) {
				if (property.group.empty() || GraphProperties::IsKeyword(property.kind) ||
					std::find(groups.begin(), groups.end(), property.group) != groups.end()) {
					continue;
				}
				groups.push_back(property.group);
				text += "#pragma shalltear_group \"" + property.group + "\"";
				for (const GraphPropertyInfo& member : properties) {
					text += member.group == property.group && !GraphProperties::IsKeyword(member.kind) ? " " + member.reference : str();
				}
				text += "\n";
			}
			return text;
		}

		bool IsWired(const GraphNode& block) {
			return std::any_of(block.inputs.begin(), block.inputs.end(), [](const GraphInput& input) { return input.fromNode != 0; });
		}

		void NeededResources(const GraphProgram& program, std::array<bool, std::size(FULLSCREEN_RESOURCES)>& out) {
			out.fill(false);
			for (size_t i = 0; i < std::size(FULLSCREEN_RESOURCES); ++i) {
				for (const GraphExternal& external : program.externals) {
					out[i] = out[i] || external.expression.find(FULLSCREEN_RESOURCES[i].name) != str::npos;
				}
			}
			for (size_t i = std::size(FULLSCREEN_RESOURCES); i-- > 0;) {
				for (std::string_view need : FULLSCREEN_RESOURCES[i].needs) {
					for (size_t j = 0; j < i && out[i] && !need.empty(); ++j) {
						out[j] = out[j] || FULLSCREEN_RESOURCES[j].name == need;
					}
				}
			}
		}

		bool EmitFullscreen(const GraphDocument& document, std::string_view sourceName, const Settings& settings,
							std::span<const GraphPropertyInfo> properties, std::span<const GraphCompileRoot> roots, std::span<const Block* const> rooted,
							GraphDefinitions& resolver, str& out, Vector<GraphDiagnostic>& diagnostics, Vector<u32>* lines) {
			GraphCompileRequest request;
			request.roots = roots;
			request.bindings = FULLSCREEN_BINDINGS;
			request.definitions = &resolver;
			GraphProgram program;
			if (!GraphCompiler::Compile(document, request, program, diagnostics)) {
				return false;
			}
			GraphHLSLText fragmentText;
			GraphHLSL::Emit(program, GraphStageFragment, fragmentText);

			str text;
			text += "// generated by Node Shader from " + str(sourceName) + ". Do not edit manually\n";
			text += "#pragma shalltear_blend " + str(RHI::BlendModeName(settings.blend)) + "\n";
			text += "#pragma shalltear_depth off\n";
			text += "#pragma shalltear_cull off\n";
			text += PropertyPragmas(properties);
			text += GraphProperties::Declarations(properties);
			text += FULLSCREEN_TRIANGLE;
			text += GraphSurface::PassDeclarations(program);
			Vector<LineSegment> segments{GetMemoryResource(MemTag::Editor)};
			text += "\n";
			segments.push_back({LineOf(text), &fragmentText.functionLineNodes});
			text += fragmentText.functions;
			text += "float4 PSMain(ShalltearFullscreenVaryings input) : SV_Target {\n";
			segments.push_back({LineOf(text), &fragmentText.lineNodes});
			text += fragmentText.body;
			str color = "float3(0.5, 0.5, 0.5)";
			str alpha = "1.0";
			for (size_t i = 0; i < rooted.size(); ++i) {
				(rooted[i]->id == "Block/Alpha" ? alpha : color) = fragmentText.roots[i];
			}
			text += "\treturn float4(" + color + ", " + alpha + ");\n";
			text += "}\n";
			LayLines(text, segments, lines);
			out = std::move(text);
			return true;
		}

		bool EmitSky(std::string_view sourceName, std::span<const GraphPropertyInfo> properties, const GraphDocument& document,
					 std::span<const GraphCompileRoot> roots, std::span<const Block* const> rooted, GraphDefinitions& resolver, str& out,
					 Vector<GraphDiagnostic>& diagnostics, Vector<u32>* lines) {
			GraphCompileRequest request;
			request.roots = roots;
			request.bindings = SKY_BINDINGS;
			request.definitions = &resolver;
			GraphProgram program;
			if (!GraphCompiler::Compile(document, request, program, diagnostics)) {
				return false;
			}
			GraphHLSLText fragmentText;
			GraphHLSL::Emit(program, GraphStageFragment, fragmentText);

			str text;
			text += "// generated by Node Shader from " + str(sourceName) + ". Do not edit manually\n";
			text += "#pragma shalltear_template sky\n";
			text += PropertyPragmas(properties);
			text += GraphProperties::Declarations(properties);
			Vector<LineSegment> segments{GetMemoryResource(MemTag::Editor)};
			text += "\n";
			segments.push_back({LineOf(text), &fragmentText.functionLineNodes});
			text += fragmentText.functions;
			text += "void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {\n";
			segments.push_back({LineOf(text), &fragmentText.lineNodes});
			text += fragmentText.body;
			for (size_t i = 0; i < rooted.size(); ++i) {
				text += "\tsky.color = " + fragmentText.roots[i] + ";\n";
			}
			text += "}\n";
			LayLines(text, segments, lines);
			out = std::move(text);
			return true;
		}

		struct StackRead {
			Settings settings;
			Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
			const GraphContext* fragment = nullptr;
			const GraphContext* vertex = nullptr;
			const GraphContext* lighting = nullptr;
			Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
			Vector<const Block*> rooted{GetMemoryResource(MemTag::Editor)};
		};

		void RootBlock(Vector<GraphCompileRoot>& roots, const GraphNode& node, GraphStageMask stage) {
			GraphCompileRoot root;
			root.node = node.id;
			root.port = "Value";
			root.stage = stage;
			root.input = true;
			roots.push_back(std::move(root));
		}

		void ReadStack(const GraphDocument& document, GraphDefinitions& resolver, StackRead& stack, Vector<GraphDiagnostic>& diagnostics) {
			const Settings& settings = stack.settings;
			ReadSettings(document, stack.settings, diagnostics);

			GraphProperties::ParseBlackboard(document, stack.properties, diagnostics);
			GraphSubgraphs::CollectKeywords(document, resolver, stack.properties, diagnostics);

			for (const GraphContext& context : document.contexts) {
				if (context.node.def == "Context/Fragment") {
					if (stack.fragment != nullptr) {
						diagnostics.push_back({context.node.id, "is a 2nd Fragment context (a shader has 1)"});
					}
					stack.fragment = &context;
				} else if (context.node.def == "Context/Vertex") {
					if (stack.vertex != nullptr) {
						diagnostics.push_back({context.node.id, "is a 2nd Vertex context (a shader has 1)"});
					}
					stack.vertex = &context;
				} else if (context.node.def == "Context/Lighting") {
					if (stack.lighting != nullptr) {
						diagnostics.push_back({context.node.id, "is a 2nd Lighting context (a shader has 1)"});
					}
					stack.lighting = &context;
				}
			}
			if (stack.fragment == nullptr) {
				diagnostics.push_back({0, "the graph has no Fragment context (nothing writes the surface)"});
			}

			if (stack.fragment != nullptr) {
				for (const GraphNode& node : stack.fragment->blocks) {
					const Block* block = nullptr;
					for (const Block& candidate : FRAGMENT_BLOCKS) {
						block = candidate.id == node.def ? &candidate : block;
					}
					if (block == nullptr) {
						diagnostics.push_back({node.id, GraphNodeLibrary::Find(node.def) != nullptr
															? str("is not a Fragment block")
															: "has no definition '" + node.def + "' (unchanged, compiles nothing)"});
						continue;
					}
					bool twice = false;
					for (const GraphNode& other : stack.fragment->blocks) {
						twice = twice || (other.def == node.def && other.id < node.id);
					}
					if (twice) {
						diagnostics.push_back({node.id, "is a 2nd " + node.def + " on the stack. 1 block writes 1 field"});
						continue;
					}
					if (!Active(*block, settings)) {
						continue;
					}
					RootBlock(stack.roots, node, GraphStageFragment);
					stack.rooted.push_back(block);
				}
			}
		}

		bool IsObjectBinding(std::string_view name) {
			return name.starts_with("ObjectSpace") || name == "ObjectToWorld" || name == "WorldToObject";
		}

		bool Functions(const GraphDocument& document, StackRead& stack, GraphDefinitions& resolver, GraphSurfaceFunctions& out,
					   Vector<GraphDiagnostic>& diagnostics, size_t firstDiagnostic) {
			const Settings& settings = stack.settings;
			Vector<GraphCompileRoot>& roots = stack.roots;
			const Vector<const Block*>& rooted = stack.rooted;
			const GraphContext* vertex = stack.vertex;
			const GraphContext* lighting = stack.lighting;

			Vector<const VertexBlock*> vertexRooted{GetMemoryResource(MemTag::Editor)};
			Vector<Interpolant> interpolants{GetMemoryResource(MemTag::Editor)};
			Vector<GraphBinding> bindings{SURFACE_BINDINGS, SURFACE_BINDINGS + std::size(SURFACE_BINDINGS), GetMemoryResource(MemTag::Editor)};
			if (vertex != nullptr) {
				Vector<str> names{GetMemoryResource(MemTag::Editor)};
				for (const GraphNode& node : vertex->blocks) {
					if (node.def == GraphSurface::INTERPOLATOR_BLOCK) {
						Interpolant interpolant;
						interpolant.block = node.id;
						interpolant.node = node.id;
						interpolant.name = NodeSettingText(node, "Name");
						const str type = NodeSettingText(node, "Type");
						const str mode = NodeSettingText(node, "Interpolation");
						u32 typeIndex = type.empty() ? 3u : static_cast<u32>(std::size(GRAPH_INTERPOLATOR_TYPES));
						u32 modeIndex = mode.empty() ? 0u : static_cast<u32>(std::size(GRAPH_INTERPOLATOR_MODES));
						for (u32 i = 0; i < std::size(GRAPH_INTERPOLATOR_TYPES); ++i) {
							typeIndex = GRAPH_INTERPOLATOR_TYPES[i] == type ? i : typeIndex;
						}
						for (u32 i = 0; i < std::size(GRAPH_INTERPOLATOR_MODES); ++i) {
							modeIndex = GRAPH_INTERPOLATOR_MODES[i] == mode ? i : modeIndex;
						}
						if (interpolant.name.empty()) {
							diagnostics.push_back({node.id, "is a custom interpolator with no name (a node finds its interpolator by name)"});
							continue;
						}
						if (std::find(names.begin(), names.end(), interpolant.name) != names.end()) {
							diagnostics.push_back({node.id, "is a 2nd custom interpolator named '" + interpolant.name + "'"});
							continue;
						}
						if (typeIndex == std::size(GRAPH_INTERPOLATOR_TYPES) || modeIndex == std::size(GRAPH_INTERPOLATOR_MODES)) {
							diagnostics.push_back({node.id, "has a Type or Interpolation that isn't one of its options"});
							continue;
						}
						names.push_back(interpolant.name);
						interpolant.width = typeIndex + 1;
						interpolant.mode = modeIndex;
						if (IsWired(node)) {
							interpolants.push_back(std::move(interpolant));
						} else {
							bindings.push_back({"Block/" + interpolant.name, InlineInterpolator(node, interpolant.width), GraphTypes::Vector(interpolant.width),
												static_cast<GraphStageMask>(GraphStageFragment | GraphStageLighting)});
						}
						continue;
					}
					const VertexBlock* block = nullptr;
					for (const VertexBlock& candidate : VERTEX_BLOCKS) {
						block = candidate.id == node.def ? &candidate : block;
					}
					if (block == nullptr) {
						diagnostics.push_back({node.id, GraphNodeLibrary::Find(node.def) != nullptr
															? str("is not a Vertex block")
															: "has no definition '" + node.def + "' (unchanged, compiles nothing)"});
						continue;
					}
					bool twice = false;
					for (const GraphNode& other : vertex->blocks) {
						twice = twice || (other.def == node.def && other.id < node.id);
					}
					if (twice) {
						diagnostics.push_back({node.id, "is a 2nd " + node.def + " on the stack. 1 block writes 1 field"});
						continue;
					}
					if (IsWired(node)) {
						RootBlock(roots, node, GraphStageVertex);
						vertexRooted.push_back(block);
					}
				}
			}

			for (const GraphNode& node : document.nodes) {
				if (node.def == "Input/Position" && NodeSettingText(node, "Source") == "Predisplacement") {
					Interpolant predisplacement;
					predisplacement.node = node.id;
					predisplacement.width = 3;
					interpolants.push_back(std::move(predisplacement));
					break;
				}
			}
			u32 counts[3] = {0, 0, 0};
			size_t overflow = SIZE_MAX;
			Pack(interpolants, counts, overflow);
			if (overflow != SIZE_MAX) {
				diagnostics.push_back({interpolants[overflow].node, "needs an interpolator slot past the " + std::to_string(MAX_INTERPOLATOR_SLOTS) +
																		" a shader has. Narrower types share a slot, fewer modes share more"});
			}
			if (diagnostics.size() != firstDiagnostic) {
				return false;
			}

			Vector<size_t> interpolantRoots{GetMemoryResource(MemTag::Editor)};
			const Interpolant* predisplacement = nullptr;
			for (const Interpolant& interpolant : interpolants) {
				if (interpolant.block == 0) {
					predisplacement = &interpolant;
					interpolantRoots.push_back(SIZE_MAX);
					continue;
				}
				bindings.push_back({"Block/" + interpolant.name,
									"input.interpolators[" + std::to_string(interpolant.slot) + "]." + str(ComponentsAt(interpolant.offset, interpolant.width)),
									GraphTypes::Vector(interpolant.width), static_cast<GraphStageMask>(GraphStageFragment | GraphStageLighting)});
				interpolantRoots.push_back(roots.size());
				const GraphNode* block = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), interpolant.block);
				RootBlock(roots, *block, GraphStageVertex);
			}

			size_t radianceRoot = SIZE_MAX;
			if (lighting != nullptr) {
				for (const GraphNode& node : lighting->blocks) {
					if (node.def != RADIANCE_BLOCK) {
						diagnostics.push_back({node.id, GraphNodeLibrary::Find(node.def) != nullptr
															? str("is not a Lighting block")
															: "has no definition '" + node.def + "' (unchanged, compiles nothing)"});
						continue;
					}
					if (radianceRoot != SIZE_MAX) {
						diagnostics.push_back({node.id, "is a 2nd Radiance on the stack. 1 block writes 1 field"});
						continue;
					}
					if (settings.target == Target::CustomLighting) {
						radianceRoot = roots.size();
						RootBlock(roots, node, GraphStageLighting);
					}
				}
				if (diagnostics.size() != firstDiagnostic) {
					return false;
				}
			}
			bindings.insert(bindings.end(), std::begin(LIGHTING_BINDINGS), std::end(LIGHTING_BINDINGS));

			GraphCompileRequest request;
			request.roots = roots;
			request.bindings = bindings;
			request.definitions = &resolver;
			GraphProgram program;
			if (!GraphCompiler::Compile(document, request, program, diagnostics)) {
				return false;
			}
			GraphHLSLText fragmentText;
			GraphHLSL::Emit(program, GraphStageFragment, fragmentText);
			GraphHLSLText vertexText;
			GraphHLSL::Emit(program, GraphStageVertex, vertexText);
			GraphHLSLText lightingText;
			GraphHLSL::Emit(program, GraphStageLighting, lightingText);
			GraphHLSLText both;
			GraphHLSL::Emit(program, GraphStageAll, both);
			out.helpers = both.helpers;
			const bool vertexFunction = !vertexRooted.empty() || !interpolants.empty();

			out.target = settings.target == Target::Unlit			 ? GraphSurfaceFunctions::Target::Unlit
						 : settings.target == Target::CustomLighting ? GraphSurfaceFunctions::Target::CustomLighting
																	 : GraphSurfaceFunctions::Target::Lit;
			out.blend = settings.blend;
			out.cull = settings.cull;
			out.depth = settings.depth;
			out.supportsEffects = SettingToggle(document, SUPPORT_EFFECTS);
			out.properties = stack.properties;
			out.propertyStages.assign(out.properties.size(), GraphStageNone);
			out.fragmentReadsObject = false;
			for (const GraphValue& value : program.values) {
				if (value.op != GraphOp::External) {
					continue;
				}
				const str& name = program.externals[value.payload].name;
				for (size_t i = 0; i < out.properties.size(); ++i) {
					if (out.properties[i].reference == name) {
						out.propertyStages[i] = static_cast<GraphStageMask>(out.propertyStages[i] | value.stages);
					}
				}
				if (IsObjectBinding(name) && (value.stages & (GraphStageFragment | GraphStageLighting)) != 0) {
					out.fragmentReadsObject = true;
				}
			}
			out.vertexFunction = vertexFunction;
			for (u32 mode = 0; mode < 3; ++mode) {
				out.interpolators[mode] = vertexFunction ? counts[mode] : 0u;
			}

			str text;
			Vector<LineSegment> segments{GetMemoryResource(MemTag::Editor)};
			segments.push_back({LineOf(text), &both.functionLineNodes});
			text += both.functions;
			text += FRAME;
			if (predisplacement != nullptr) {
				const str read = "input.interpolators[" + std::to_string(predisplacement->slot) + "]." +
								 str(ComponentsAt(predisplacement->offset, predisplacement->width));
				text += "float3 ShalltearGraphPredisplacementWS(ShalltearSurfaceInput input)\n{\n\treturn " + read + ";\n}\n\n";
				text += "float3 ShalltearGraphPredisplacementOS(ShalltearSurfaceInput input)\n{\n"
						"\treturn mul(input.worldToObject, float4(ShalltearGraphPredisplacementWS(input), 1.0)).xyz;\n}\n\n";
			}

			if (vertexFunction) {
				text += "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n";
				segments.push_back({LineOf(text), &vertexText.lineNodes});
				text += vertexText.body;
				size_t root = rooted.size();
				for (const VertexBlock* block : vertexRooted) {
					text += "\tvertex." + str(block->field) + " = " + vertexText.roots[root++] + ";\n";
				}
				for (size_t i = 0; i < interpolants.size(); ++i) {
					const Interpolant& interpolant = interpolants[i];
					const str slot = "\tvertex.interpolators[" + std::to_string(interpolant.slot) + "]." + str(ComponentsAt(interpolant.offset, interpolant.width));
					if (interpolant.block == 0) {
						text += slot + " = input.positionWS;\n";
					} else {
						text += slot + " = (" + vertexText.roots[interpolantRoots[i]] + ")." + str(ComponentsAt(0, interpolant.width)) + ";\n";
					}
				}
				text += "}\n";
			}

			if (settings.target == Target::CustomLighting) {
				text += "float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n";
				segments.push_back({LineOf(text), &lightingText.lineNodes});
				text += lightingText.body;
				text += radianceRoot != SIZE_MAX ? "\treturn " + lightingText.roots[radianceRoot] + ";\n"
												 : "\treturn ShalltearStandardLighting(surface, normalWS, light, input.viewDirectionWS);\n";
				text += "}\n";
			}

			text += "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n";
			segments.push_back({LineOf(text), &fragmentText.lineNodes});
			text += fragmentText.body;
			for (size_t i = 0; i < rooted.size(); ++i) {
				const str& value = fragmentText.roots[i];
				text += "\tsurface." + str(rooted[i]->field) + " = ";
				text += rooted[i]->id == "Block/NormalWS" ? "ShalltearGraphWorldToTangent(input, " + value + ")" : value;
				text += ";\n";
			}
			text += "}\n";
			LayLines(text, segments, &out.lines);
			out.text = std::move(text);
			return true;
		}

	} // anonymous namespace

	namespace GraphSurface {

		bool Generate(const GraphDocument& document, std::string_view sourceName, str& out, Vector<GraphDiagnostic>& diagnostics,
					  GraphDefinitions* definitions, Vector<u32>* lines) {
			out.clear();
			const size_t firstDiagnostic = diagnostics.size();
			if (document.type != DOCUMENT_TYPE) {
				diagnostics.push_back({0, "the document is a '" + document.type + "', not a node shader"});
				return false;
			}
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			StackRead stack;
			ReadStack(document, resolver, stack, diagnostics);
			const Settings& settings = stack.settings;

			if (settings.target == Target::Fullscreen) {
				if (diagnostics.size() != firstDiagnostic) {
					return false;
				}
				return EmitFullscreen(document, sourceName, settings, stack.properties, stack.roots, stack.rooted, resolver, out, diagnostics, lines);
			}
			if (settings.target == Target::Sky) {
				if (diagnostics.size() != firstDiagnostic) {
					return false;
				}
				return EmitSky(sourceName, stack.properties, document, stack.roots, stack.rooted, resolver, out, diagnostics, lines);
			}

			GraphSurfaceFunctions functions;
			if (!Functions(document, stack, resolver, functions, diagnostics, firstDiagnostic)) {
				return false;
			}

			str text;
			text += "// generated by Node Shader from " + str(sourceName) + ". Do not edit manually\n";
			text += settings.target == Target::Unlit ? "#pragma shalltear_template unlit\n" : "#pragma shalltear_template lit\n";
			if (settings.target == Target::CustomLighting) {
				text += "#define SHALLTEAR_CUSTOM_LIGHTING\n";
			}
			text += "#pragma shalltear_blend " + str(RHI::BlendModeName(settings.blend)) + "\n";
			text += "#pragma shalltear_depth " + str(RHI::DepthModeName(settings.depth)) + "\n";
			text += "#pragma shalltear_cull " + str(RHI::CullModeName(settings.cull)) + "\n";
			if (functions.vertexFunction) {
				text += "#define SHALLTEAR_VERTEX_FUNCTION\n";
				for (u32 mode = 0; mode < 3; ++mode) {
					if (functions.interpolators[mode] != 0) {
						text += "#define " + str(INTERPOLATOR_COUNTS[mode]) + " " + std::to_string(functions.interpolators[mode]) + "\n";
					}
				}
			}

			text += PropertyPragmas(stack.properties);

			// declared even when unread (a keyword never changes the interface)
			text += GraphProperties::Declarations(stack.properties);

			const size_t head = LineOf(text);
			text += functions.text;
			if (lines) {
				lines->assign(head, 0u);
				lines->insert(lines->end(), functions.lines.begin(), functions.lines.end());
			}
			out = std::move(text);
			return true;
		}

		bool GenerateFunctions(const GraphDocument& document, GraphSurfaceFunctions& out, Vector<GraphDiagnostic>& diagnostics,
							   GraphDefinitions* definitions) {
			const size_t firstDiagnostic = diagnostics.size();
			if (document.type != DOCUMENT_TYPE) {
				diagnostics.push_back({0, "the document is a '" + document.type + "', not a node shader"});
				return false;
			}
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			StackRead stack;
			ReadStack(document, resolver, stack, diagnostics);
			if (stack.settings.target == Target::Fullscreen) {
				diagnostics.push_back({0, "the graph is a Fullscreen graph (it shades a pass row, has no surface to shade anything else with)"});
			}
			if (stack.settings.target == Target::Sky) {
				diagnostics.push_back({0, "the graph is a Sky graph (it shades the sky, has no surface to shade anything else with)"});
			}
			if (diagnostics.size() != firstDiagnostic) {
				return false;
			}
			return Functions(document, stack, resolver, out, diagnostics, firstDiagnostic);
		}

		bool SupportsEffects(const GraphDocument& document) {
			const str target = SettingText(document, "target");
			return document.type == DOCUMENT_TYPE && target != "fullscreen" && target != "sky" && SettingToggle(document, SUPPORT_EFFECTS);
		}

		bool CanShadeEffects(const GraphDocument& document, std::string_view graph, str& why) {
			if (SupportsEffects(document)) {
				return true;
			}
			if (IsFullscreen(document)) {
				why = "shades with '" + str(graph) + "' (a Fullscreen graph shades a pass row, not a particle)";
			} else if (IsSky(document)) {
				why = "shades with '" + str(graph) + "' (a Sky graph shades the sky, not a particle)";
			} else {
				why = "shades with node shader '" + str(graph) + "' (its Support Effects is off). Turn it on in its Graph Settings";
			}
			return false;
		}

		bool GenerateSubgraphPreview(const GraphDocument& document, std::string_view sourceName, str& out, Vector<GraphDiagnostic>& diagnostics,
									 GraphDefinitions* definitions, const Guid& asset, Vector<u32>* lines) {
			out.clear();
			const size_t firstDiagnostic = diagnostics.size();
			if (document.type != GRAPH_SUBGRAPH_DOCUMENT_TYPE) {
				diagnostics.push_back({0, "the document is a '" + document.type + "', not a subgraph"});
				return false;
			}
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
			GraphProperties::ParseBlackboard(document, properties, diagnostics);
			GraphSubgraphs::CollectKeywords(document, resolver, properties, diagnostics);

			const GraphContext* output = nullptr;
			for (const GraphContext& context : document.contexts) {
				output = context.node.def == GRAPH_SUBGRAPH_OUTPUT_CONTEXT ? &context : output;
			}
			if (output == nullptr || output->blocks.empty()) {
				diagnostics.push_back({0, "the subgraph has no outputs (needs at least 1)"});
				return false;
			}
			if (diagnostics.size() != firstDiagnostic) {
				return false;
			}
			GraphCompileRoot root;
			root.node = output->blocks.front().id;
			root.port = "Value";
			root.input = true;
			GraphCompileRequest request;
			request.roots = {&root, 1};
			request.bindings = SURFACE_BINDINGS;
			request.definitions = &resolver;
			request.asset = asset;
			GraphProgram program;
			if (!GraphCompiler::Compile(document, request, program, diagnostics)) {
				return false;
			}
			GraphHLSLText fragment;
			GraphHLSL::Emit(program, GraphStageFragment, fragment);
			const str& value = fragment.roots[0];
			str color;
			switch (program.At(program.roots[0].value).type) {
			case GraphValueType::Bool:
				color = "(float3)(float)(" + value + ")";
				break;
			case GraphValueType::Float:
				color = "(float3)(" + value + ")";
				break;
			case GraphValueType::Float2:
				color = "float3((" + value + "), 0.0)";
				break;
			case GraphValueType::Float3:
				color = value;
				break;
			case GraphValueType::Float4:
				color = "(" + value + ").xyz";
				break;
			default:
				diagnostics.push_back({root.node, "is the 1st output and a " + str(GraphTypes::DisplayName(program.At(program.roots[0].value).type)) +
													  " (the main preview can't show it as a color)"});
				return false;
			}

			str text;
			text += "// generated by Node Shader from " + str(sourceName) + "'s main preview. Do not edit manually\n";
			text += "#pragma shalltear_template unlit\n";
			text += "#pragma shalltear_blend opaque\n";
			text += "#pragma shalltear_depth test_write\n";
			text += "#pragma shalltear_cull back\n";
			text += PropertyPragmas(properties);
			text += GraphProperties::Declarations(properties);
			Vector<LineSegment> segments{GetMemoryResource(MemTag::Editor)};
			segments.push_back({LineOf(text), &fragment.functionLineNodes});
			text += fragment.functions;
			text += FRAME;
			text += "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n";
			segments.push_back({LineOf(text), &fragment.lineNodes});
			text += fragment.body;
			text += "\tsurface.baseColor = " + color + ";\n";
			text += "\tsurface.alpha = 1.0;\n";
			text += "}\n";
			LayLines(text, segments, lines);
			out = std::move(text);
			return true;
		}

		std::span<const GraphBinding> Bindings() { return SURFACE_BINDINGS; }

		std::span<const GraphBinding> LightingBindings() { return LIGHTING_BINDINGS; }

		std::span<const GraphBinding> FullscreenBindings() { return FULLSCREEN_BINDINGS; }

		std::span<const GraphBinding> SkyBindings() { return SKY_BINDINGS; }

		bool IsFullscreen(const GraphDocument& document) { return SettingText(document, "target") == "fullscreen"; }

		bool IsSky(const GraphDocument& document) { return SettingText(document, "target") == "sky"; }

		bool HasVertexStage(const GraphDocument& document) { return !IsFullscreen(document) && !IsSky(document); }

		str PassDeclarations(const GraphProgram& program) {
			std::array<bool, std::size(FULLSCREEN_RESOURCES)> needed{};
			NeededResources(program, needed);
			str text;
			for (size_t i = 0; i < std::size(FULLSCREEN_RESOURCES); ++i) {
				text += needed[i] ? str(FULLSCREEN_RESOURCES[i].text) : str();
			}
			return text;
		}

		bool ReadsClock(std::string_view generated) {
			for (const std::span<const GraphBinding> table : {Bindings(), FullscreenBindings(), SkyBindings()}) {
				for (const GraphBinding& binding : table) {
					if (binding.name == "TimeParameters" && generated.find(binding.expression) != std::string_view::npos) {
						return true;
					}
				}
			}
			return false;
		}

		bool IsBlockActive(const GraphDocument& document, std::string_view def) {
			Settings settings;
			Vector<GraphDiagnostic> ignored{GetMemoryResource(MemTag::Editor)};
			ReadSettings(document, settings, ignored);
			for (const Block& block : FRAGMENT_BLOCKS) {
				if (block.id == def) {
					return Active(block, settings);
				}
			}
			if (def == RADIANCE_BLOCK) {
				return settings.target == Target::CustomLighting;
			}
			return settings.target != Target::Fullscreen && settings.target != Target::Sky &&
				   (def == INTERPOLATOR_BLOCK ||
					std::any_of(std::begin(VERTEX_BLOCKS), std::end(VERTEX_BLOCKS), [&](const VertexBlock& block) { return block.id == def; }));
		}

		std::span<const std::string_view> VertexBlocks() {
			static const auto ids = [] {
				std::array<std::string_view, std::size(VERTEX_BLOCKS)> out{};
				for (size_t i = 0; i < out.size(); ++i) {
					out[i] = VERTEX_BLOCKS[i].id;
				}
				return out;
			}();
			return ids;
		}

		std::span<const std::string_view> FragmentBlocks() {
			static const auto ids = [] {
				std::array<std::string_view, std::size(FRAGMENT_BLOCKS)> out{};
				for (size_t i = 0; i < out.size(); ++i) {
					out[i] = FRAGMENT_BLOCKS[i].id;
				}
				return out;
			}();
			return ids;
		}

		bool IsMaterialShaderSource(std::string_view source) {
			GraphDocument document;
			str error;
			if (!GraphDocumentOps::Read(source, document, error) || document.type != DOCUMENT_TYPE) {
				return false;
			}
			str target = SettingText(document, "target");
			return target.empty() || target == "lit" || target == "unlit" || target == "customLighting" || target == "fullscreen" || target == "sky";
		}

	} // namespace GraphSurface

} // namespace Shalltear
