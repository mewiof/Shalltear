#include "AnimatorGraphOps.h"
#include "AnimatorControllerText.h"

#include "Shalltear/Animation/AnimationClip.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Shalltear {

	namespace AnimatorGraphOps {

		namespace {

			bool IsState(const AnimatorController& controller, i32 state) {
				return state >= 0 && static_cast<size_t>(state) < controller.states.size();
			}

			bool IsMachine(const AnimatorController& controller, i32 machine) {
				return machine >= 0 && static_cast<size_t>(machine) < controller.machines.size();
			}

			void Touch(AnimatorController& controller) { ++controller.graphRevision; }

			void MarkReachable(const AnimatorController& controller, i32 node, Vector<u8>& reached) {
				if (node < 0 || static_cast<size_t>(node) >= controller.nodes.size()) {
					return;
				}
				if (reached[static_cast<size_t>(node)] != 0) {
					return;
				}
				reached[static_cast<size_t>(node)] = 1;
				for (const AnimatorController::MotionChild& child : controller.nodes[static_cast<size_t>(node)].children) {
					MarkReachable(controller, child.node, reached);
				}
			}

			void CompactNodes(AnimatorController& controller) {
				if (controller.nodes.empty()) {
					return;
				}
				Vector<u8> reached(controller.nodes.size(), 0, GetMemoryResource(MemTag::Asset));
				for (const AnimatorController::State& state : controller.states) {
					MarkReachable(controller, state.motion, reached);
				}

				Vector<i32> remap(controller.nodes.size(), -1, GetMemoryResource(MemTag::Asset));
				i32 next = 0;
				for (size_t index = 0; index < controller.nodes.size(); ++index) {
					if (reached[index] != 0) {
						remap[index] = next++;
					}
				}
				if (static_cast<size_t>(next) == controller.nodes.size()) {
					return;
				}

				Vector<AnimatorController::MotionNode> kept{GetMemoryResource(MemTag::Asset)};
				kept.reserve(static_cast<size_t>(next));
				for (size_t index = 0; index < controller.nodes.size(); ++index) {
					if (reached[index] == 0) {
						continue;
					}
					AnimatorController::MotionNode node = std::move(controller.nodes[index]);
					for (AnimatorController::MotionChild& child : node.children) {
						child.node = (child.node >= 0 && static_cast<size_t>(child.node) < remap.size())
										 ? remap[static_cast<size_t>(child.node)]
										 : -1;
					}
					kept.push_back(std::move(node));
				}
				for (AnimatorController::State& state : controller.states) {
					state.motion = (state.motion >= 0 && static_cast<size_t>(state.motion) < remap.size())
									   ? remap[static_cast<size_t>(state.motion)]
									   : -1;
				}
				controller.nodes = std::move(kept);
			}

			template <typename Fn>
			void ForEachTransitionList(AnimatorController& controller, Fn&& fn) {
				for (AnimatorController::State& state : controller.states) {
					fn(state.transitions);
				}
				for (AnimatorController::StateMachine& machine : controller.machines) {
					fn(machine.anyState);
				}
			}

		} // anonymous namespace

		i32 AddState(AnimatorController& controller, i32 machine, std::string_view name, Vec2 position) {
			if (!IsMachine(controller, machine)) {
				return -1;
			}
			AnimatorController::State state;
			state.name = str(name);
			state.machine = machine;
			state.position = position;
			controller.states.push_back(std::move(state));

			const i32 index = static_cast<i32>(controller.states.size()) - 1;
			if (controller.machines[static_cast<size_t>(machine)].defaultState < 0) {
				controller.machines[static_cast<size_t>(machine)].defaultState = index;
			}
			Touch(controller);
			return index;
		}

		bool RemoveState(AnimatorController& controller, i32 state) {
			if (!IsState(controller, state)) {
				return false;
			}
			const i32 doomed = state;

			ForEachTransitionList(controller, [doomed](Vector<AnimatorController::Transition>& list) {
				for (size_t index = list.size(); index-- > 0;) {
					if (list[index].destination == doomed) {
						list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
					} else if (list[index].destination > doomed) {
						--list[index].destination;
					}
				}
			});

			const i32 doomedMachine = controller.states[static_cast<size_t>(doomed)].machine;
			for (AnimatorController::StateMachine& machine : controller.machines) {
				if (machine.defaultState > doomed) {
					--machine.defaultState;
				} else if (machine.defaultState == doomed) {
					machine.defaultState = -1;
				}
			}

			controller.states.erase(controller.states.begin() + static_cast<std::ptrdiff_t>(doomed));

			if (IsMachine(controller, doomedMachine) &&
				controller.machines[static_cast<size_t>(doomedMachine)].defaultState < 0) {
				for (size_t index = 0; index < controller.states.size(); ++index) {
					if (controller.states[index].machine == doomedMachine) {
						controller.machines[static_cast<size_t>(doomedMachine)].defaultState = static_cast<i32>(index);
						break;
					}
				}
			}

			CompactNodes(controller);
			Touch(controller);
			return true;
		}

		i32 SetStateClip(AnimatorController& controller, i32 state, const Guid& clipGuid, Handle<AnimationClip> clip) {
			if (!IsState(controller, state)) {
				return -1;
			}
			AnimatorController::State& target = controller.states[static_cast<size_t>(state)];
			const i32 existing = target.motion;
			if (existing >= 0 && static_cast<size_t>(existing) < controller.nodes.size() &&
				controller.nodes[static_cast<size_t>(existing)].kind == AnimatorBlendKind::Clip) {
				AnimatorController::MotionNode& node = controller.nodes[static_cast<size_t>(existing)];
				node.clipGuid = clipGuid;
				node.clip = clip;
				Touch(controller);
				return existing;
			}

			AnimatorController::MotionNode node;
			node.kind = AnimatorBlendKind::Clip;
			node.clipGuid = clipGuid;
			node.clip = clip;
			controller.nodes.push_back(std::move(node));
			target.motion = static_cast<i32>(controller.nodes.size()) - 1;
			Touch(controller);
			return target.motion;
		}

		i32 AddTransition(AnimatorController& controller, i32 fromState, i32 toState) {
			if (!IsState(controller, fromState) || !IsState(controller, toState)) {
				return -1;
			}
			AnimatorController::Transition transition;
			transition.destination = toState;
			Vector<AnimatorController::Transition>& list = controller.states[static_cast<size_t>(fromState)].transitions;
			list.push_back(std::move(transition));
			Touch(controller);
			return static_cast<i32>(list.size()) - 1;
		}

		bool RemoveTransition(AnimatorController& controller, i32 fromState, i32 index) {
			if (!IsState(controller, fromState)) {
				return false;
			}
			Vector<AnimatorController::Transition>& list = controller.states[static_cast<size_t>(fromState)].transitions;
			if (index < 0 || static_cast<size_t>(index) >= list.size()) {
				return false;
			}
			list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
			Touch(controller);
			return true;
		}

		i32 AddAnyStateTransition(AnimatorController& controller, i32 machine, i32 toState) {
			if (!IsMachine(controller, machine) || !IsState(controller, toState)) {
				return -1;
			}
			AnimatorController::Transition transition;
			transition.destination = toState;
			transition.exitTime = -1.0f;
			Vector<AnimatorController::Transition>& list = controller.machines[static_cast<size_t>(machine)].anyState;
			list.push_back(std::move(transition));
			Touch(controller);
			return static_cast<i32>(list.size()) - 1;
		}

		bool RemoveAnyStateTransition(AnimatorController& controller, i32 machine, i32 index) {
			if (!IsMachine(controller, machine)) {
				return false;
			}
			Vector<AnimatorController::Transition>& list = controller.machines[static_cast<size_t>(machine)].anyState;
			if (index < 0 || static_cast<size_t>(index) >= list.size()) {
				return false;
			}
			list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
			Touch(controller);
			return true;
		}

		bool SetDefaultState(AnimatorController& controller, i32 machine, i32 state) {
			if (!IsMachine(controller, machine) || !IsState(controller, state)) {
				return false;
			}
			if (controller.states[static_cast<size_t>(state)].machine != machine) {
				return false;
			}
			controller.machines[static_cast<size_t>(machine)].defaultState = state;
			Touch(controller);
			return true;
		}

		i32 AddMachine(AnimatorController& controller, i32 parent, std::string_view name, Vec2 position) {
			if (!IsMachine(controller, parent)) {
				return -1;
			}
			AnimatorController::StateMachine machine;
			machine.name = str(name);
			machine.parent = parent;
			machine.position = position;
			controller.machines.push_back(std::move(machine));
			Touch(controller);
			return static_cast<i32>(controller.machines.size()) - 1;
		}

		bool RemoveMachine(AnimatorController& controller, i32 machine) {
			if (!IsMachine(controller, machine)) {
				return false;
			}
			for (const AnimatorController::Layer& layer : controller.layers) {
				if (layer.machine == machine) {
					return false;
				}
			}

			Vector<u8> doomed(controller.machines.size(), 0, GetMemoryResource(MemTag::Asset));
			doomed[static_cast<size_t>(machine)] = 1;
			for (u32 guard = 0; guard < static_cast<u32>(controller.machines.size()); ++guard) {
				bool grew = false;
				for (size_t index = 0; index < controller.machines.size(); ++index) {
					if (doomed[index] != 0) {
						continue;
					}
					const i32 parent = controller.machines[index].parent;
					if (parent >= 0 && static_cast<size_t>(parent) < doomed.size() && doomed[static_cast<size_t>(parent)] != 0) {
						doomed[index] = 1;
						grew = true;
					}
				}
				if (!grew) {
					break;
				}
			}

			for (;;) {
				i32 victim = -1;
				for (size_t index = 0; index < controller.states.size(); ++index) {
					const i32 owner = controller.states[index].machine;
					if (owner >= 0 && static_cast<size_t>(owner) < doomed.size() && doomed[static_cast<size_t>(owner)] != 0) {
						victim = static_cast<i32>(index);
						break;
					}
				}
				if (victim < 0) {
					break;
				}
				RemoveState(controller, victim);
			}

			Vector<i32> remap(controller.machines.size(), -1, GetMemoryResource(MemTag::Asset));
			i32 next = 0;
			for (size_t index = 0; index < controller.machines.size(); ++index) {
				if (doomed[index] == 0) {
					remap[index] = next++;
				}
			}
			auto remapped = [&remap](i32 index) {
				return index >= 0 && static_cast<size_t>(index) < remap.size() ? remap[static_cast<size_t>(index)] : -1;
			};

			Vector<AnimatorController::StateMachine> kept{GetMemoryResource(MemTag::Asset)};
			kept.reserve(static_cast<size_t>(next));
			for (size_t index = 0; index < controller.machines.size(); ++index) {
				if (doomed[index] != 0) {
					continue;
				}
				AnimatorController::StateMachine survivor = std::move(controller.machines[index]);
				survivor.parent = remapped(survivor.parent);
				kept.push_back(std::move(survivor));
			}
			for (AnimatorController::State& state : controller.states) {
				state.machine = remapped(state.machine);
			}
			for (AnimatorController::Layer& layer : controller.layers) {
				layer.machine = remapped(layer.machine);
			}
			controller.machines = std::move(kept);
			Touch(controller);
			return true;
		}

		u32 MachineAncestry(const AnimatorController& controller, i32 machine, i32* out, u32 capacity) {
			if (!IsMachine(controller, machine) || out == nullptr || capacity == 0) {
				return 0;
			}
			Vector<i32> chain{GetMemoryResource(MemTag::Asset)};
			i32 walk = machine;
			for (u32 guard = 0; guard < static_cast<u32>(controller.machines.size()) + 1; ++guard) {
				if (!IsMachine(controller, walk)) {
					break;
				}
				chain.push_back(walk);
				walk = controller.machines[static_cast<size_t>(walk)].parent;
				if (walk < 0) {
					break;
				}
			}
			u32 written = 0;
			for (size_t index = chain.size(); index-- > 0 && written < capacity;) {
				out[written++] = chain[index];
			}
			return written;
		}

		bool MachineContains(const AnimatorController& controller, i32 ancestor, i32 machine) {
			if (!IsMachine(controller, ancestor) || !IsMachine(controller, machine)) {
				return false;
			}
			i32 walk = machine;
			for (u32 guard = 0; guard < static_cast<u32>(controller.machines.size()) + 1; ++guard) {
				if (walk == ancestor) {
					return true;
				}
				if (!IsMachine(controller, walk)) {
					return false;
				}
				walk = controller.machines[static_cast<size_t>(walk)].parent;
			}
			return false;
		}

		bool StatePath(const AnimatorController& controller, i32 state, str& out) {
			if (!IsState(controller, state)) {
				return false;
			}
			const AnimatorController::State& target = controller.states[static_cast<size_t>(state)];

			constexpr u32 MAX_DEPTH = 32;
			i32 chain[MAX_DEPTH];
			const u32 depth = MachineAncestry(controller, target.machine, chain, MAX_DEPTH);

			str path;
			for (u32 index = 0; index < depth; ++index) {
				path += controller.machines[static_cast<size_t>(chain[index])].name;
				path += "/";
			}
			path += target.name;
			out = std::move(path);
			return true;
		}

		i32 AddParameter(AnimatorController& controller, std::string_view name, AnimatorParameterKind kind) {
			if (name.empty() || controller.FindParameter(name) >= 0) {
				return -1;
			}
			AnimatorController::Parameter parameter;
			parameter.name = str(name);
			parameter.kind = kind;
			controller.parameters.push_back(std::move(parameter));
			Touch(controller);
			return static_cast<i32>(controller.parameters.size()) - 1;
		}

		bool RemoveParameter(AnimatorController& controller, i32 parameter) {
			if (parameter < 0 || static_cast<size_t>(parameter) >= controller.parameters.size()) {
				return false;
			}
			const str name = controller.parameters[static_cast<size_t>(parameter)].name;

			ForEachTransitionList(controller, [&name](Vector<AnimatorController::Transition>& list) {
				for (AnimatorController::Transition& transition : list) {
					for (size_t index = transition.conditions.size(); index-- > 0;) {
						if (transition.conditions[index].parameter == name) {
							transition.conditions.erase(transition.conditions.begin() +
														static_cast<std::ptrdiff_t>(index));
						}
					}
				}
			});

			for (AnimatorController::MotionNode& node : controller.nodes) {
				if (node.parameterX == name) {
					node.parameterX.clear();
				}
				if (node.parameterY == name) {
					node.parameterY.clear();
				}
				for (AnimatorController::MotionChild& child : node.children) {
					if (child.parameter == name) {
						child.parameter.clear();
					}
				}
			}

			controller.parameters.erase(controller.parameters.begin() + static_cast<std::ptrdiff_t>(parameter));
			Touch(controller);
			return true;
		}

		bool RenameParameter(AnimatorController& controller, i32 parameter, std::string_view name) {
			if (parameter < 0 || static_cast<size_t>(parameter) >= controller.parameters.size() || name.empty()) {
				return false;
			}
			const i32 clash = controller.FindParameter(name);
			if (clash >= 0) {
				return clash == parameter;
			}
			const str previous = controller.parameters[static_cast<size_t>(parameter)].name;
			const str renamed = str(name);

			ForEachTransitionList(controller, [&previous, &renamed](Vector<AnimatorController::Transition>& list) {
				for (AnimatorController::Transition& transition : list) {
					for (AnimatorController::Condition& condition : transition.conditions) {
						if (condition.parameter == previous) {
							condition.parameter = renamed;
						}
					}
				}
			});
			for (AnimatorController::MotionNode& node : controller.nodes) {
				if (node.parameterX == previous) {
					node.parameterX = renamed;
				}
				if (node.parameterY == previous) {
					node.parameterY = renamed;
				}
				for (AnimatorController::MotionChild& child : node.children) {
					if (child.parameter == previous) {
						child.parameter = renamed;
					}
				}
			}
			controller.parameters[static_cast<size_t>(parameter)].name = renamed;
			Touch(controller);
			return true;
		}

		bool MoveParameter(AnimatorController& controller, i32 parameter, i32 index) {
			const i32 count = static_cast<i32>(controller.parameters.size());
			if (parameter < 0 || parameter >= count || index < 0 || index >= count || index == parameter) {
				return false;
			}
			auto first = controller.parameters.begin();
			if (index < parameter) {
				std::rotate(first + index, first + parameter, first + parameter + 1);
			} else {
				std::rotate(first + parameter, first + parameter + 1, first + index + 1);
			}
			Touch(controller);
			return true;
		}

		namespace {

			bool IsLayer(const AnimatorController& controller, i32 layer) {
				return layer >= 0 && static_cast<size_t>(layer) < controller.layers.size();
			}

			bool LayerNameTaken(const AnimatorController& controller, std::string_view name, i32 ignore) {
				for (size_t index = 0; index < controller.layers.size(); ++index) {
					if (static_cast<i32>(index) == ignore) {
						continue;
					}
					if (controller.layers[index].name == name) {
						return true;
					}
				}
				return false;
			}

		} // anonymous namespace

		i32 AddLayer(AnimatorController& controller, std::string_view name) {
			if (name.empty() || LayerNameTaken(controller, name, -1)) {
				return -1;
			}
			AnimatorController::StateMachine root;
			root.name = str(name);
			controller.machines.push_back(std::move(root));
			const i32 machine = static_cast<i32>(controller.machines.size()) - 1;

			AnimatorController::Layer layer;
			layer.name = str(name);
			layer.machine = machine;
			controller.layers.push_back(std::move(layer));
			Touch(controller);
			return static_cast<i32>(controller.layers.size()) - 1;
		}

		bool RemoveLayer(AnimatorController& controller, i32 layer) {
			if (!IsLayer(controller, layer)) {
				return false;
			}
			if (layer == 0) {
				return false;
			}
			const i32 machine = controller.layers[static_cast<size_t>(layer)].machine;

			controller.layers.erase(controller.layers.begin() + layer);
			if (IsMachine(controller, machine)) {
				RemoveMachine(controller, machine);
			}
			Touch(controller);
			return true;
		}

		bool RenameLayer(AnimatorController& controller, i32 layer, std::string_view name) {
			if (!IsLayer(controller, layer) || name.empty() || LayerNameTaken(controller, name, layer)) {
				return false;
			}
			controller.layers[static_cast<size_t>(layer)].name = str(name);
			const i32 machine = controller.layers[static_cast<size_t>(layer)].machine;
			if (IsMachine(controller, machine)) {
				controller.machines[static_cast<size_t>(machine)].name = str(name);
			}
			Touch(controller);
			return true;
		}

		bool SetLayerWeight(AnimatorController& controller, i32 layer, f32 weight) {
			if (!IsLayer(controller, layer)) {
				return false;
			}
			const f32 clamped = Math::IsFinite(weight) ? Math::Clamp(weight, 0.0f, 1.0f) : 0.0f;
			controller.layers[static_cast<size_t>(layer)].weight = clamped;
			Touch(controller);
			return true;
		}

		bool SetLayerBlend(AnimatorController& controller, i32 layer, AnimationBlendMode blend) {
			if (!IsLayer(controller, layer)) {
				return false;
			}
			controller.layers[static_cast<size_t>(layer)].blend = blend;
			Touch(controller);
			return true;
		}

		i32 AddLayerMaskEntry(AnimatorController& controller, i32 layer, std::string_view bone, f32 weight) {
			if (!IsLayer(controller, layer) || bone.empty()) {
				return -1;
			}
			AnimatorController::Layer& target = controller.layers[static_cast<size_t>(layer)];
			for (const AnimatorController::Layer::MaskEntry& entry : target.mask) {
				if (entry.bone == bone) {
					return -1;
				}
			}
			AnimatorController::Layer::MaskEntry entry;
			entry.bone = str(bone);
			entry.weight = Math::IsFinite(weight) ? Math::Clamp(weight, 0.0f, 1.0f) : 0.0f;
			target.mask.push_back(std::move(entry));
			Touch(controller);
			return static_cast<i32>(target.mask.size()) - 1;
		}

		bool RemoveLayerMaskEntry(AnimatorController& controller, i32 layer, i32 entry) {
			if (!IsLayer(controller, layer)) {
				return false;
			}
			AnimatorController::Layer& target = controller.layers[static_cast<size_t>(layer)];
			if (entry < 0 || static_cast<size_t>(entry) >= target.mask.size()) {
				return false;
			}
			target.mask.erase(target.mask.begin() + entry);
			Touch(controller);
			return true;
		}

		bool SetLayerMaskEntryWeight(AnimatorController& controller, i32 layer, i32 entry, f32 weight) {
			if (!IsLayer(controller, layer)) {
				return false;
			}
			AnimatorController::Layer& target = controller.layers[static_cast<size_t>(layer)];
			if (entry < 0 || static_cast<size_t>(entry) >= target.mask.size()) {
				return false;
			}
			target.mask[static_cast<size_t>(entry)].weight = Math::IsFinite(weight) ? Math::Clamp(weight, 0.0f, 1.0f) : 0.0f;
			Touch(controller);
			return true;
		}

		namespace {

			constexpr f32 COLUMN_STEP = 260.0f;
			constexpr f32 ROW_STEP = 96.0f;
			constexpr u32 ROWS_PER_COLUMN = 6;

			bool StatesArePiled(const AnimatorController& controller, i32 machine) {
				u32 count = 0;
				for (const AnimatorController::State& state : controller.states) {
					if (state.machine != machine) {
						continue;
					}
					if (state.position.x != 0.0f || state.position.y != 0.0f) {
						return false;
					}
					++count;
				}
				return count > 1;
			}

			bool SyntheticNodesAreCoincident(const AnimatorController& controller, i32 machine) {
				const AnimatorController::StateMachine& target = controller.machines[static_cast<size_t>(machine)];
				if (target.entryPosition.x == target.anyStatePosition.x &&
					target.entryPosition.y == target.anyStatePosition.y) {
					return true;
				}
				if (target.parent < 0) {
					return false;
				}
				return target.upPosition.x == target.entryPosition.x && target.upPosition.y == target.entryPosition.y;
			}

		} // anonymous namespace

		bool NeedsAutoLayout(const AnimatorController& controller, i32 machine) {
			if (!IsMachine(controller, machine)) {
				return false;
			}
			return StatesArePiled(controller, machine) || SyntheticNodesAreCoincident(controller, machine);
		}

		bool AutoLayout(AnimatorController& controller, i32 machine) {
			if (!IsMachine(controller, machine)) {
				return false;
			}

			bool moved = false;
			if (StatesArePiled(controller, machine)) {
				u32 placed = 0;
				for (AnimatorController::State& state : controller.states) {
					if (state.machine != machine) {
						continue;
					}
					const u32 column = placed / ROWS_PER_COLUMN;
					const u32 row = placed % ROWS_PER_COLUMN;
					state.position = Vec2(static_cast<f32>(column) * COLUMN_STEP, static_cast<f32>(row) * ROW_STEP);
					++placed;
				}
				moved = true;
			}

			if (SyntheticNodesAreCoincident(controller, machine)) {
				AnimatorController::StateMachine& target = controller.machines[static_cast<size_t>(machine)];
				target.entryPosition = Vec2(-COLUMN_STEP, 0.0f);
				target.anyStatePosition = Vec2(-COLUMN_STEP, ROW_STEP * 1.5f);
				if (target.parent >= 0) {
					target.upPosition = Vec2(-COLUMN_STEP, ROW_STEP * 3.0f);
				}
				moved = true;
			}
			return moved;
		}

		str CaptureSnapshot(const AnimatorController& controller) {
			return AnimatorControllerText::Write(controller, AnimatorControllerText::Shape::Snapshot);
		}

		bool RestoreSnapshot(AnimatorController& controller, std::string_view snapshot) {
			AnimatorControllerText::Arrays incoming;
			if (!AnimatorControllerText::Read(snapshot, controller.virtualPath, incoming)) {
				return false;
			}
			const str shapeBefore = AnimatorControllerText::Write(controller, AnimatorControllerText::Shape::Structure);

			for (AnimatorController::MotionNode& node : incoming.nodes) {
				if (node.kind != AnimatorBlendKind::Clip || !node.clipGuid.IsValid()) {
					continue;
				}
				if (Asset* resolved = AssetDatabase::LoadAssetByGuid(node.clipGuid)) {
					node.clip = Handle<AnimationClip>(static_cast<AnimationClip*>(resolved));
				}
			}
			std::swap(controller.parameters, incoming.parameters);
			std::swap(controller.nodes, incoming.nodes);
			std::swap(controller.states, incoming.states);
			std::swap(controller.machines, incoming.machines);
			std::swap(controller.layers, incoming.layers);
			for (AnimatorController::MotionNode& node : incoming.nodes) {
				if (AnimationClip* held = node.clip.Get()) {
					AssetDatabase::UnloadAsset(held);
				}
			}

			if (AnimatorControllerText::Write(controller, AnimatorControllerText::Shape::Structure) != shapeBefore) {
				++controller.graphRevision;
			}
			return true;
		}

	} // namespace AnimatorGraphOps

} // namespace Shalltear
