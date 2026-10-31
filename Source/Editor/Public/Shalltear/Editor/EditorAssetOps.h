#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <span>

namespace Shalltear {

	class Asset;

	class SHALLTEAR_EDITOR_API EditorAssetOps {
	public:
		static void Initialize();
		static void Terminate();

		static void MarkDirty(Handle<Asset> asset);
		static bool AnyDirty();
		static bool IsDirty(Handle<Asset> asset);

		static void SaveAsset(Handle<Asset> asset);
		static void SaveAllDirty();
		static void DiscardAllDirty();

		using ReloadConflict = std::function<void(const Path& path, std::function<void()> reload)>;
		static u32 ReloadChanged(std::span<const Path> changed, const ReloadConflict& onConflict);
		static bool RevertToFile(Handle<Asset> asset);
	};

} // namespace Shalltear
