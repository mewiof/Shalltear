#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	enum class SettingsAudience : u8 {
		Unspecified = 0,
		EditorOnly,
		EditorAndPlayer,
		PlayerOnly,
	};

	struct SettingsCategoryPresentation {
		str label;
		str tooltip;
		SettingsAudience audience = SettingsAudience::Unspecified;
		str keywords;
		i32 order = 0;
	};

	struct SettingsFieldCondition {
		str field;
		i64 value = 0;
		bool equals = true;
	};

	struct SettingsFieldPresentation {
		str label;
		str tooltip;
		str keywords;
		Vector<SettingsFieldCondition> shownWhen{GetMemoryResource(MemTag::Editor)};
	};

	class SHALLTEAR_EDITOR_API SettingsPresentation {
	public:
		static void Terminate();

		static void RegisterCategory(std::string_view categoryPath, SettingsCategoryPresentation presentation, const void* owner = nullptr);
		static void RegisterField(std::string_view categoryPath, std::string_view fieldName, SettingsFieldPresentation presentation, const void* owner = nullptr);

		static u32 UnregisterOwnedBy(const void* owner);

		static const SettingsCategoryPresentation* FindCategory(std::string_view categoryPath);
		static const SettingsFieldPresentation* FindField(std::string_view categoryPath, std::string_view fieldName);

		static void RegisterBuiltIns();
	};

} // namespace Shalltear
