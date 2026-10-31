#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	class ShaderAsset;

	namespace EditorVariantCompiles {

		SHALLTEAR_EDITOR_API void Request(ShaderAsset& shader, std::span<const str> enabled);

		SHALLTEAR_EDITOR_API void Drain();

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace EditorVariantCompiles

} // namespace Shalltear
