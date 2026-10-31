#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	class Object;
	class Scene;

	struct SceneRowKey {
		u64 localId = 0;
		u64 prefabKey = 0;
		u32 ordinal = 0;
	};

	class PlayModeController {
	public:
		bool IsPlaying() const { return _playing; }
		bool IsPaused() const { return _playing && _paused; }

		void RequestToggle() { _togglePending = true; }
		void RequestPause() { _pauseTogglePending = true; }
		void RequestStep() { _stepPending = true; }

		SHALLTEAR_EDITOR_API bool BeginFrameTick();

		// never from a nested modal tick
		SHALLTEAR_EDITOR_API void PumpMainThread();
	private:
		void EnterPlay();
		void ExitPlay();
		void RebuildWorldFromSnapshots(bool playing);
		void PublishState();

		struct SceneSnapshot {
			Buffer buffer;
			bool wasPrimary = false;
			u64 sceneId = 0;
		};

		struct SelectionEntry {
			Handle<Object> survivor;
			u32 snapshotIndex = 0;
			SceneRowKey object;
			SceneRowKey component;
			bool isComponent = false;
		};

		struct IdCapture {
			u32 snapshotIndex = 0;
			SceneRowKey row;
			SceneRowKey component;
			bool isScene = false;
			bool isComponent = false;
			u64 oldId = 0;
		};

		void CaptureSelection();
		void RestoreSelection(const Vector<Handle<Scene>>& restored);
		void CaptureWorldIds();
		void PublishWorldRemap(const Vector<Handle<Scene>>& restored);

		Vector<SceneSnapshot> _snapshots{GetMemoryResource(MemTag::Editor)};
		Vector<SelectionEntry> _selection{GetMemoryResource(MemTag::Editor)};
		Vector<IdCapture> _idCapture{GetMemoryResource(MemTag::Editor)};
		bool _playing = false;
		bool _paused = false;
		bool _togglePending = false;
		bool _pauseTogglePending = false;
		bool _stepPending = false;
		u32 _stepFrames = 0;
	};

} // namespace Shalltear
