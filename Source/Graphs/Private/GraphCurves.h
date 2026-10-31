#pragma once

#include "GraphsAPI.h"

#include "GraphProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class JSONValue;

	namespace GraphCurves {

		inline constexpr std::string_view DEFAULT_JSON = R"({"keys":[{"time":0,"value":0,"outTangent":1},{"time":1,"value":1,"inTangent":1}]})";

		SHALLTEAR_GRAPHS_API bool Parse(std::string_view json, AnimationCurve& out, str& error);
		SHALLTEAR_GRAPHS_API str Write(const AnimationCurve& curve);

		SHALLTEAR_GRAPHS_API bool Pack(const AnimationCurve& curve, std::span<f32> out, str& error);

		SHALLTEAR_GRAPHS_API GraphValueId Sample(GraphProgramBuilder& builder, std::span<const f32> curve, GraphValueId time);

	} // namespace GraphCurves

	namespace GraphGradients {

		SHALLTEAR_GRAPHS_API void Default(std::span<f32> out);

		SHALLTEAR_GRAPHS_API bool Read(const JSONValue& json, std::span<f32> out, str& error);
		SHALLTEAR_GRAPHS_API bool Read(std::string_view json, std::span<f32> out, str& error);

		SHALLTEAR_GRAPHS_API GraphValueId Sample(GraphProgramBuilder& builder, std::span<const f32> gradient, GraphValueId time);
		SHALLTEAR_GRAPHS_API void Evaluate(std::span<const f32> gradient, f32 time, f32 out[4]);

	} // namespace GraphGradients

} // namespace Shalltear
