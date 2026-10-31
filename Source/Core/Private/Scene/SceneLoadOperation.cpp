#include "Shalltear/Core/Scene/SceneLoadOperation.h"

#include "SceneDocument.h"
#include "SceneInternal.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneEvents.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace Shalltear {

	using namespace SceneInternal;

	// error written before the Failed release, read after acquire
	struct SceneLoadOperation::Impl {
		std::atomic<SceneLoadState> state{SceneLoadState::Pending};
		std::atomic<u32> progressPermille{0};
		std::atomic<bool> activationAllowed{true};

		// main thread takes ownership at integration start
		std::atomic<SceneDocument*> parsedDocument{nullptr};

		JobHandle job;
		SceneIntegration* integration = nullptr;
		Handle<Scene> scene;
		str error;
		str path;
	};

	SceneLoadOperation::SceneLoadOperation() {
		_impl = New<Impl>(MemTag::Scene);
	}

	SceneLoadOperation::~SceneLoadOperation() {
		if (_impl) {
			if (SceneDocument* document = _impl->parsedDocument.load(std::memory_order_acquire)) {
				Delete(document);
			}
			if (_impl->integration) {
				DestroySceneIntegration(_impl->integration);
			}
			Delete(_impl);
			_impl = nullptr;
		}
	}

	SceneLoadState SceneLoadOperation::GetState() const {
		return _impl->state.load(std::memory_order_acquire);
	}

	f32 SceneLoadOperation::GetProgress() const {
		return static_cast<f32>(_impl->progressPermille.load(std::memory_order_acquire)) / 1000.0f;
	}

	bool SceneLoadOperation::IsDone() const {
		SceneLoadState state = GetState();
		return state == SceneLoadState::Done || state == SceneLoadState::Failed;
	}

	Handle<Scene> SceneLoadOperation::GetScene() const {
		return _impl->scene;
	}

	str SceneLoadOperation::GetError() const {
		return _impl->error;
	}

	void SceneLoadOperation::SetActivationAllowed(bool allowed) {
		_impl->activationAllowed.store(allowed, std::memory_order_release);
	}

	void SceneLoadOperation::WaitForCompletion() {
		SLT_SCENE_ASSERT_MAIN_THREAD();
		while (!IsDone() && GetState() != SceneLoadState::AwaitingActivation) {
			SceneInternal::PumpAsyncLoads(-1.0);
			if (!IsDone() && GetState() != SceneLoadState::AwaitingActivation) {
				std::this_thread::yield();
			}
		}
	}

	namespace {

		void RaiseProgress(SceneLoadOperation::Impl& impl, u32 permille) {
			u32 current = impl.progressPermille.load(std::memory_order_relaxed);
			while (current < permille &&
				   !impl.progressPermille.compare_exchange_weak(current, permille, std::memory_order_release)) {
			}
		}

		void MoveToCompleted(SystemState& state, size_t activeIndex) {
			state.completedLoads.push_back(state.activeLoads[activeIndex]);
			state.activeLoads.erase(state.activeLoads.begin() + activeIndex);
		}

		void FailOperation(SceneLoadOperation::Impl& impl, str message) {
			if (SceneDocument* document = impl.parsedDocument.exchange(nullptr, std::memory_order_acq_rel)) {
				Delete(document);
			}
			if (impl.integration) {
				DestroySceneIntegration(impl.integration);
				impl.integration = nullptr;
			}
			impl.error = std::move(message);
			impl.state.store(SceneLoadState::Failed, std::memory_order_release);
		}

		bool StepOperation(SceneLoadOperation& operation, std::chrono::steady_clock::time_point deadline) {
			SceneLoadOperation::Impl& impl = *SceneInternalAccess::ImplOf(operation);

			SceneLoadState state = impl.state.load(std::memory_order_acquire);
			if (state == SceneLoadState::Done || state == SceneLoadState::Failed) {
				return true;
			}

			if (state == SceneLoadState::Pending || state == SceneLoadState::Reading || state == SceneLoadState::Parsing) {
				SceneDocument* document = impl.parsedDocument.load(std::memory_order_acquire);
				if (!document) {
					return false;
				}
				Scene* scene = CreateSceneInternal(SceneNameFromPath(Path(impl.path)), false);
				SceneInternalAccess::SourceGuidOf(*scene) = AssetDatabase::LookupGuid(Path(impl.path));
				impl.scene = Handle<Scene>(scene);
				impl.integration = CreateSceneIntegration(*document, *scene);
				impl.state.store(SceneLoadState::Integrating, std::memory_order_release);
				state = SceneLoadState::Integrating;
			}

			if (state == SceneLoadState::Integrating) {
				Scene* scene = impl.scene.Get();
				if (!scene) {
					FailOperation(impl, "scene destroyed during integration");
					return true;
				}
				bool complete = StepSceneIntegration(*impl.integration, deadline);
				RaiseProgress(impl, 100 + static_cast<u32>(GetSceneIntegrationProgress(*impl.integration) * 800.0f));
				if (!complete) {
					return false;
				}
				DestroySceneIntegration(impl.integration);
				impl.integration = nullptr;
				if (SceneDocument* document = impl.parsedDocument.exchange(nullptr, std::memory_order_acq_rel)) {
					Delete(document);
				}
				RaiseProgress(impl, 900);
				impl.state.store(SceneLoadState::AwaitingActivation, std::memory_order_release);
				state = SceneLoadState::AwaitingActivation;
			}

			if (state == SceneLoadState::AwaitingActivation) {
				Scene* scene = impl.scene.Get();
				if (!scene) {
					FailOperation(impl, "scene unloaded while awaiting activation");
					return true;
				}
				if (!impl.activationAllowed.load(std::memory_order_acquire)) {
					return false;
				}

				MakeSceneVisible(*scene);
				for (SceneObject* root : SceneInternalAccess::RootsOf(*scene)) {
					DeriveActiveSubtree(*root);
				}
				for (SceneObject* root : SceneInternalAccess::RootsOf(*scene)) {
					DispatchIslandAttach(*root);
				}
				RaiseProgress(impl, 1000);
				impl.state.store(SceneLoadState::Done, std::memory_order_release);

				Handle<Scene> sceneHandle(scene);
				SceneEvents::Loaded.Publish(EventBus::GetGlobal(), SceneLoadedEvent{sceneHandle});
				SceneEvents::LoadCompleted.Publish(EventBus::GetGlobal(),
												   SceneLoadCompletedEvent{Handle<SceneLoadOperation>(&operation), sceneHandle});
				return true;
			}
			return false;
		}

	} // anonymous namespace

	namespace SceneInternal {

		void PumpAsyncLoads(f64 budgetSeconds) {
			SystemState& state = GetState();
			if (state.activeLoads.empty()) {
				return;
			}
			auto deadline = budgetSeconds < 0.0
								? std::chrono::steady_clock::time_point::max()
								: std::chrono::steady_clock::now() +
									  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
										  std::chrono::duration<f64>(budgetSeconds));

			for (size_t i = 0; i < state.activeLoads.size();) {
				SceneLoadOperation* operation = state.activeLoads[i].Get();
				if (!operation) {
					state.activeLoads.erase(state.activeLoads.begin() + i);
					continue;
				}
				if (StepOperation(*operation, deadline)) {
					MoveToCompleted(state, i);
				} else {
					++i;
				}
			}
		}

		void CancelAllLoads() {
			SystemState& state = GetState();
			state.replaceRequested = false;
			state.replaceIncoming = {};
			state.replaceKeep = {};
			while (!state.activeLoads.empty()) {
				SceneLoadOperation* operation = state.activeLoads.front().Get();
				if (!operation) {
					state.activeLoads.erase(state.activeLoads.begin());
					continue;
				}
				SceneLoadOperation::Impl& impl = *SceneInternalAccess::ImplOf(*operation);
				// worker quiesces before its output is touched
				if (impl.job.IsValid()) {
					JobSystem::WaitFor(impl.job);
				}
				if (Scene* scene = impl.scene.Get()) {
					if (scene->GetState() == SceneState::Loading) {
						Vector<SceneObject*>& roots = SceneInternalAccess::RootsOf(*scene);
						while (!roots.empty()) {
							FinalizeSubtree(*roots.front(), false);
						}
						DestroySceneNow(*scene);
					}
				}
				FailOperation(impl, "load cancelled");
				MoveToCompleted(state, 0);
			}

			for (const Handle<SceneLoadOperation>& handle : state.completedLoads) {
				if (SceneLoadOperation* operation = handle.Get()) {
					operation->~SceneLoadOperation();
					Free(operation);
				}
			}
			state.completedLoads.clear();
		}

	} // namespace SceneInternal

	Handle<SceneLoadOperation> SceneSystem::LoadSceneAsync(const Path& path) {
		SLT_SCENE_ASSERT_MAIN_THREAD();
		SLT_CORE_ASSERT_MSG(IsStructuralMutationLegal(), "LoadSceneAsync is illegal inside lifecycle hooks");
		SystemState& state = GetState();
		if (!state.initialized || !IsStructuralMutationLegal()) {
			return Handle<SceneLoadOperation>();
		}

		void* memory = Allocate(sizeof(SceneLoadOperation), MemTag::Scene, alignof(SceneLoadOperation));
		SceneLoadOperation* operation = new (memory) SceneLoadOperation();
		SceneLoadOperation::Impl* impl = SceneInternalAccess::ImplOf(*operation);
		impl->path = path.ToString();
		impl->state.store(SceneLoadState::Reading, std::memory_order_release);

		impl->job = JobSystem::Submit([impl]() {
			Buffer bytes = FileSystem::ReadFile(FileSystem::Resolve(Path(impl->path)), MemTag::Scene);
			if (!bytes) {
				impl->error = "can't read scene file";
				impl->state.store(SceneLoadState::Failed, std::memory_order_release);
				return;
			}
			impl->state.store(SceneLoadState::Parsing, std::memory_order_release);
			SceneDocument* document = ParseSceneDocument(std::move(bytes));
			if (!document || document->isPrefab) {
				if (document) {
					Delete(document);
				}
				impl->error = "malformed scene document";
				impl->state.store(SceneLoadState::Failed, std::memory_order_release);
				return;
			}
			impl->progressPermille.store(100, std::memory_order_release);
			impl->parsedDocument.store(document, std::memory_order_release);
		});

		Handle<SceneLoadOperation> handle(operation);
		state.activeLoads.push_back(handle);
		return handle;
	}

} // namespace Shalltear
