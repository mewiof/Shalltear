#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class TextureAsset;

	// matched by name to bindings from ShalltearGlobals.hlsli
	namespace ShaderGlobals {

		SHALLTEAR_CORE_API void SetFloat(std::string_view name, f32 value);
		SHALLTEAR_CORE_API void SetVector(std::string_view name, const Vec4& value);
		SHALLTEAR_CORE_API void SetColor(std::string_view name, const Color& value);
		SHALLTEAR_CORE_API void SetInt(std::string_view name, i32 value);
		// weak handle, texture must stay loaded
		SHALLTEAR_CORE_API void SetTexture(std::string_view name, Handle<TextureAsset> texture);
		SHALLTEAR_CORE_API void Clear(std::string_view name);
		SHALLTEAR_CORE_API void ClearAll();

	} // namespace ShaderGlobals

} // namespace Shalltear
