#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class LODFadeMode : u8 {
		None = 0,
		CrossFade = 1,
		AnimatedCrossFade = 2,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::LODFadeMode)
SLT_ENUM_VALUE(None)
SLT_ENUM_VALUE(CrossFade)
SLT_ENUM_VALUE(AnimatedCrossFade)
SLT_END_ENUM()

namespace Shalltear {

	struct LODLevel {
		// share of view height from which level is drawn
		f32 screenRelativeHeight = 0.5f;
		// share of level's height range used for cross fade
		f32 fadeTransitionWidth = 0.0f;
		// renderers on these objects (not children)
		Vector<Handle<SceneObject>> objects{GetMemoryResource(MemTag::Scene)};

		SLT_REFLECT_VALUE(LODLevel)
		SLT_REFLECT_FIELD(screenRelativeHeight, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(fadeTransitionWidth, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_ARRAY(objects)
		SLT_END_REFLECT()
	};

	struct LODChoice {
		static constexpr u8 NONE = 0xFF;
		static constexpr u32 MAX_LEVELS = 64;
		u8 level = NONE;
		u8 next = NONE;
		// level's drawn share out of 255, next drawn for rest
		u8 keep = 255;
	};

	struct LODFadeState {
		u8 from = LODChoice::NONE;
		u8 to = LODChoice::NONE;
		bool started = false;
		f64 start = 0.0;
	};

	class SHALLTEAR_CORE_API LODGroup : public Component {
	public:
		SLT_REFLECT_COMPONENT(LODGroup, Component)
		SLT_REFLECT_FIELD(_fadeMode)
		SLT_REFLECT_FIELD(_crossFadeDuration, SLT_RANGE(0.01f, 10.0f))
		SLT_REFLECT_ARRAY(_levels)
		SLT_END_REFLECT()

		LODGroup() = default;
		~LODGroup() override;

		u32 GetLevelCount() const { return static_cast<u32>(_levels.size()); }
		const LODLevel& GetLevel(u32 index) const { return _levels[index]; }
		LODLevel& EditLevel(u32 index) { return _levels[index]; }
		void SetLevels(Vector<LODLevel> levels) { _levels = std::move(levels); }
		LODLevel& AddLevel(f32 screenRelativeHeight);

		LODFadeMode GetFadeMode() const { return _fadeMode; }
		void SetFadeMode(LODFadeMode mode) { _fadeMode = mode; }
		f32 GetCrossFadeDuration() const { return _crossFadeDuration; }
		void SetCrossFadeDuration(f32 seconds) { _crossFadeDuration = seconds; }

		// height already scaled by LOD bias
		LODChoice Choose(f32 screenRelativeHeight) const;
		// timed fade of AnimatedCrossFade, 1 state per view
		LODChoice Choose(f32 screenRelativeHeight, LODFadeState& state, f64 now) const;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Vector<LODLevel> _levels{GetMemoryResource(MemTag::Scene)};
		LODFadeMode _fadeMode = LODFadeMode::None;
		f32 _crossFadeDuration = 0.5f;
	};

} // namespace Shalltear
