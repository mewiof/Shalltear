#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditTarget.h"

#include <functional>

namespace Shalltear {

	class Component;
	class Asset;

	using PropertyDrawerFn = std::function<void(UI::Context& context, UI::ElementHandle row,
												FieldRef field, EditTarget target)>;

	using PropertyDrawerRefreshFn = std::function<void(UI::Context& context)>;

	using ComponentInspectorFn = std::function<void(UI::Context& context, UI::ElementHandle parent,
													Handle<Component> component)>;

	using AssetInspectorFn = std::function<void(UI::Context& context, UI::ElementHandle parent,
												const Guid& guid, Asset& asset)>;

	using AssetInspectorSectionAppliesFn = std::function<bool(const Guid& guid, Asset& asset)>;
	using AssetInspectorSectionFn = std::function<void(UI::Context& context, UI::ElementHandle parent,
													   const Guid& guid, Asset& asset)>;

	struct AssetInspectorSectionDesc {
		std::string_view label;
		i32 order = 0;
		AssetInspectorSectionAppliesFn applies;
		AssetInspectorSectionFn draw;
	};

	struct AssetInspectorSection {
		str label;
		i32 order = 0;
		AssetInspectorSectionAppliesFn applies;
		AssetInspectorSectionFn draw;
	};

	class SHALLTEAR_EDITOR_API PropertyDrawers {
	public:
		static void Initialize();
		static void Terminate();

		static bool RegisterFieldDrawer(StringHash fieldTypeName, PropertyDrawerFn drawer, const void* owner = nullptr);
		static bool RegisterComponentInspector(StringHash componentTypeName, ComponentInspectorFn inspector, const void* owner = nullptr);
		static bool RegisterAssetInspector(StringHash assetTypeName, AssetInspectorFn inspector, const void* owner = nullptr);

		static bool RegisterFieldDrawerFor(StringHash declaringTypeName, StringHash fieldName,
										   PropertyDrawerFn drawer, const void* owner = nullptr);

		static const PropertyDrawerFn* FindFieldDrawer(StringHash fieldTypeName);
		static const PropertyDrawerFn* FindFieldDrawerFor(StringHash declaringTypeName, StringHash fieldName);
		static const ComponentInspectorFn* FindComponentInspector(StringHash componentTypeName);
		static const AssetInspectorFn* FindAssetInspector(StringHash assetTypeName);

		static bool RegisterAssetInspectorSection(StringHash assetTypeName, AssetInspectorSectionDesc desc,
												  const void* owner = nullptr);

		static void GetAssetInspectorSections(StringHash assetTypeName, Vector<AssetInspectorSection>& out);

		static std::function<void()> GetInspectorRebuilder();

		static bool SetRowRefresh(UI::Context& context, UI::ElementHandle row,
								  PropertyDrawerRefreshFn refresh, const void* owner);

		static void RefreshRows(UI::Context& context);

		static bool DropRow(UI::Context& context, UI::ElementHandle row);

		static u32 UnregisterOwnedBy(const void* owner);
	};

} // namespace Shalltear
