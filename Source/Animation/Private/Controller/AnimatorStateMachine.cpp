#include "Shalltear/Animation/AnimatorController.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Time.h"

namespace Shalltear {

	namespace {

		constexpr u32 MAX_BLEND_CHILDREN = 64;

		constexpr u32 MAX_BLEND_DEPTH = 8;

	} // anonymous namespace

	AnimatorStateMachine::~AnimatorStateMachine() = default;

	void AnimatorStateMachine::OnEnable() {}

	void AnimatorStateMachine::OnDisable() {
		_adoptedId = 0;
		_adopted = {};
		_layers.clear();
	}

	Handle<Animator> AnimatorStateMachine::GetAnimator() const {
		SceneObject* owner = GetOwner().Get();
		return owner ? owner->GetComponent<Animator>() : Handle<Animator>();
	}

	void AnimatorStateMachine::OnUpdate() {
		const Animator* animator = GetAnimator().Get();
		if (!animator || animator->GetUpdateMode() == AnimatorUpdateMode::AnimatePhysics) {
			return;
		}
		Tick(animator->GetUpdateMode() == AnimatorUpdateMode::UnscaledTime ? Time::GetUnscaledDeltaTime()
																		   : Time::GetDeltaTime());
	}

	void AnimatorStateMachine::OnFixedUpdate() {
		const Animator* animator = GetAnimator().Get();
		if (!animator || animator->GetUpdateMode() != AnimatorUpdateMode::AnimatePhysics) {
			return;
		}
		Tick(Time::GetDeltaTime());
	}

	bool AnimatorStateMachine::SetFloat(std::string_view name, f32 value) {
		AdoptController();
		const AnimatorController* asset = _adopted.Get();
		if (!asset) {
			return false;
		}
		const i32 index = asset->FindParameter(name);
		if (index < 0 || static_cast<size_t>(index) >= _values.size()) {
			return false;
		}
		_values[static_cast<size_t>(index)] = Math::IsFinite(value) ? value : 0.0f;
		return true;
	}

	bool AnimatorStateMachine::SetInt(std::string_view name, i32 value) {
		return SetFloat(name, static_cast<f32>(value));
	}

	bool AnimatorStateMachine::SetBool(std::string_view name, bool value) {
		return SetFloat(name, value ? 1.0f : 0.0f);
	}

	bool AnimatorStateMachine::SetTrigger(std::string_view name) { return SetFloat(name, 1.0f); }

	bool AnimatorStateMachine::ResetTrigger(std::string_view name) { return SetFloat(name, 0.0f); }

	f32 AnimatorStateMachine::GetFloat(std::string_view name) const {
		const AnimatorController* asset = _adopted.Get();
		if (!asset) {
			return 0.0f;
		}
		const i32 index = asset->FindParameter(name);
		return index >= 0 && static_cast<size_t>(index) < _values.size() ? _values[static_cast<size_t>(index)] : 0.0f;
	}

	i32 AnimatorStateMachine::GetInt(std::string_view name) const {
		const f32 value = GetFloat(name);
		return static_cast<i32>(value >= 0.0f ? value + 0.5f : value - 0.5f);
	}

	bool AnimatorStateMachine::GetBool(std::string_view name) const { return GetFloat(name) != 0.0f; }

	AnimatorController* AnimatorStateMachine::AdoptController() {
		AnimatorController* live = _controller.Get();

		const u32 revision = live ? live->graphRevision : 0;
		if (_adoptedId == _controller.GetInstanceId() && _adoptedRevision == revision &&
			(_controller.GetInstanceId() != 0 || _layers.empty())) {
			return live;
		}
		_adoptedId = _controller.GetInstanceId();
		_adoptedRevision = revision;
		_adopted = _controller;
		_layers.clear();
		_values.clear();

		AnimatorController* asset = live;
		if (!asset) {
			return nullptr;
		}

		_values.reserve(asset->parameters.size());
		for (const AnimatorController::Parameter& parameter : asset->parameters) {
			_values.push_back(parameter.kind == AnimatorParameterKind::Trigger ? 0.0f : parameter.defaultValue);
		}

		Animator* animator = GetAnimator().Get();
		for (size_t index = 0; index < asset->layers.size(); ++index) {
			LayerState state;
			if (index == 0) {
				state.target = 0;
			} else if (animator) {
				state.target = animator->AddLayer();
			}
			_layers.push_back(std::move(state));
		}
		return asset;
	}

	namespace {

		bool ApplyMask(AnimationLayer& layer, const AnimatorController::Layer& authored, const SkeletonAsset& rig) {
			layer.ClearMask();
			for (const AnimatorController::Layer::MaskEntry& entry : authored.mask) {
				layer.SetMaskSubtree(rig, entry.bone, entry.weight);
			}
			return true;
		}

	} // anonymous namespace

	void AnimatorStateMachine::Flatten(i32 node, f32 weight, f32 speed) {
		struct Frame {
			i32 node;
			f32 weight;
			f32 speed;
			u32 depth;
		};
		const AnimatorController* asset = _adopted.Get();
		if (!asset) {
			return;
		}

		Vector<Frame> stack{GetMemoryResource(MemTag::Scene)};
		stack.push_back(Frame{node, weight, speed, 0});
		while (!stack.empty()) {
			const Frame frame = stack.back();
			stack.pop_back();
			if (frame.node < 0 || static_cast<size_t>(frame.node) >= asset->nodes.size() || frame.depth > MAX_BLEND_DEPTH) {
				continue;
			}
			const AnimatorController::MotionNode& tree = asset->nodes[static_cast<size_t>(frame.node)];
			if (tree.kind == AnimatorBlendKind::Clip) {
				AnimationLayer::Motion motion;
				motion.clip = tree.clip;
				motion.weight = frame.weight;
				motion.speed = frame.speed;
				_scratchMotions.push_back(motion);
				continue;
			}

			const Vec2 sample(GetFloat(tree.parameterX), GetFloat(tree.parameterY));
			f32 weights[MAX_BLEND_CHILDREN];
			const f32* values = nullptr;
			if (tree.kind == AnimatorBlendKind::Direct) {
				_scratchValues.clear();
				for (const AnimatorController::MotionChild& child : tree.children) {
					_scratchValues.push_back(GetFloat(child.parameter));
				}
				values = _scratchValues.data();
			}
			const u32 count = AnimatorBlend::Weights(*asset, frame.node, sample, values, weights, MAX_BLEND_CHILDREN);
			if (count < tree.children.size()) {
				SLT_LOG_WARN("Animation", "AnimatorController '{}': blend node {} has {} motions (only {} blended)", asset->virtualPath, frame.node, tree.children.size(), count);
			}
			for (u32 index = count; index > 0; --index) {
				const AnimatorController::MotionChild& child = tree.children[index - 1];
				stack.push_back(Frame{child.node, frame.weight * weights[index - 1], frame.speed * child.speed, frame.depth + 1});
			}
		}
	}

	namespace {

		f32 NormalizedOf(const AnimationLayer& layer) {
			const AnimationLayer::Motion motion = layer.GetMotion(0);
			const AnimationClip* clip = motion.clip.Get();
			if (!clip || !(clip->length > 0.0f) || !(motion.speed != 0.0f)) {
				return 0.0f;
			}
			const f32 length = clip->length / Math::Abs(motion.speed);
			return layer.GetTime() / length;
		}

	} // anonymous namespace

	void AnimatorStateMachine::EnterState(LayerState& live, i32 layerIndex, i32 state, f32 fade, f32 startNormalized) {
		AnimatorController* asset = _adopted.Get();
		Animator* animator = GetAnimator().Get();
		if (!asset || !animator || state < 0 || static_cast<size_t>(state) >= asset->states.size()) {
			return;
		}
		(void)layerIndex;
		const AnimatorController::State& entered = asset->states[static_cast<size_t>(state)];
		live.state = state;

		_scratchMotions.clear();
		Flatten(entered.motion, 1.0f, 1.0f);
		live.motionCount = static_cast<u32>(_scratchMotions.size());

		animator->PlayBlend(_scratchMotions.data(), live.motionCount, fade, live.target);
		AnimationLayer* layer = animator->GetLayer(live.target);
		if (!layer) {
			return;
		}
		layer->speed = entered.speed;
		if (startNormalized != 0.0f) {
			const AnimationLayer::Motion motion = layer->GetMotion(0);
			const AnimationClip* clip = motion.clip.Get();
			if (clip && clip->length > 0.0f && motion.speed != 0.0f) {
				layer->SetTime(startNormalized * clip->length / Math::Abs(motion.speed));
			}
		}
	}

	void AnimatorStateMachine::UpdateWeights(LayerState& live, i32 layerIndex) {
		(void)layerIndex;
		AnimatorController* asset = _adopted.Get();
		Animator* animator = GetAnimator().Get();
		if (!asset || !animator || live.state < 0) {
			return;
		}
		AnimationLayer* layer = animator->GetLayer(live.target);
		if (!layer || live.motionCount == 0) {
			return;
		}
		_scratchMotions.clear();
		Flatten(asset->states[static_cast<size_t>(live.state)].motion, 1.0f, 1.0f);
		const u32 count = Math::Min(static_cast<u32>(_scratchMotions.size()), live.motionCount);
		for (u32 index = 0; index < count; ++index) {
			layer->SetMotionWeight(index, _scratchMotions[index].weight);
		}
	}

	bool AnimatorStateMachine::ConditionsHold(const AnimatorController::Transition& transition) const {
		const AnimatorController* asset = _adopted.Get();
		if (!asset) {
			return false;
		}
		for (const AnimatorController::Condition& condition : transition.conditions) {
			const i32 index = asset->FindParameter(condition.parameter);
			if (index < 0 || static_cast<size_t>(index) >= _values.size()) {
				return false;
			}
			const f32 value = _values[static_cast<size_t>(index)];
			const AnimatorParameterKind kind = asset->parameters[static_cast<size_t>(index)].kind;
			switch (condition.mode) {
			case AnimatorConditionMode::Greater:
				if (!(value > condition.threshold)) {
					return false;
				}
				break;
			case AnimatorConditionMode::Less:
				if (!(value < condition.threshold)) {
					return false;
				}
				break;
			case AnimatorConditionMode::Equals:
			case AnimatorConditionMode::NotEqual: {
				const bool same = kind == AnimatorParameterKind::Float
									  ? value == condition.threshold
									  : static_cast<i32>(value + (value >= 0.0f ? 0.5f : -0.5f)) ==
											static_cast<i32>(condition.threshold + (condition.threshold >= 0.0f ? 0.5f : -0.5f));
				if (same != (condition.mode == AnimatorConditionMode::Equals)) {
					return false;
				}
				break;
			}
			case AnimatorConditionMode::If:
				if (value == 0.0f) {
					return false;
				}
				break;
			case AnimatorConditionMode::IfNot:
				if (value != 0.0f) {
					return false;
				}
				break;
			}
		}
		return true;
	}

	void AnimatorStateMachine::ConsumeTriggers(const AnimatorController::Transition& transition) {
		const AnimatorController* asset = _adopted.Get();
		if (!asset) {
			return;
		}
		for (const AnimatorController::Condition& condition : transition.conditions) {
			const i32 index = asset->FindParameter(condition.parameter);
			if (index < 0 || static_cast<size_t>(index) >= _values.size()) {
				continue;
			}
			if (asset->parameters[static_cast<size_t>(index)].kind == AnimatorParameterKind::Trigger) {
				_values[static_cast<size_t>(index)] = 0.0f;
			}
		}
	}

	const AnimatorController::Transition* AnimatorStateMachine::PickTransition(const LayerState& live, f32 normalized) const {
		const AnimatorController* asset = _adopted.Get();
		if (!asset || live.state < 0 || static_cast<size_t>(live.state) >= asset->states.size()) {
			return nullptr;
		}
		const AnimatorController::State& state = asset->states[static_cast<size_t>(live.state)];

		auto usable = [&](const AnimatorController::Transition& transition, bool fromAnyState) {
			if (transition.destination < 0 || static_cast<size_t>(transition.destination) >= asset->states.size()) {
				return false;
			}
			if (fromAnyState && transition.destination == live.state) {
				return false;
			}
			if (transition.exitTime >= 0.0f && normalized < transition.exitTime) {
				return false;
			}
			return ConditionsHold(transition);
		};

		for (const AnimatorController::Transition& transition : state.transitions) {
			if (usable(transition, false)) {
				return &transition;
			}
		}
		i32 machine = state.machine;
		u32 guard = 0;
		while (machine >= 0 && static_cast<size_t>(machine) < asset->machines.size() && guard++ <= MAX_BLEND_DEPTH) {
			const AnimatorController::StateMachine& owner = asset->machines[static_cast<size_t>(machine)];
			for (const AnimatorController::Transition& transition : owner.anyState) {
				if (usable(transition, true)) {
					return &transition;
				}
			}
			machine = owner.parent;
		}
		return nullptr;
	}

	bool AnimatorStateMachine::Travel(std::string_view state, i32 layer) {
		AdoptController();
		AnimatorController* asset = _adopted.Get();
		if (!asset || layer < 0 || static_cast<size_t>(layer) >= _layers.size()) {
			return false;
		}
		const i32 goal = asset->FindState(state);
		LayerState& live = _layers[static_cast<size_t>(layer)];
		if (goal < 0 || live.state < 0) {
			return false;
		}
		live.travel.clear();
		if (goal == live.state) {
			return true;
		}

		_scratchVisit.clear();
		Vector<i32> came{GetMemoryResource(MemTag::Scene)};
		came.assign(asset->states.size(), -2);
		came[static_cast<size_t>(live.state)] = -1;
		_scratchVisit.push_back(live.state);
		size_t cursor = 0;
		bool found = false;
		while (cursor < _scratchVisit.size() && !found) {
			const i32 at = _scratchVisit[cursor++];
			for (const AnimatorController::Transition& transition : asset->states[static_cast<size_t>(at)].transitions) {
				const i32 next = transition.destination;
				if (next < 0 || static_cast<size_t>(next) >= asset->states.size() || came[static_cast<size_t>(next)] != -2) {
					continue;
				}
				came[static_cast<size_t>(next)] = at;
				if (next == goal) {
					found = true;
					break;
				}
				_scratchVisit.push_back(next);
			}
		}
		if (!found) {
			return false;
		}
		for (i32 at = goal; at != live.state; at = came[static_cast<size_t>(at)]) {
			live.travel.push_back(at);
		}
		return true;
	}

	u32 AnimatorStateMachine::GetTravelRemaining(i32 layer) const {
		return layer >= 0 && static_cast<size_t>(layer) < _layers.size()
				   ? static_cast<u32>(_layers[static_cast<size_t>(layer)].travel.size())
				   : 0u;
	}

	i32 AnimatorStateMachine::GetCurrentState(i32 layer) const {
		return layer >= 0 && static_cast<size_t>(layer) < _layers.size() ? _layers[static_cast<size_t>(layer)].state : -1;
	}

	f32 AnimatorStateMachine::GetNormalizedTime(i32 layer) const {
		if (layer < 0 || static_cast<size_t>(layer) >= _layers.size()) {
			return 0.0f;
		}
		const Animator* animator = GetAnimator().Get();
		const AnimationLayer* target = animator ? animator->GetLayer(_layers[static_cast<size_t>(layer)].target) : nullptr;
		return target ? NormalizedOf(*target) : 0.0f;
	}

	bool AnimatorStateMachine::Play(std::string_view state, f32 fade, i32 layer) {
		AdoptController();
		AnimatorController* asset = _adopted.Get();
		if (!asset || layer < 0 || static_cast<size_t>(layer) >= _layers.size()) {
			return false;
		}
		const i32 index = asset->FindState(state);
		if (index < 0) {
			return false;
		}
		LayerState& live = _layers[static_cast<size_t>(layer)];
		live.travel.clear();
		EnterState(live, layer, index, fade, 0.0f);
		return true;
	}

	void AnimatorStateMachine::Tick(f32 deltaTime) {
		(void)deltaTime;
		AnimatorController* asset = AdoptController();
		Animator* animator = GetAnimator().Get();
		if (!asset || !animator) {
			return;
		}
		const Skeleton* rigComponent = animator->GetSkeleton().Get();
		const SkeletonAsset* rig = rigComponent ? rigComponent->GetSkeleton().Get() : nullptr;

		for (size_t index = 0; index < _layers.size() && index < asset->layers.size(); ++index) {
			LayerState& live = _layers[index];
			const AnimatorController::Layer& authored = asset->layers[index];

			if (live.state < 0) {
				const i32 machine = authored.machine;
				const i32 defaultState = machine >= 0 && static_cast<size_t>(machine) < asset->machines.size()
											 ? asset->machines[static_cast<size_t>(machine)].defaultState
											 : -1;
				if (defaultState < 0) {
					continue;
				}
				EnterState(live, static_cast<i32>(index), defaultState, 0.0f, 0.0f);
			}

			AnimationLayer* target = animator->GetLayer(live.target);
			if (!target) {
				continue;
			}
			target->weight = authored.weight;
			target->blend = authored.blend;
			if (rig && !authored.mask.empty() && target->mask.size() != rig->GetBoneCount()) {
				ApplyMask(*target, authored, *rig);
			}

			const f32 normalized = NormalizedOf(*target);
			const AnimatorController::Transition* take = nullptr;
			if (!live.travel.empty()) {
				const i32 next = live.travel.back();
				for (const AnimatorController::Transition& transition : asset->states[static_cast<size_t>(live.state)].transitions) {
					if (transition.destination != next) {
						continue;
					}
					if (transition.exitTime >= 0.0f && normalized < transition.exitTime) {
						break;
					}
					take = &transition;
					break;
				}
				if (take) {
					live.travel.pop_back();
				}
			} else {
				take = PickTransition(live, normalized);
			}

			if (take) {
				ConsumeTriggers(*take);
				const f32 start = take->syncPhase ? normalized : take->offset;
				EnterState(live, static_cast<i32>(index), take->destination, take->duration, start);
			}
			UpdateWeights(live, static_cast<i32>(index));
		}
	}

} // namespace Shalltear
