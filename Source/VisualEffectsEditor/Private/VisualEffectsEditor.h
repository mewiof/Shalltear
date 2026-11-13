#pragma once

#include <string_view>

namespace Shalltear::VisualEffectsEditor {

	inline constexpr std::string_view EffectsCategory = "Effect Graphs";

	void RegisterImporter(const void* owner);
	void RegisterVectorFieldImporter(const void* owner);

	void RegisterEditorExtensions(const void* owner);
	void RegisterComponentIcons(const void* owner);
	void ShutdownEditor();

} // namespace Shalltear::VisualEffectsEditor
