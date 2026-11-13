#include "Shalltear/Core/Graphics/LODGroup.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear {

	LODGroup::~LODGroup() = default;

	LODLevel& LODGroup::AddLevel(f32 screenRelativeHeight) {
		LODLevel& level = _levels.emplace_back();
		level.screenRelativeHeight = screenRelativeHeight;
		return level;
	}

	LODChoice LODGroup::Choose(f32 screenRelativeHeight) const {
		LODChoice choice;
		const u32 count = static_cast<u32>(Math::Min<size_t>(_levels.size(), LODChoice::MAX_LEVELS));
		f32 above = 1.0f;
		for (u32 i = 0; i < count; ++i) {
			const LODLevel& level = _levels[i];
			if (screenRelativeHeight < level.screenRelativeHeight) {
				above = level.screenRelativeHeight;
				continue;
			}
			choice.level = static_cast<u8>(i);
			if (_fadeMode == LODFadeMode::CrossFade && level.fadeTransitionWidth > 0.0f) {
				const f32 band = Math::Clamp01(level.fadeTransitionWidth) * Math::Max(above - level.screenRelativeHeight, 0.0f);
				if (band > 0.0f && screenRelativeHeight < level.screenRelativeHeight + band) {
					const f32 through = (screenRelativeHeight - level.screenRelativeHeight) / band;
					choice.keep = static_cast<u8>(std::lround(Math::Clamp01(through) * 255.0f));
					choice.next = i + 1 < count ? static_cast<u8>(i + 1) : LODChoice::NONE;
				}
			}
			return choice;
		}
		return choice;
	}

	LODChoice LODGroup::Choose(f32 screenRelativeHeight, LODFadeState& state, f64 now) const {
		LODChoice choice = Choose(screenRelativeHeight);
		if (_fadeMode != LODFadeMode::AnimatedCrossFade) {
			return choice;
		}
		const u8 target = choice.level;
		const f64 duration = Math::Max(_crossFadeDuration, 0.001f);
		if (!state.started) {
			state = {target, target, true, now - duration};
			return choice;
		}
		f64 progress = Math::Clamp((now - state.start) / duration, 0.0, 1.0);
		if (target != state.to) {
			if (target == state.from && progress < 1.0) {
				state.from = state.to;
				progress = 1.0 - progress;
				state.start = now - progress * duration;
			} else {
				state.from = progress >= 0.5 ? state.to : state.from;
				state.start = now;
				progress = 0.0;
			}
			state.to = target;
		}
		if (progress >= 1.0 || state.from == state.to) {
			state.from = state.to;
			return {state.to, LODChoice::NONE, 255};
		}
		choice.level = state.from;
		choice.next = state.to;
		choice.keep = static_cast<u8>(std::lround((1.0 - progress) * 255.0));
		return choice;
	}

	void LODGroup::OnEnable() {
		RenderSystem::RegisterLODGroup(Handle<LODGroup>(this));
	}

	void LODGroup::OnDisable() {
		RenderSystem::UnregisterLODGroup(Handle<LODGroup>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::LODGroup)
