#include "PostProcessingEditor.h"

#include "Shalltear/Core/Reflection/TypeInfo.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"

#include "Shalltear/PostProcessing/PostProcessVolume.h"

namespace Shalltear::PostProcessingEditor {

	namespace {

		constexpr std::string_view VolumeMark = "engine://Icons/Components/PostProcessVolume.svg";

		constexpr std::string_view Category = "Rendering";

	} // anonymous namespace

	void RegisterComponentPresentation(const void* owner) {
		EditorIcons::ComponentIcons::Register(PostProcessVolume::GetReflectType()->name, VolumeMark, owner);
		ComponentMenu::Register(PostProcessVolume::GetReflectType()->name, Category, owner);
	}

} // namespace Shalltear::PostProcessingEditor
