#include "Shalltear/Editor/EditorAssetWatch.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/ProjectLayout.h"

#include "Shalltear/Editor/AssetBrowserOps.h"

#include <algorithm>
#include <mutex>

namespace Shalltear {

	namespace {

		constexpr u32 PENDING_LIMIT = 2048;

		struct WatchState {
			std::mutex mutex;
			Vector<Path> pending{GetMemoryResource(MemTag::Editor)};
			bool overflowed = false;

			Vector<Path> thisFrame{GetMemoryResource(MemTag::Editor)};
			bool overflowedThisFrame = false;
			Vector<FileWatcher> watchers{GetMemoryResource(MemTag::Editor)};
			str armedRoot;
			u64 armedGeneration = ~0ull;
		};

		WatchState* _state = nullptr;

		bool IsSelfInflicted(const str& path) {
			return path.ends_with(".meta") || path.ends_with(".assets.db");
		}

		str ToVirtual(const str& armedRoot, const str& changed) {
			if (changed.find("://") != str::npos) {
				return changed;
			}
			if (armedRoot.empty()) {
				return {};
			}
			str normalized = changed;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			if (normalized.size() <= armedRoot.size()) {
				return {};
			}
			auto sameChar = [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			};
			if (!std::equal(armedRoot.begin(), armedRoot.end(), normalized.begin(), sameChar)) {
				return {};
			}
			size_t at = armedRoot.size();
			if (normalized[at] == '/') {
				++at;
			}
			return "project://" + normalized.substr(at);
		}

		void RecordChange(const str& armedRoot, const Path& changed) {
			if (IsSelfInflicted(changed.ToString())) {
				return;
			}
			str virtualPath = ToVirtual(armedRoot, changed.ToString());
			if (virtualPath.empty()) {
				return;
			}
			std::lock_guard lock(_state->mutex);
			if (_state->pending.size() >= PENDING_LIMIT) {
				_state->overflowed = true;
				_state->pending.clear();
				return;
			}
			_state->pending.emplace_back(std::move(virtualPath));
		}

		void Disarm(bool discardPending) {
			_state->watchers.clear();
			_state->armedRoot.clear();
			if (!discardPending) {
				return;
			}
			std::lock_guard lock(_state->mutex);
			_state->pending.clear();
			_state->overflowed = false;
		}

		void ArmFor(const str& root) {
			_state->watchers.reserve(ProjectLayout::GetContentRoots().size());
			for (std::string_view contentRoot : ProjectLayout::GetContentRoots()) {
				Path start{root + "/" + str(contentRoot)};
				if (!FileSystem::IsDirectory(start)) {
					continue;
				}
				_state->watchers.emplace_back(start, [root](const Path& changed, FileAction) {
					// worker thread, only the pending list under its lock
					if (_state) {
						RecordChange(root, changed);
					}
				});
			}
			_state->armedRoot = root;
		}

	} // anonymous namespace

	void EditorAssetWatch::Initialize() {
		if (!_state) {
			_state = New<WatchState>(MemTag::Editor);
		}
	}

	void EditorAssetWatch::Terminate() {
		if (!_state) {
			return;
		}
		// watchers first (each destructor joins a worker using _state)
		_state->watchers.clear();
		Delete(_state);
		_state = nullptr;
	}

	void EditorAssetWatch::PollChanges() {
		if (!_state) {
			return;
		}
		_state->thisFrame.clear();
		_state->overflowedThisFrame = false;

		u64 generation = FileSystem::GetMountGeneration();
		if (generation != _state->armedGeneration) {
			_state->armedGeneration = generation;
			str root = AssetBrowserOps::IsProjectMounted() ? AssetBrowserOps::GetProjectRootPhysical().ToString() : str();
			if (root != _state->armedRoot) {
				Disarm(!_state->armedRoot.empty());
				if (!root.empty()) {
					ArmFor(root);
				}
			}
		}

		{
			std::lock_guard lock(_state->mutex);
			_state->overflowedThisFrame = _state->overflowed;
			_state->thisFrame.swap(_state->pending);
			_state->pending.clear();
			_state->overflowed = false;
		}
		if (_state->overflowedThisFrame) {
			_state->thisFrame.clear();
			return;
		}

		std::sort(_state->thisFrame.begin(), _state->thisFrame.end(),
				  [](const Path& a, const Path& b) { return a.ToString() < b.ToString(); });
		_state->thisFrame.erase(std::unique(_state->thisFrame.begin(), _state->thisFrame.end(),
											[](const Path& a, const Path& b) { return a.ToString() == b.ToString(); }),
								_state->thisFrame.end());
	}

	std::span<const Path> EditorAssetWatch::GetChangedThisFrame() {
		if (!_state) {
			return {};
		}
		return {_state->thisFrame.data(), _state->thisFrame.size()};
	}

	bool EditorAssetWatch::DidOverflowThisFrame() {
		return _state && _state->overflowedThisFrame;
	}

	void EditorAssetWatch::NotifyChanged(const Path& path) {
		if (!_state) {
			return;
		}
		RecordChange(_state->armedRoot, path);
	}

} // namespace Shalltear
