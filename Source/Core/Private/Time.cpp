#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Platform.h"

#include "Shalltear/Core/Settings.h"

#include "Module/EngineStateTable.h"
#include "TimeInternal.h"
#include "TimeSettings.h"

#if defined(SLT_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error "Time not yet implemented for non-Windows platforms"
#endif

namespace Shalltear {

	namespace {
		u64 InitTicksPerSecond() noexcept {
#if defined(SLT_PLATFORM_WINDOWS)
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
			return static_cast<u64>(frequency.QuadPart);
#endif
		}

		struct ClockState {
			FrameTime frame;
			f64 fixedTime = 0.0;
			f64 fixedUnscaledTime = 0.0;
			bool gameplayRan = false;
		};

		struct TimeData {
			u64 frequency;
			u64 initialTicks;
			ClockState clock;
			u32 stageDepth = 0;
			u32 scopeDepth = 0;

			TimeData() noexcept : frequency(InitTicksPerSecond()), initialTicks(Time::GetTicks()) {}
		};

		TimeSettings ResolveTimePolicy() {
			TimeSettings policy;
			if (const TimeSettings* declared = Settings::DeclareCategory<TimeSettings>("Time", SettingScope::Project)) {
				policy = *declared;
			}
			if (!(policy.fixedDeltaTime >= MIN_FIXED_DELTA_TIME)) {
				policy.fixedDeltaTime = MIN_FIXED_DELTA_TIME;
			}
			if (!(policy.maximumDeltaTime >= policy.fixedDeltaTime)) {
				policy.maximumDeltaTime = policy.fixedDeltaTime;
			}
			return policy;
		}

		f32 DeriveFixedAlpha(const ClockState& clock) noexcept {
			const f64 step = static_cast<f64>(clock.frame.fixedDeltaTime);
			if (!(step > 0.0)) {
				return 0.0f;
			}
			const f64 banked = clock.frame.time - clock.fixedTime;
			return banked > 0.0 ? static_cast<f32>(banked / step) : 0.0f;
		}

		TimeData* _boundTime = nullptr;

		TimeData& GetTimeData() noexcept {
			if (_boundTime)
				return *_boundTime;
			static TimeData local;
			return local;
		}
	} // namespace

	u64 Time::GetTicks() noexcept {
#if defined(SLT_PLATFORM_WINDOWS)
		LARGE_INTEGER ticks;
		QueryPerformanceCounter(&ticks);
		return static_cast<u64>(ticks.QuadPart);
#endif
	}

	u64 Time::GetTicksPerSecond() noexcept {
		return GetTimeData().frequency;
	}

	f64 Time::GetRealtimeSinceStartup() noexcept {
		auto& data = GetTimeData();
		u64 current = GetTicks();
		return static_cast<f64>(current - data.initialTicks) / static_cast<f64>(data.frequency);
	}

	void Stopwatch::Start() noexcept {
		if (!_isRunning) {
			_startTime = Time::GetTicks();
			_isRunning = true;
		}
	}

	void Stopwatch::Stop() noexcept {
		if (_isRunning) {
			_accumulatedTicks += Time::GetTicks() - _startTime;
			_isRunning = false;
		}
	}

	void Stopwatch::Reset() noexcept {
		_accumulatedTicks = 0;
		_isRunning = false;
	}

	void Stopwatch::Restart() noexcept {
		_accumulatedTicks = 0;
		_startTime = Time::GetTicks();
		_isRunning = true;
	}

	f64 Stopwatch::GetElapsedSeconds() const noexcept {
		u64 totalTicks = _accumulatedTicks;
		if (_isRunning) {
			totalTicks += Time::GetTicks() - _startTime;
		}
		return static_cast<f64>(totalTicks) / static_cast<f64>(Time::GetTicksPerSecond());
	}

	f64 Stopwatch::GetElapsedMilliseconds() const noexcept {
		return GetElapsedSeconds() * 1000.0;
	}

	FrameTimer::FrameTimer() noexcept {
		_lastTick = Time::GetTicks();
	}

	void FrameTimer::Tick() noexcept {
		u64 currentTick = Time::GetTicks();
		u64 deltaTicks = currentTick - _lastTick;
		_lastTick = currentTick;

		_unscaledDeltaTime = static_cast<f32>(deltaTicks) / static_cast<f32>(Time::GetTicksPerSecond());
	}

	const FrameTime& Time::GetFrame() noexcept {
		return GetTimeData().clock.frame;
	}

	void Time::BeginFrame(f64 unscaledDeltaSeconds) noexcept {
		TimeData& data = GetTimeData();
		SLT_CORE_ASSERT_MSG(data.stageDepth == 0,
							"Time::BeginFrame called inside a frame stage. Nested host frame must not publish the clock");

		ClockState& clock = data.clock;
		FrameTime& frame = clock.frame;

		if (data.scopeDepth == 0) {
			const TimeSettings policy = ResolveTimePolicy();
			frame.fixedDeltaTime = policy.fixedDeltaTime;
			frame.maximumDeltaTime = policy.maximumDeltaTime;
		}

		frame.unscaledDeltaTime = static_cast<f32>(unscaledDeltaSeconds);
		frame.unscaledTime += unscaledDeltaSeconds;

		frame.deltaTime = 0.0f;
		frame.inFixedTimeStep = false;
		++frame.frameCount;
		clock.gameplayRan = false;
	}

	void Time::SetTimeScale(f32 scale) noexcept {
		if (scale < 0.0f) {
			return;
		}
		GetTimeData().clock.frame.timeScale = scale;
	}

	void Time::ResetFrameClock() noexcept {
		TimeData& data = GetTimeData();
		SLT_CORE_ASSERT_MSG(data.stageDepth == 0,
							"Time::ResetFrameClock called inside a frame stage. Clock must not restart under a stage");
		const f32 fixedDeltaTime = data.clock.frame.fixedDeltaTime;
		const f32 maximumDeltaTime = data.clock.frame.maximumDeltaTime;
		data.clock = ClockState{};
		data.clock.frame.fixedDeltaTime = fixedDeltaTime;
		data.clock.frame.maximumDeltaTime = maximumDeltaTime;
	}

	Time::FrameScope::FrameScope(const FrameTime& frame) noexcept {
		TimeData& data = GetTimeData();
		++data.scopeDepth;
		ClockState& clock = data.clock;
		_frame = clock.frame;
		_fixedTime = clock.fixedTime;
		_fixedUnscaledTime = clock.fixedUnscaledTime;
		_gameplayRan = clock.gameplayRan;
		clock.frame = frame;
		clock.fixedTime = frame.time;
		clock.fixedUnscaledTime = frame.unscaledTime;
	}

	Time::FrameScope::~FrameScope() {
		TimeData& data = GetTimeData();
		ClockState& clock = data.clock;
		clock.frame = _frame;
		clock.fixedTime = _fixedTime;
		clock.fixedUnscaledTime = _fixedUnscaledTime;
		clock.gameplayRan = _gameplayRan;
		--data.scopeDepth;
	}

	namespace TimeInternal {

		namespace {
			bool IsGameplayStage(FrameStage stage) noexcept {
				return stage == FrameStage::FixedUpdate || stage == FrameStage::Update || stage == FrameStage::LateUpdate;
			}
		} // namespace

		StageScope::StageScope(FrameStage stage) noexcept {
			TimeData& data = GetTimeData();
			++data.stageDepth;
			ClockState& clock = data.clock;

			if (IsGameplayStage(stage) && !clock.gameplayRan) {
				clock.gameplayRan = true;
				clock.frame.deltaTime = clock.frame.ComputeGameDelta();
				clock.frame.time += static_cast<f64>(clock.frame.deltaTime);
			}

			if (stage != FrameStage::FixedUpdate) {
				clock.frame.fixedAlpha = DeriveFixedAlpha(clock);
				return;
			}

			_saved = clock.frame;
			_stamped = true;

			const f64 step = static_cast<f64>(clock.frame.fixedDeltaTime);
			const f64 unscaledStep = clock.frame.timeScale > 0.0f ? step / static_cast<f64>(clock.frame.timeScale) : step;
			clock.fixedTime += step;
			clock.fixedUnscaledTime += unscaledStep;

			clock.frame.deltaTime = clock.frame.fixedDeltaTime;
			clock.frame.unscaledDeltaTime = static_cast<f32>(unscaledStep);
			clock.frame.time = clock.fixedTime;
			clock.frame.unscaledTime = clock.fixedUnscaledTime;
			clock.frame.fixedAlpha = 0.0f;
			clock.frame.inFixedTimeStep = true;
		}

		StageScope::~StageScope() {
			TimeData& data = GetTimeData();
			if (_stamped) {
				data.clock.frame = _saved;
			}
			--data.stageDepth;
		}

	} // namespace TimeInternal

	ScopedTimer::ScopedTimer(const char* name) noexcept : _name(name) {
		_stopwatch.Start();
	}

	ScopedTimer::~ScopedTimer() {
		_stopwatch.Stop();
		SLT_LOG_TRACE("Performance", "ScopedTimer '{}' took {:.3f} ms", _name, _stopwatch.GetElapsedMilliseconds());
	}

	namespace ModuleInternal {

		void CaptureTimeEngineState(EngineStateTable& table) {
			table.time = &GetTimeData();
		}

		void BindTimeEngineState(const EngineStateTable& table) {
			_boundTime = static_cast<TimeData*>(table.time);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
