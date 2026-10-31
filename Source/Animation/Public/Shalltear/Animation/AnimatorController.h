#pragma once

#include "Shalltear/Animation/AnimationAPI.h"
#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationLayer.h"
#include "Shalltear/Animation/Animator.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class AnimatorParameterKind : u8 {
		Float = 0,
		Int = 1,
		Bool = 2,
		Trigger = 3,
	};

	enum class AnimatorConditionMode : u8 {
		Greater = 0,
		Less = 1,
		Equals = 2,
		NotEqual = 3,
		If = 4,
		IfNot = 5,
	};

	enum class AnimatorBlendKind : u8 {
		Clip = 0,
		Simple1D = 1,
		FreeformCartesian2D = 2,
		FreeformDirectional2D = 3,
		Direct = 4,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AnimatorParameterKind)
SLT_ENUM_VALUE(Float)
SLT_ENUM_VALUE(Int)
SLT_ENUM_VALUE(Bool)
SLT_ENUM_VALUE(Trigger)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AnimatorConditionMode)
SLT_ENUM_VALUE(Greater)
SLT_ENUM_VALUE(Less)
SLT_ENUM_VALUE(Equals)
SLT_ENUM_VALUE(NotEqual)
SLT_ENUM_VALUE(If)
SLT_ENUM_VALUE(IfNot)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AnimatorBlendKind)
SLT_ENUM_VALUE(Clip)
SLT_ENUM_VALUE(Simple1D)
SLT_ENUM_VALUE(FreeformCartesian2D)
SLT_ENUM_VALUE(FreeformDirectional2D)
SLT_ENUM_VALUE(Direct)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_ANIMATION_API AnimatorController : public Asset {
	public:
		SLT_REFLECT_CLASS(AnimatorController)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		struct Parameter {
			str name;
			AnimatorParameterKind kind = AnimatorParameterKind::Float;

			f32 defaultValue = 0.0f;
		};

		struct Condition {
			str parameter;
			AnimatorConditionMode mode = AnimatorConditionMode::Greater;

			f32 threshold = 0.0f;
		};

		struct Transition {
			i32 destination = -1;

			Vector<Condition> conditions{GetMemoryResource(MemTag::Asset)};

			f32 exitTime = -1.0f;

			f32 duration = 0.25f;

			f32 offset = 0.0f;

			bool syncPhase = false;
		};

		struct State {
			str name;

			i32 motion = -1;

			f32 speed = 1.0f;

			i32 machine = 0;

			Vec2 position{};

			Vector<Transition> transitions{GetMemoryResource(MemTag::Asset)};
		};

		struct StateMachine {
			str name;

			i32 parent = -1;

			i32 defaultState = -1;

			Vector<Transition> anyState{GetMemoryResource(MemTag::Asset)};

			Vec2 position{};

			Vec2 entryPosition{};
			Vec2 anyStatePosition{};

			Vec2 upPosition{};
		};

		struct MotionChild {
			i32 node = -1;

			Vec2 position{};

			f32 speed = 1.0f;

			str parameter;
		};

		struct MotionNode {
			AnimatorBlendKind kind = AnimatorBlendKind::Clip;

			Guid clipGuid;
			Handle<AnimationClip> clip;

			str parameterX;
			str parameterY;

			Vector<MotionChild> children{GetMemoryResource(MemTag::Asset)};
		};

		struct Layer {
			str name;

			i32 machine = -1;

			f32 weight = 1.0f;
			AnimationBlendMode blend = AnimationBlendMode::Override;

			struct MaskEntry {
				str bone;
				f32 weight = 1.0f;
			};
			Vector<MaskEntry> mask{GetMemoryResource(MemTag::Asset)};
		};

		AnimatorController() = default;
		~AnimatorController() override;

		Vector<Parameter> parameters{GetMemoryResource(MemTag::Asset)};
		Vector<MotionNode> nodes{GetMemoryResource(MemTag::Asset)};
		Vector<State> states{GetMemoryResource(MemTag::Asset)};
		Vector<StateMachine> machines{GetMemoryResource(MemTag::Asset)};
		Vector<Layer> layers{GetMemoryResource(MemTag::Asset)};

		u32 graphRevision = 0;

		i32 FindParameter(std::string_view name) const;
		i32 FindState(std::string_view name) const;

		StringHash GetAssetTypeId() const override { return AnimatorController::GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	};

	namespace AnimatorBlend {

		SHALLTEAR_ANIMATION_API u32 Weights(const AnimatorController& controller, i32 node, Vec2 sample,
											const f32* values, f32* out, u32 capacity);

	} // namespace AnimatorBlend

	class SHALLTEAR_ANIMATION_API AnimatorStateMachine : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(AnimatorStateMachine, Behavior)
		SLT_REFLECT_FIELD(_controller)
		SLT_END_REFLECT()

		~AnimatorStateMachine() override;

		Handle<AnimatorController> GetController() const { return _controller; }
		void SetController(Handle<AnimatorController> controller) { _controller = controller; }

		bool SetFloat(std::string_view name, f32 value);
		bool SetInt(std::string_view name, i32 value);
		bool SetBool(std::string_view name, bool value);
		bool SetTrigger(std::string_view name);
		bool ResetTrigger(std::string_view name);

		f32 GetFloat(std::string_view name) const;
		i32 GetInt(std::string_view name) const;
		bool GetBool(std::string_view name) const;

		bool Play(std::string_view state, f32 fade = ANIMATION_DEFAULT_FADE, i32 layer = 0);

		bool Travel(std::string_view state, i32 layer = 0);

		i32 GetCurrentState(i32 layer) const;

		f32 GetNormalizedTime(i32 layer) const;

		u32 GetTravelRemaining(i32 layer) const;

		void Tick(f32 deltaTime);

		Handle<Animator> GetAnimator() const;
	protected:
		void OnEnable() override;
		void OnDisable() override;
		void OnUpdate() override;
		void OnFixedUpdate() override;
	private:
		struct LayerState {
			i32 state = -1;

			i32 target = 0;

			Vector<i32> travel{GetMemoryResource(MemTag::Scene)};

			u32 motionCount = 0;
		};

		AnimatorController* AdoptController();
		void EnterState(LayerState& live, i32 layerIndex, i32 state, f32 fade, f32 startNormalized);
		void UpdateWeights(LayerState& live, i32 layerIndex);
		bool ConditionsHold(const AnimatorController::Transition& transition) const;
		void ConsumeTriggers(const AnimatorController::Transition& transition);
		const AnimatorController::Transition* PickTransition(const LayerState& live, f32 normalized) const;

		void Flatten(i32 node, f32 weight, f32 speed);

		Handle<AnimatorController> _controller;

		Handle<AnimatorController> _adopted;
		u64 _adoptedId = 0;

		u32 _adoptedRevision = 0;

		Vector<LayerState> _layers{GetMemoryResource(MemTag::Scene)};

		Vector<f32> _values{GetMemoryResource(MemTag::Scene)};

		Vector<AnimationLayer::Motion> _scratchMotions{GetMemoryResource(MemTag::Scene)};

		Vector<f32> _scratchValues{GetMemoryResource(MemTag::Scene)};
		Vector<i32> _scratchVisit{GetMemoryResource(MemTag::Scene)};
	};

} // namespace Shalltear
