#include "Shalltear/Editor/PanelManager.h"

#include "Shalltear/Editor/AuthoringDefaults.h"
#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"

#include "AssetBrowserPanel.h"
#include "BuildProfilesPanel.h"
#include "BuildReportPanel.h"
#include "ConsolePanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "PackageManagerPanel.h"
#include "ProfilerPanel.h"
#include "RuntimeViewPanel.h"
#include "SceneViewPanel.h"
#include "SettingsPanel.h"
#include "UIBuilderPanel.h"
#include "UIDebuggerPanel.h"

namespace Shalltear {

	void RegisterBuiltInPanels() {
		AssetBrowserPanel::GetReflectType();
		BuildProfilesPanel::GetReflectType();
		BuildReportPanel::GetReflectType();
		ConsolePanel::GetReflectType();
		HierarchyPanel::GetReflectType();
		InspectorPanel::GetReflectType();
		PackageManagerPanel::GetReflectType();
		ProfilerPanel::GetReflectType();
		RuntimeViewPanel::GetReflectType();
		SceneViewPanel::GetReflectType();
		SettingsPanel::GetReflectType();
		UIBuilderPanel::GetReflectType();
		UIDebuggerPanel::GetReflectType();
		RegisterBuiltInComponentInspectors();
		RegisterBuiltInAssetInspectors();
		RegisterPrefabAssetInspector();
		EditorPreviews::RegisterBuiltIns();
		EditorIcons::ComponentIcons::RegisterBuiltIns();
		EditorPreviews::AssetBadges::RegisterBuiltIns();
		ComponentMenu::RegisterBuiltIns();
		AuthoringDefaults::RegisterBuiltIns();
	}

} // namespace Shalltear
