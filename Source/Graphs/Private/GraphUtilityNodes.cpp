#include "GraphNodeLibrary.h"

#include "Shalltear/Editor/GraphDocument.h"

namespace Shalltear {

	namespace {

		constexpr GraphPortDefinition REDIRECT_INPUTS[] = {{GraphDocumentOps::REDIRECT_IN, GraphPortType::Any}};
		constexpr GraphPortDefinition REDIRECT_OUTPUTS[] = {{GraphDocumentOps::REDIRECT_OUT, GraphPortType::Any}};

		void RedirectBody(GraphBodyContext& c) { c.outputs[0] = c.In(0); }

		constexpr GraphSettingDefinition KEYWORD_SETTINGS[] = {{"property", GraphSettingKind::Property}};
		constexpr GraphPortDefinition KEYWORD_INPUTS[] = {{"On", GraphPortType::DynamicVector}, {"Off", GraphPortType::DynamicVector}};
		constexpr GraphPortDefinition KEYWORD_OUTPUTS[] = {{"Out", GraphPortType::DynamicVector}};

		void KeywordBody(GraphBodyContext& c) {
			if (c.settings[0].type != GraphValueType::None) {
				c.error = "reads '" + str(c.settings[0].text) + "' (a property, not a keyword)";
				return;
			}
			const std::span<const GraphPortDefinition> entries = c.definition->inputs;
			if (entries.empty() || entries[0].keyword.empty()) {
				c.outputs[0] = c.builder.Keyword(c.settings[0].text, c.In(0), c.In(1));
				return;
			}
			GraphValueId chain = c.In(static_cast<u32>(entries.size() - 1));
			for (size_t i = entries.size() - 1; i-- > 0;) {
				chain = c.builder.Keyword(entries[i].keyword, c.In(static_cast<u32>(i)), chain);
			}
			c.outputs[0] = chain;
		}

		using P = GraphPortType;
		using M = GraphPreviewMode;

		constexpr GraphPortDefinition BOOL_PAIR[] = {{"A", P::Boolean}, {"B", P::Boolean}};
		constexpr GraphPortDefinition BOOL_IN[] = {{"In", P::Boolean}};
		constexpr GraphPortDefinition BOOL_OUT[] = {{"Out", P::Boolean}};
		constexpr GraphPortDefinition FLOAT_IN[] = {{"In", P::Float}};
		constexpr GraphPortDefinition FLOAT_PAIR[] = {{"A", P::Float}, {"B", P::Float}};
		constexpr GraphPortDefinition VECTOR_IN[] = {{"In", P::DynamicVector}};
		constexpr GraphPortDefinition VECTOR_OUT[] = {{"Out", P::DynamicVector}};
		constexpr GraphPortDefinition BRANCH_INPUTS[] = {
			{"Predicate", P::Boolean}, {"True", P::DynamicVector, {1.0f, 1.0f, 1.0f, 1.0f}}, {"False", P::DynamicVector}};

		constexpr std::string_view COMPARISON_TYPES[] = {"Equal", "NotEqual", "Less", "LessOrEqual", "Greater", "GreaterOrEqual"};
		constexpr GraphSettingDefinition COMPARISON_SETTINGS[] = {{"Comparison Type", GraphSettingKind::Choice, COMPARISON_TYPES}};

		void AndBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Binary(GraphOp::LogicalAnd, c.In(0), c.In(1)); }
		void OrBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Binary(GraphOp::LogicalOr, c.In(0), c.In(1)); }
		void NotBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Unary(GraphOp::LogicalNot, c.In(0)); }
		void NandBody(GraphBodyContext& c) {
			c.outputs[0] = c.builder.Unary(GraphOp::LogicalNot, c.builder.Binary(GraphOp::LogicalAnd, c.In(0), c.In(1)));
		}
		void AllBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Unary(GraphOp::All, c.In(0)); }
		void AnyBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Unary(GraphOp::Any, c.In(0)); }
		void IsNaNBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Unary(GraphOp::IsNaN, c.In(0)); }
		void IsInfiniteBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Unary(GraphOp::IsInfinite, c.In(0)); }
		void IsFrontFaceBody(GraphBodyContext& c) { c.outputs[0] = c.Binding("IsFrontFace"); }

		void BranchBody(GraphBodyContext& c) { c.outputs[0] = c.builder.Select(c.In(0), c.In(1), c.In(2)); }

		constexpr GraphPortDefinition CONNECTION_INPUTS[] = {
			{"Input", P::ConnectionState}, {"Connected", P::DynamicVector, {1.0f, 1.0f, 1.0f, 1.0f}}, {"NotConnected", P::DynamicVector}};

		void ComparisonBody(GraphBodyContext& c) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId x = c.In(0);
			const GraphValueId y = c.In(1);
			switch (c.Setting(0)) {
			case 1:
				c.outputs[0] = b.Unary(GraphOp::LogicalNot, b.Binary(GraphOp::Equal, x, y));
				break;
			case 2:
				c.outputs[0] = b.Binary(GraphOp::Less, x, y);
				break;
			case 3:
				c.outputs[0] = b.Binary(GraphOp::LessEqual, x, y);
				break;
			case 4:
				c.outputs[0] = b.Binary(GraphOp::Less, y, x);
				break;
			case 5:
				c.outputs[0] = b.Binary(GraphOp::LessEqual, y, x);
				break;
			default:
				c.outputs[0] = b.Binary(GraphOp::Equal, x, y);
				break;
			}
		}

		void PreviewBody(GraphBodyContext& c) { c.outputs[0] = c.In(0); }

		constexpr GraphNodeDefinition UTILITY_DEFINITIONS[] = {
			{GraphDocumentOps::REDIRECT_DEF, "Redirect", "Utility", "", REDIRECT_INPUTS, REDIRECT_OUTPUTS, {}, RedirectBody},
			{"Utility/Keyword", "Keyword", "Utility", "shader feature variant toggle", KEYWORD_INPUTS, KEYWORD_OUTPUTS, KEYWORD_SETTINGS, KeywordBody},
			{"Utility/Preview", "Preview", "Utility", "", VECTOR_IN, VECTOR_OUT, {}, PreviewBody},
			{"Utility/All", "All", "Utility/Logic", "", VECTOR_IN, BOOL_OUT, {}, AllBody, M::None},
			{"Utility/And", "And", "Utility/Logic", "", BOOL_PAIR, BOOL_OUT, {}, AndBody, M::None},
			{"Utility/Any", "Any", "Utility/Logic", "", VECTOR_IN, BOOL_OUT, {}, AnyBody, M::None},
			{"Utility/Branch", "Branch", "Utility/Logic", "switch if else", BRANCH_INPUTS, VECTOR_OUT, {}, BranchBody},
			{"Utility/BranchOnInputConnection", "Branch On Input Connection", "Utility/Logic", "", CONNECTION_INPUTS, VECTOR_OUT, {}, BranchBody, M::Inherit, true},
			{"Utility/Comparison", "Comparison", "Utility/Logic", "equal greater than less than", FLOAT_PAIR, BOOL_OUT, COMPARISON_SETTINGS, ComparisonBody,
			 M::None},
			{"Utility/IsFrontFace", "Is Front Face", "Utility/Logic", "face side", {}, BOOL_OUT, {}, IsFrontFaceBody, M::None},
			{"Utility/IsInfinite", "Is Infinite", "Utility/Logic", "", FLOAT_IN, BOOL_OUT, {}, IsInfiniteBody, M::None},
			{"Utility/IsNaN", "Is NaN", "Utility/Logic", "", FLOAT_IN, BOOL_OUT, {}, IsNaNBody, M::None},
			{"Utility/Nand", "Nand", "Utility/Logic", "", BOOL_PAIR, BOOL_OUT, {}, NandBody, M::None},
			{"Utility/Not", "Not", "Utility/Logic", "", BOOL_IN, BOOL_OUT, {}, NotBody, M::None},
			{"Utility/Or", "Or", "Utility/Logic", "", BOOL_PAIR, BOOL_OUT, {}, OrBody, M::None},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphUtilityDefinitions() { return UTILITY_DEFINITIONS; }

} // namespace Shalltear
