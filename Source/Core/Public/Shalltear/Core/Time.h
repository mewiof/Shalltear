#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	inline constexpr f32 DEFAULT_MAXIMUM_DELTA_TIME = 0.1f;

	inline constexpr f32 DEFAULT_FIXED_DELTA_TIME = 1.0f / 60.0f;

	struct FrameTime {
		f32 deltaTime = 0.0f;
		f32 unscaledDeltaTime = 0.0f; // never clamped
		f32 fixedDeltaTime = DEFAULT_FIXED_DELTA_TIME;
		f32 maximumDeltaTime = DEFAULT_MAXIMUM_DELTA_TIME;
		f64 time = 0.0;
		f64 unscaledTime = 0.0;
		f32 timeScale = 1.0f;
		f32 fixedAlpha = 0.0f;
		u64 frameCount = 0;
		bool inFixedTimeStep = false;

		f32 ComputeGameDelta() const noexcept {
			const f32 scaled = unscaledDeltaTime * timeScale;
			return scaled < maximumDeltaTime ? scaled : maximumDeltaTime;
		}
	};

	class SHALLTEAR_CORE_API Time {
	public:
		static u64 GetTicks() noexcept;

		static u64 GetTicksPerSecond() noexcept;

		static f64 GetRealtimeSinceStartup() noexcept;

		static const FrameTime& GetFrame() noexcept;

		static f32 GetDeltaTime() noexcept { return GetFrame().deltaTime; }
		static f32 GetUnscaledDeltaTime() noexcept { return GetFrame().unscaledDeltaTime; }
		static f32 GetFixedDeltaTime() noexcept { return GetFrame().fixedDeltaTime; }
		static f64 GetTime() noexcept { return GetFrame().time; }
		static f64 GetUnscaledTime() noexcept { return GetFrame().unscaledTime; }
		static f32 GetTimeScale() noexcept { return GetFrame().timeScale; }
		static f32 GetFixedAlpha() noexcept { return GetFrame().fixedAlpha; }
		static u64 GetFrameCount() noexcept { return GetFrame().frameCount; }
		static bool IsInFixedTimeStep() noexcept { return GetFrame().inFixedTimeStep; }

		static void SetTimeScale(f32 scale) noexcept;

		static void BeginFrame(f64 unscaledDeltaSeconds) noexcept;

		static void ResetFrameClock() noexcept;

		class FrameScope {
		public:
			SHALLTEAR_CORE_API explicit FrameScope(const FrameTime& frame) noexcept;
			SHALLTEAR_CORE_API ~FrameScope();

			FrameScope(const FrameScope&) = delete;
			FrameScope& operator=(const FrameScope&) = delete;
			FrameScope(FrameScope&&) = delete;
			FrameScope& operator=(FrameScope&&) = delete;
		private:
			FrameTime _frame;
			f64 _fixedTime;
			f64 _fixedUnscaledTime;
			bool _gameplayRan;
		};
	};

	class SHALLTEAR_CORE_API Stopwatch {
	public:
		Stopwatch() noexcept = default;

		void Start() noexcept;
		void Stop() noexcept;
		void Reset() noexcept;
		void Restart() noexcept;

		f64 GetElapsedSeconds() const noexcept;
		f64 GetElapsedMilliseconds() const noexcept;

		bool IsRunning() const noexcept { return _isRunning; }
	private:
		u64 _startTime = 0;
		u64 _accumulatedTicks = 0;
		bool _isRunning = false;
	};

	class SHALLTEAR_CORE_API FrameTimer {
	public:
		FrameTimer() noexcept;

		void Tick() noexcept;

		f32 GetUnscaledDeltaTime() const noexcept { return _unscaledDeltaTime; }
	private:
		u64 _lastTick = 0;
		f32 _unscaledDeltaTime = 0.0f;
	};

	class SHALLTEAR_CORE_API ScopedTimer {
	public:
		explicit ScopedTimer(const char* name) noexcept;
		~ScopedTimer();

		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;
		ScopedTimer(ScopedTimer&&) = delete;
		ScopedTimer& operator=(ScopedTimer&&) = delete;
	private:
		const char* _name;
		Stopwatch _stopwatch;
	};

} // namespace Shalltear

#if defined(SHALLTEAR_CONFIG_RELEASE)
#define SLT_PROFILE_SCOPE(name)
#else
#define _SLT_TIME_CAT_IMPL(a, b) a##b
#define _SLT_TIME_CAT(a, b) _SLT_TIME_CAT_IMPL(a, b)
#define SLT_PROFILE_SCOPE(name) ::Shalltear::ScopedTimer _SLT_TIME_CAT(scopedTimer_, __LINE__)(name)
#endif
