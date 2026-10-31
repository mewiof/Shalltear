#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	namespace EditorAssetWatch {

		SHALLTEAR_EDITOR_API void Initialize();
		SHALLTEAR_EDITOR_API void Terminate();

		SHALLTEAR_EDITOR_API void PollChanges();

		SHALLTEAR_EDITOR_API std::span<const Path> GetChangedThisFrame();

		SHALLTEAR_EDITOR_API bool DidOverflowThisFrame();

		SHALLTEAR_EDITOR_API void NotifyChanged(const Path& path);

	} // namespace EditorAssetWatch

} // namespace Shalltear
