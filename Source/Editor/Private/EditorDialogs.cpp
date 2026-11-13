#include "Shalltear/Editor/EditorDialogs.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/Window.h"

#include "Platform/EditorPlatform.h"
#include "SceneFileFlows.h"

#include <memory>
#include <mutex>
#include <thread>

namespace Shalltear::EditorDialogs {

	namespace {

		struct PickResult {
			std::mutex mutex;
			bool done = false;
			bool opened = false;
			Vector<str> paths{GetMemoryResource(MemTag::Editor)};
			str error;
		};

		struct PendingPick {
			std::shared_ptr<PickResult> result;
			str title;
			std::function<void(std::span<const str>)> onPicked;
		};

		struct Picks {
			Vector<PendingPick> pending{GetMemoryResource(MemTag::Editor)};
			FileDialogFn dialog;
		};

		Picks& GetPicks() {
			static Picks picks;
			return picks;
		}

		bool IsDone(PickResult& result) {
			std::lock_guard lock(result.mutex);
			return result.done;
		}

		NativeWindowHandle WindowOf(const UI::Context& context) {
			NativeWindowHandle native;
			UISystem::ForEachContext([&context, &native](Handle<Window> window, UI::Context& candidate) {
				if (&candidate == &context && window.Get()) {
					native = window.Get()->GetNativeHandle();
				}
			});
			return native;
		}

	} // anonymous namespace

	UI::ElementHandle OpenConfirm(UI::Context& context, str message, str acceptLabel, std::function<void()> onAccept) {
		return SceneFileFlows::OpenConfirmPopup(context, std::move(message), std::move(acceptLabel), std::move(onAccept), "", {});
	}

	UI::ElementHandle OpenTextInput(UI::Context& context, str title, str initialText, str acceptLabel,
									std::function<void(const str&)> onAccept) {
		return SceneFileFlows::OpenTextInputPopup(context, std::move(title), std::move(initialText), std::move(onAccept), std::move(acceptLabel));
	}

	void PickFiles(UI::Context& context, FileDialogDesc desc, std::function<void(std::span<const str> paths)> onPicked) {
		if (desc.owner.system == NativeWindowSystem::None) {
			desc.owner = WindowOf(context);
		}
		Picks& picks = GetPicks();
		FileDialogFn dialog = picks.dialog ? picks.dialog : FileDialogFn(&EditorPlatform::RunOpenFileDialog);
		auto result = std::make_shared<PickResult>();
		std::thread([result, desc, dialog = std::move(dialog)]() {
			Vector<str> paths{GetMemoryResource(MemTag::Editor)};
			str error;
			const bool opened = dialog(desc, paths, error);
			std::lock_guard lock(result->mutex);
			result->opened = opened;
			result->paths = std::move(paths);
			result->error = std::move(error);
			result->done = true;
		}).detach();
		picks.pending.push_back({std::move(result), std::move(desc.title), std::move(onPicked)});
	}

	void Drain() {
		Vector<PendingPick>& pending = GetPicks().pending;
		for (size_t i = 0; i < pending.size();) {
			if (!IsDone(*pending[i].result)) {
				++i;
				continue;
			}
			PendingPick finished = std::move(pending[i]);
			pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
			const PickResult& result = *finished.result;
			if (!result.opened) {
				SLT_LOG_WARN("Editor", "{}: couldn't open file dialog ({})", finished.title, result.error);
			} else if (!result.paths.empty() && finished.onPicked) {
				finished.onPicked({result.paths.data(), result.paths.size()});
			}
		}
	}

	u32 GetPendingPickCount() {
		return static_cast<u32>(GetPicks().pending.size());
	}

	void SetFileDialog(FileDialogFn dialog) {
		GetPicks().dialog = std::move(dialog);
	}

} // namespace Shalltear::EditorDialogs
