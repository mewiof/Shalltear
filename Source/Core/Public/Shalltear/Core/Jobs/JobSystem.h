#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Jobs/Job.h"
#include "Shalltear/Core/Types.h"

#include <initializer_list>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace Shalltear {

	enum class JobType : u8 {
		Normal,
		LongRunning
	};

	class SHALLTEAR_CORE_API JobSystem {
	public:
		static void Initialize();
		static void Terminate();

		template <typename F>
		static JobHandle Submit(F&& func, JobType type = JobType::Normal) {
			return Submit(std::forward<F>(func), std::span<const JobHandle>(), type);
		}

		template <typename F>
		static JobHandle Submit(F&& func, JobHandle dependency, JobType type = JobType::Normal) {
			if (dependency.IsValid()) {
				JobHandle deps[1] = {dependency};
				return Submit(std::forward<F>(func), std::span<const JobHandle>(deps), type);
			} else {
				return Submit(std::forward<F>(func), std::span<const JobHandle>(), type);
			}
		}

		template <typename F>
		static JobHandle Submit(F&& func, std::span<const JobHandle> dependencies, JobType type = JobType::Normal) {
			static_assert(sizeof(std::decay_t<F>) <= 64, "Job lambda payload too large! Limit is 64 bytes.");

			auto wrapper = [](void* data) {
				using T = std::decay_t<F>;
				T* ptr = static_cast<T*>(data);
				(*ptr)();
				ptr->~T();
			};

			auto construct = [](void* dest, void* src) {
				using T = std::decay_t<F>;
				using OriginalF = std::remove_reference_t<F>;
				new (dest) T(std::forward<F>(*static_cast<OriginalF*>(src)));
			};

			return SubmitPayload(wrapper, construct, &func, dependencies, type);
		}

		template <typename F>
		static JobHandle Submit(F&& func, std::initializer_list<JobHandle> dependencies, JobType type = JobType::Normal) {
			return Submit(std::forward<F>(func), std::span<const JobHandle>(dependencies.begin(), dependencies.size()), type);
		}

		static void WaitFor(JobHandle handle);

		static void WaitForAll(std::span<const JobHandle> handles);
		static void WaitForAll(std::initializer_list<JobHandle> handles) {
			WaitForAll(std::span<const JobHandle>(handles.begin(), handles.size()));
		}

		static u32 GetWorkerCount();
	private:
		using JobConstructFunc = void (*)(void* dest, void* src);
		static JobHandle SubmitPayload(void (*func)(void*), JobConstructFunc construct, void* srcPayload, std::span<const JobHandle> dependencies, JobType type);
	};

} // namespace Shalltear
