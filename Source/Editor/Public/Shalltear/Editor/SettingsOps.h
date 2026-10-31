#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	struct FieldInfo;

	namespace SettingsOps {

		struct TreeNode {
			str path;
			str label;
			str tooltip;
			u32 depth = 0;
			bool declared = false;
		};

		SHALLTEAR_EDITOR_API void BuildTree(std::string_view search, Vector<TreeNode>& out);

		SHALLTEAR_EDITOR_API bool CategoryMatches(std::string_view search, std::string_view categoryPath);

		SHALLTEAR_EDITOR_API std::string_view ScopeName(SettingScope scope);
		SHALLTEAR_EDITOR_API std::string_view ScopeDescription(SettingScope scope);
		SHALLTEAR_EDITOR_API std::string_view SourceBadge(const SettingFieldState& state);
		SHALLTEAR_EDITOR_API std::string_view SourceBadgeDescription(const SettingFieldState& state);

		SHALLTEAR_EDITOR_API std::string_view ScopePrecedenceHint();

		SHALLTEAR_EDITOR_API std::string_view AudienceLine(std::string_view categoryPath);

		SHALLTEAR_EDITOR_API bool AnyFieldTakesEffectOnRestart(const SettingsCategoryInfo& category);
		SHALLTEAR_EDITOR_API std::string_view RestartLine(std::string_view categoryPath);

		SHALLTEAR_EDITOR_API bool IsFieldShown(const SettingsCategoryInfo& category, const FieldInfo& field);

		SHALLTEAR_EDITOR_API std::string_view MutabilityBadge(MutabilityClass mutability);
		SHALLTEAR_EDITOR_API std::string_view MutabilityLine(MutabilityClass mutability);
		SHALLTEAR_EDITOR_API std::string_view FieldMutabilityBadge(const FieldInfo& field);
		SHALLTEAR_EDITOR_API std::string_view FieldMutabilityLine(const FieldInfo& field);

		SHALLTEAR_EDITOR_API str FieldLabelFor(std::string_view categoryPath, const FieldInfo& field);
		SHALLTEAR_EDITOR_API str FieldTooltipFor(std::string_view categoryPath, const FieldInfo& field);

		SHALLTEAR_EDITOR_API u32 MigrateLegacyEngineIni(const Path& legacyFile, const Path& projectDocument);

		SHALLTEAR_EDITOR_API u32 ListProjectQualityProfiles();

		SHALLTEAR_EDITOR_API u32 CollectSettingsAssetRoots(Vector<Guid>& out);

	} // namespace SettingsOps

} // namespace Shalltear
