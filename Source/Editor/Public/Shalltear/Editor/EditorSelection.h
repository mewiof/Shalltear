#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	class SHALLTEAR_EDITOR_API EditorSelection {
	public:
		static void Initialize();
		static void Terminate();

		static void Select(Handle<Object> object);
		static void AddToSelection(Handle<Object> object);
		static void RemoveFromSelection(Handle<Object> object);
		static void ToggleInSelection(Handle<Object> object);
		static void Clear();

		static std::span<const Handle<Object>> GetSelection();
		static Handle<Object> GetActive();
		static bool IsSelected(Handle<Object> object);
		static u32 GetSelectionCount();

		static void SelectAsset(const Guid& assetGuid);
		static void SelectAssets(std::span<const Guid> assetGuids);
		static std::span<const Guid> GetSelectedAssets();
		static const Guid& GetSelectedAsset();
		static bool HasAssetSelection();
		static u32 GetAssetSelectionCount();
	};

} // namespace Shalltear
