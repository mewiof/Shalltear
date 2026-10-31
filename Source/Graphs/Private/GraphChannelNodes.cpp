#include "GraphNodeLibrary.h"

#include <algorithm>
#include <array>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using B = GraphProgramBuilder;

		constexpr GraphPortDefinition COMBINE_INPUTS[] = {{"R", P::Float}, {"G", P::Float}, {"B", P::Float}, {"A", P::Float}};
		constexpr GraphPortDefinition COMBINE_OUTPUTS[] = {{"RGBA", P::Float4}, {"RGB", P::Float3}, {"RG", P::Float2}};
		constexpr GraphPortDefinition VECTOR_IN[] = {{"In", P::DynamicVector}};
		constexpr GraphPortDefinition VECTOR_OUT[] = {{"Out", P::DynamicVector}};
		constexpr GraphPortDefinition SPLIT_OUTPUTS[] = {{"R", P::Float}, {"G", P::Float}, {"B", P::Float}, {"A", P::Float}};
		constexpr GraphSettingDefinition FLIP_SETTINGS[] = {
			{"Red", GraphSettingKind::Toggle}, {"Green", GraphSettingKind::Toggle}, {"Blue", GraphSettingKind::Toggle}, {"Alpha", GraphSettingKind::Toggle}};
		constexpr GraphSettingDefinition SWIZZLE_SETTINGS[] = {{"Mask", GraphSettingKind::Text, {}, 0, "xxxx"}};

		void CombineBody(GraphBodyContext& c) {
			B& b = c.builder;
			const GraphValueId all[] = {c.In(0), c.In(1), c.In(2), c.In(3)};
			c.outputs[0] = b.Construct(GraphValueType::Float4, {all, 4});
			c.outputs[1] = b.Construct(GraphValueType::Float3, {all, 3});
			c.outputs[2] = b.Construct(GraphValueType::Float2, {all, 2});
		}

		void FlipBody(GraphBodyContext& c) {
			B& b = c.builder;
			u32 width = GraphTypes::VectorWidth(c.TypeOf(0));
			std::array<f32, 4> flags{};
			for (u32 i = 0; i < width; ++i) {
				flags[i] = c.Setting(i) != 0 ? 1.0f : 0.0f;
			}
			GraphValueId flip = b.Constant(c.TypeOf(0), {flags.data(), width});
			GraphValueId sign = b.Add(b.Multiply(flip, b.Scalar(-2.0f)), b.Scalar(1.0f));
			c.outputs[0] = b.Multiply(sign, c.In(0));
		}

		void SplitBody(GraphBodyContext& c) {
			B& b = c.builder;
			constexpr std::string_view CHANNELS[] = {"x", "y", "z", "w"};
			u32 width = GraphTypes::VectorWidth(c.TypeOf(0));
			for (u32 i = 0; i < 4; ++i) {
				if (i >= width) {
					c.outputs[i] = b.Scalar(0.0f);
				} else {
					c.outputs[i] = width == 1 ? c.In(0) : b.Swizzle(c.In(0), CHANNELS[i]);
				}
			}
		}

		void SwizzleBody(GraphBodyContext& c) {
			B& b = c.builder;
			std::string_view mask = c.Text(0);
			str converted;
			u32 furthest = 0;
			bool valid = !mask.empty() && mask.size() <= 4;
			for (char letter : mask) {
				char lower = letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
				constexpr std::string_view XYZW = "xyzw";
				constexpr std::string_view RGBA = "rgba";
				size_t index = XYZW.find(lower);
				if (index == std::string_view::npos) {
					index = RGBA.find(lower);
				}
				if (index == std::string_view::npos) {
					valid = false;
					break;
				}
				furthest = std::max(furthest, static_cast<u32>(index) + 1);
				converted += XYZW[index];
			}
			u32 inputSize = c.IsConnected(0) ? GraphTypes::VectorWidth(c.TypeOf(0)) : furthest;
			if (!valid || furthest > inputSize) {
				c.error = "Invalid mask for a 'Vector" + std::to_string(inputSize) + "' input.";
				return;
			}
			c.outputs[0] = b.Swizzle(b.Convert(c.In(0), GraphTypes::Vector(furthest)), converted);
		}

		constexpr GraphNodeDefinition CHANNEL_DEFINITIONS[] = {
			{"Channel/Combine", "Combine", "Channel", "append", COMBINE_INPUTS, COMBINE_OUTPUTS, {}, CombineBody},
			{"Channel/Flip", "Flip", "Channel", "", VECTOR_IN, VECTOR_OUT, FLIP_SETTINGS, FlipBody},
			{"Channel/Split", "Split", "Channel", "separate", VECTOR_IN, SPLIT_OUTPUTS, {}, SplitBody, GraphPreviewMode::None},
			{"Channel/Swizzle", "Swizzle", "Channel", "swap reorder component mask", VECTOR_IN, VECTOR_OUT, SWIZZLE_SETTINGS, SwizzleBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphChannelDefinitions() { return CHANNEL_DEFINITIONS; }

} // namespace Shalltear
