#pragma once

#include "Shalltear/Animation/AnimatorController.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	namespace AnimatorControllerText {

		struct Arrays {
			Vector<AnimatorController::Parameter> parameters{GetMemoryResource(MemTag::Asset)};
			Vector<AnimatorController::MotionNode> nodes{GetMemoryResource(MemTag::Asset)};
			Vector<AnimatorController::State> states{GetMemoryResource(MemTag::Asset)};
			Vector<AnimatorController::StateMachine> machines{GetMemoryResource(MemTag::Asset)};
			Vector<AnimatorController::Layer> layers{GetMemoryResource(MemTag::Asset)};
		};

		enum class Shape : u8 {
			File,
			Snapshot,
			Structure
		};

		str Write(const AnimatorController& controller, Shape shape);

		bool Read(std::string_view text, std::string_view source, Arrays& out);

	} // namespace AnimatorControllerText

} // namespace Shalltear
