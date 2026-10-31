#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include <functional>
#include <span>

namespace Shalltear {

	class Scene;

	namespace SceneFileFlows {

		UI::ElementHandle OpenConfirmPopup(UI::Context& context, str message,
										   str acceptLabel, std::function<void()> onAccept,
										   str altLabel, std::function<void()> onAlt,
										   str cancelLabel = "Cancel", std::function<void()> onCancel = {});

		UI::ElementHandle OpenDetailedConfirmPopup(UI::Context& context, str message, std::span<const str> details,
												   str acceptLabel, std::function<void()> onAccept,
												   str altLabel, std::function<void()> onAlt,
												   str cancelLabel = "Cancel", std::function<void()> onCancel = {});

		UI::ElementHandle OpenTextInputPopup(UI::Context& context, str title, str initialText,
											 std::function<void(const str&)> onAccept, str acceptLabel = "OK");

		void ConfirmUnsavedThen(UI::Context& context, std::function<void()> proceed);

		void NewScene(UI::Context& context);
		void OpenScene(UI::Context& context);
		void OpenScenePath(UI::Context& context, Path path);
		void AddScenePath(Path path);
		void OpenPrefabPath(UI::Context& context, Path path);
		void LeavePrefabStage(UI::Context& context, std::function<void()> then = {});
		void SaveScene(UI::Context& context, Handle<Scene> scene);
		void SaveSceneAs(UI::Context& context, Handle<Scene> scene);

	} // namespace SceneFileFlows

} // namespace Shalltear
