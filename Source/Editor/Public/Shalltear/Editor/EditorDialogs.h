#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/NativeWindow.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <span>

namespace Shalltear::EditorDialogs {

	SHALLTEAR_EDITOR_API UI::ElementHandle OpenConfirm(UI::Context& context, str message, str acceptLabel, std::function<void()> onAccept);

	SHALLTEAR_EDITOR_API UI::ElementHandle OpenTextInput(UI::Context& context, str title, str initialText, str acceptLabel,
														 std::function<void(const str&)> onAccept);

	struct FileDialogDesc {
		str title;
		str physicalFolder;
		bool multiple = false;
		NativeWindowHandle owner;
	};

	using FileDialogFn = std::function<bool(const FileDialogDesc& desc, Vector<str>& outPaths, str& outError)>;

	// OS dialog over context's window, onPicked called in Drain (never on cancel)
	SHALLTEAR_EDITOR_API void PickFiles(UI::Context& context, FileDialogDesc desc,
										std::function<void(std::span<const str> paths)> onPicked);

	SHALLTEAR_EDITOR_API void Drain();

	SHALLTEAR_EDITOR_API u32 GetPendingPickCount();

	// called on worker thread, may block until closed
	SHALLTEAR_EDITOR_API void SetFileDialog(FileDialogFn dialog);

} // namespace Shalltear::EditorDialogs
