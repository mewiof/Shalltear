#include "Shalltear/Animation/AnimatorController.h"

#include "AnimatorControllerText.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

namespace Shalltear {

	AnimatorController::~AnimatorController() = default;

	i32 AnimatorController::FindParameter(std::string_view name) const {
		for (size_t index = 0; index < parameters.size(); ++index) {
			if (parameters[index].name == name) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

	i32 AnimatorController::FindState(std::string_view name) const {
		for (size_t index = 0; index < states.size(); ++index) {
			if (states[index].name == name) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

	void AnimatorController::OnLoad() {
		for (MotionNode& node : nodes) {
			if (node.kind != AnimatorBlendKind::Clip || !node.clipGuid.IsValid()) {
				continue;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(node.clipGuid);
			if (!resolved) {
				SLT_LOG_WARN("Animation", "AnimatorController '{}': clip {} is unresolvable (its motion is silent)", virtualPath, node.clipGuid.ToString());
				continue;
			}
			node.clip = Handle<AnimationClip>(static_cast<AnimationClip*>(resolved));
		}
	}

	void AnimatorController::OnUnload() {
		for (MotionNode& node : nodes) {
			if (AnimationClip* resolved = node.clip.Get()) {
				AssetDatabase::UnloadAsset(resolved);
			}
			node.clip = {};
		}
	}

	namespace {

		JSONValue WriteTransition(const AnimatorController::Transition& transition) {
			JSONValue out = JSONValue::MakeObject();
			out.AddMember("destination", JSONValue::MakeNumber(transition.destination));
			out.AddMember("exitTime", JSONValue::MakeNumber(transition.exitTime));
			out.AddMember("duration", JSONValue::MakeNumber(transition.duration));
			out.AddMember("offset", JSONValue::MakeNumber(transition.offset));
			out.AddMember("syncPhase", JSONValue::MakeBool(transition.syncPhase));
			JSONValue conditions = JSONValue::MakeArray();
			for (const AnimatorController::Condition& condition : transition.conditions) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("parameter", JSONValue::MakeString(condition.parameter));
				row.AddMember("mode", JSONValue::MakeNumber(static_cast<f64>(static_cast<u8>(condition.mode))));
				row.AddMember("threshold", JSONValue::MakeNumber(condition.threshold));
				conditions.Push(std::move(row));
			}
			out.AddMember("conditions", std::move(conditions));
			return out;
		}

		void ReadTransition(const JSONValue& value, AnimatorController::Transition& out) {
			out.destination = static_cast<i32>(value["destination"].AsInt(-1));
			out.exitTime = static_cast<f32>(value["exitTime"].AsNumber(-1.0));
			out.duration = static_cast<f32>(value["duration"].AsNumber(0.25));
			out.offset = static_cast<f32>(value["offset"].AsNumber(0.0));
			out.syncPhase = value["syncPhase"].AsBool(false);
			const JSONValue& conditions = value["conditions"];
			for (size_t index = 0; index < conditions.Size(); ++index) {
				const JSONValue& row = conditions.ChildAt(index);
				AnimatorController::Condition condition;
				condition.parameter = str(row["parameter"].AsString());
				condition.mode = static_cast<AnimatorConditionMode>(row["mode"].AsInt(0));
				condition.threshold = static_cast<f32>(row["threshold"].AsNumber(0.0));
				out.conditions.push_back(std::move(condition));
			}
		}

	} // anonymous namespace

	namespace AnimatorControllerText {

		str Write(const AnimatorController& controller, Shape shape) {
			const bool layout = shape != Shape::Structure;
			JSONValue root = JSONValue::MakeObject();

			Vector<str> guidText{GetMemoryResource(MemTag::Asset)};
			guidText.reserve(controller.nodes.size());

			JSONValue jsonParameters = JSONValue::MakeArray();
			for (const AnimatorController::Parameter& parameter : controller.parameters) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("name", JSONValue::MakeString(parameter.name));
				row.AddMember("kind", JSONValue::MakeNumber(static_cast<f64>(static_cast<u8>(parameter.kind))));
				row.AddMember("default", JSONValue::MakeNumber(parameter.defaultValue));
				jsonParameters.Push(std::move(row));
			}
			root.AddMember("parameters", std::move(jsonParameters));

			JSONValue jsonNodes = JSONValue::MakeArray();
			for (const AnimatorController::MotionNode& node : controller.nodes) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("kind", JSONValue::MakeNumber(static_cast<f64>(static_cast<u8>(node.kind))));
				guidText.push_back(node.clipGuid.ToString());
				row.AddMember("clip", JSONValue::MakeString(guidText.back()));
				row.AddMember("parameterX", JSONValue::MakeString(node.parameterX));
				row.AddMember("parameterY", JSONValue::MakeString(node.parameterY));
				JSONValue children = JSONValue::MakeArray();
				for (const AnimatorController::MotionChild& child : node.children) {
					JSONValue entry = JSONValue::MakeObject();
					entry.AddMember("node", JSONValue::MakeNumber(child.node));
					entry.AddMember("x", JSONValue::MakeNumber(child.position.x));
					entry.AddMember("y", JSONValue::MakeNumber(child.position.y));
					entry.AddMember("speed", JSONValue::MakeNumber(child.speed));
					entry.AddMember("parameter", JSONValue::MakeString(child.parameter));
					children.Push(std::move(entry));
				}
				row.AddMember("children", std::move(children));
				jsonNodes.Push(std::move(row));
			}
			root.AddMember("nodes", std::move(jsonNodes));

			JSONValue jsonMachines = JSONValue::MakeArray();
			for (const AnimatorController::StateMachine& machine : controller.machines) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("name", JSONValue::MakeString(machine.name));
				row.AddMember("parent", JSONValue::MakeNumber(machine.parent));
				row.AddMember("defaultState", JSONValue::MakeNumber(machine.defaultState));
				if (layout) {
					row.AddMember("x", JSONValue::MakeNumber(machine.position.x));
					row.AddMember("y", JSONValue::MakeNumber(machine.position.y));
					row.AddMember("entryX", JSONValue::MakeNumber(machine.entryPosition.x));
					row.AddMember("entryY", JSONValue::MakeNumber(machine.entryPosition.y));
					row.AddMember("anyX", JSONValue::MakeNumber(machine.anyStatePosition.x));
					row.AddMember("anyY", JSONValue::MakeNumber(machine.anyStatePosition.y));
					row.AddMember("upX", JSONValue::MakeNumber(machine.upPosition.x));
					row.AddMember("upY", JSONValue::MakeNumber(machine.upPosition.y));
				}
				JSONValue any = JSONValue::MakeArray();
				for (const AnimatorController::Transition& transition : machine.anyState) {
					any.Push(WriteTransition(transition));
				}
				row.AddMember("anyState", std::move(any));
				jsonMachines.Push(std::move(row));
			}
			root.AddMember("machines", std::move(jsonMachines));

			JSONValue jsonStates = JSONValue::MakeArray();
			for (const AnimatorController::State& state : controller.states) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("name", JSONValue::MakeString(state.name));
				row.AddMember("motion", JSONValue::MakeNumber(state.motion));
				row.AddMember("speed", JSONValue::MakeNumber(state.speed));
				row.AddMember("machine", JSONValue::MakeNumber(state.machine));
				if (layout) {
					row.AddMember("x", JSONValue::MakeNumber(state.position.x));
					row.AddMember("y", JSONValue::MakeNumber(state.position.y));
				}
				JSONValue transitions = JSONValue::MakeArray();
				for (const AnimatorController::Transition& transition : state.transitions) {
					transitions.Push(WriteTransition(transition));
				}
				row.AddMember("transitions", std::move(transitions));
				jsonStates.Push(std::move(row));
			}
			root.AddMember("states", std::move(jsonStates));

			JSONValue jsonLayers = JSONValue::MakeArray();
			for (const AnimatorController::Layer& layer : controller.layers) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("name", JSONValue::MakeString(layer.name));
				row.AddMember("machine", JSONValue::MakeNumber(layer.machine));
				row.AddMember("weight", JSONValue::MakeNumber(layer.weight));
				row.AddMember("blend", JSONValue::MakeNumber(static_cast<f64>(static_cast<u8>(layer.blend))));
				JSONValue mask = JSONValue::MakeArray();
				for (const AnimatorController::Layer::MaskEntry& entry : layer.mask) {
					JSONValue row2 = JSONValue::MakeObject();
					row2.AddMember("bone", JSONValue::MakeString(entry.bone));
					row2.AddMember("weight", JSONValue::MakeNumber(entry.weight));
					mask.Push(std::move(row2));
				}
				row.AddMember("mask", std::move(mask));
				jsonLayers.Push(std::move(row));
			}
			root.AddMember("layers", std::move(jsonLayers));

			return JSONSerialize(root, shape == Shape::File);
		}

		bool Read(std::string_view text, std::string_view source, Arrays& out) {
			LinearAllocator arena(text.size() + 64, MemTag::Asset);
			JSONValue root = JSONParse(text, arena);
			if (!root.IsObject()) {
				SLT_LOG_ERROR("Animation", "AnimatorController '{}' isn't a JSON object", source);
				return false;
			}

			Arrays read;

			const JSONValue& jsonParameters = root["parameters"];
			for (size_t index = 0; index < jsonParameters.Size(); ++index) {
				const JSONValue& row = jsonParameters.ChildAt(index);
				AnimatorController::Parameter parameter;
				parameter.name = str(row["name"].AsString());
				parameter.kind = static_cast<AnimatorParameterKind>(row["kind"].AsInt(0));
				parameter.defaultValue = static_cast<f32>(row["default"].AsNumber(0.0));
				read.parameters.push_back(std::move(parameter));
			}

			const JSONValue& jsonNodes = root["nodes"];
			for (size_t index = 0; index < jsonNodes.Size(); ++index) {
				const JSONValue& row = jsonNodes.ChildAt(index);
				AnimatorController::MotionNode node;
				node.kind = static_cast<AnimatorBlendKind>(row["kind"].AsInt(0));
				node.clipGuid = Guid::FromString(row["clip"].AsString());
				node.parameterX = str(row["parameterX"].AsString());
				node.parameterY = str(row["parameterY"].AsString());
				const JSONValue& children = row["children"];
				for (size_t child = 0; child < children.Size(); ++child) {
					const JSONValue& entry = children.ChildAt(child);
					AnimatorController::MotionChild motion;
					motion.node = static_cast<i32>(entry["node"].AsInt(-1));
					motion.position = Vec2(static_cast<f32>(entry["x"].AsNumber(0.0)), static_cast<f32>(entry["y"].AsNumber(0.0)));
					motion.speed = static_cast<f32>(entry["speed"].AsNumber(1.0));
					motion.parameter = str(entry["parameter"].AsString());
					node.children.push_back(std::move(motion));
				}
				read.nodes.push_back(std::move(node));
			}

			const JSONValue& jsonMachines = root["machines"];
			for (size_t index = 0; index < jsonMachines.Size(); ++index) {
				const JSONValue& row = jsonMachines.ChildAt(index);
				AnimatorController::StateMachine machine;
				machine.name = str(row["name"].AsString());
				machine.parent = static_cast<i32>(row["parent"].AsInt(-1));
				machine.defaultState = static_cast<i32>(row["defaultState"].AsInt(-1));
				machine.position = Vec2(static_cast<f32>(row["x"].AsNumber(0.0)), static_cast<f32>(row["y"].AsNumber(0.0)));
				machine.entryPosition = Vec2(static_cast<f32>(row["entryX"].AsNumber(0.0)), static_cast<f32>(row["entryY"].AsNumber(0.0)));
				machine.anyStatePosition = Vec2(static_cast<f32>(row["anyX"].AsNumber(0.0)), static_cast<f32>(row["anyY"].AsNumber(0.0)));
				machine.upPosition = Vec2(static_cast<f32>(row["upX"].AsNumber(0.0)), static_cast<f32>(row["upY"].AsNumber(0.0)));
				const JSONValue& any = row["anyState"];
				for (size_t entry = 0; entry < any.Size(); ++entry) {
					AnimatorController::Transition transition;
					ReadTransition(any.ChildAt(entry), transition);
					machine.anyState.push_back(std::move(transition));
				}
				read.machines.push_back(std::move(machine));
			}

			const JSONValue& jsonStates = root["states"];
			for (size_t index = 0; index < jsonStates.Size(); ++index) {
				const JSONValue& row = jsonStates.ChildAt(index);
				AnimatorController::State state;
				state.name = str(row["name"].AsString());
				state.motion = static_cast<i32>(row["motion"].AsInt(-1));
				state.speed = static_cast<f32>(row["speed"].AsNumber(1.0));
				state.machine = static_cast<i32>(row["machine"].AsInt(0));
				state.position = Vec2(static_cast<f32>(row["x"].AsNumber(0.0)), static_cast<f32>(row["y"].AsNumber(0.0)));
				const JSONValue& transitions = row["transitions"];
				for (size_t entry = 0; entry < transitions.Size(); ++entry) {
					AnimatorController::Transition transition;
					ReadTransition(transitions.ChildAt(entry), transition);
					state.transitions.push_back(std::move(transition));
				}
				read.states.push_back(std::move(state));
			}

			const JSONValue& jsonLayers = root["layers"];
			for (size_t index = 0; index < jsonLayers.Size(); ++index) {
				const JSONValue& row = jsonLayers.ChildAt(index);
				AnimatorController::Layer layer;
				layer.name = str(row["name"].AsString());
				layer.machine = static_cast<i32>(row["machine"].AsInt(-1));
				layer.weight = static_cast<f32>(row["weight"].AsNumber(1.0));
				layer.blend = static_cast<AnimationBlendMode>(row["blend"].AsInt(0));
				const JSONValue& mask = row["mask"];
				for (size_t entry = 0; entry < mask.Size(); ++entry) {
					const JSONValue& maskRow = mask.ChildAt(entry);
					AnimatorController::Layer::MaskEntry maskEntry;
					maskEntry.bone = str(maskRow["bone"].AsString());
					maskEntry.weight = static_cast<f32>(maskRow["weight"].AsNumber(1.0));
					layer.mask.push_back(std::move(maskEntry));
				}
				read.layers.push_back(std::move(layer));
			}

			out = std::move(read);
			return true;
		}

	} // namespace AnimatorControllerText

	bool AnimatorController::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, AnimatorControllerText::Write(*this, AnimatorControllerText::Shape::File));
	}

	bool AnimatorController::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			return true;
		}
		AnimatorControllerText::Arrays read;
		if (!AnimatorControllerText::Read(text, sourcePath.ToString(), read)) {
			return false;
		}
		parameters = std::move(read.parameters);
		nodes = std::move(read.nodes);
		states = std::move(read.states);
		machines = std::move(read.machines);
		layers = std::move(read.layers);
		return true;
	}

	namespace {

		void CookTransition(CookWriter& out, const AnimatorController::Transition& transition) {
			out.Write(transition.destination);
			out.Write(transition.exitTime);
			out.Write(transition.duration);
			out.Write(transition.offset);
			out.Write(static_cast<u8>(transition.syncPhase ? 1 : 0));
			out.Write(static_cast<u32>(transition.conditions.size()));
			for (const AnimatorController::Condition& condition : transition.conditions) {
				out.WriteString(condition.parameter);
				out.Write(static_cast<u8>(condition.mode));
				out.Write(condition.threshold);
			}
		}

		constexpr size_t BYTES_PER_TRANSITION = sizeof(i32) + sizeof(f32) * 3 + sizeof(u8) + sizeof(u32);

		bool ReadTransition(CookReader& in, AnimatorController::Transition& out) {
			in.Read(out.destination);
			in.Read(out.exitTime);
			in.Read(out.duration);
			in.Read(out.offset);
			u8 sync = 0;
			in.Read(sync);
			out.syncPhase = sync != 0;
			u32 count = 0;
			in.Read(count);
			constexpr size_t BYTES_PER_CONDITION = sizeof(u32) + sizeof(u8) + sizeof(f32);
			if (in.Failed() || in.Remaining() < static_cast<size_t>(count) * BYTES_PER_CONDITION) {
				return false;
			}
			out.conditions.reserve(count);
			for (u32 index = 0; index < count; ++index) {
				AnimatorController::Condition condition;
				condition.parameter = str(in.ReadString());
				u8 mode = 0;
				in.Read(mode);
				condition.mode = static_cast<AnimatorConditionMode>(mode);
				in.Read(condition.threshold);
				out.conditions.push_back(std::move(condition));
			}
			return !in.Failed();
		}

	} // anonymous namespace

	bool AnimatorController::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;

		descriptor.Write(static_cast<u32>(parameters.size()));
		for (const Parameter& parameter : parameters) {
			descriptor.WriteString(parameter.name);
			descriptor.Write(static_cast<u8>(parameter.kind));
			descriptor.Write(parameter.defaultValue);
		}

		descriptor.Write(static_cast<u32>(nodes.size()));
		for (const MotionNode& node : nodes) {
			descriptor.Write(static_cast<u8>(node.kind));
			descriptor.WriteAssetRef(node.clipGuid);
			descriptor.WriteString(node.parameterX);
			descriptor.WriteString(node.parameterY);
			descriptor.Write(static_cast<u32>(node.children.size()));
			for (const MotionChild& child : node.children) {
				descriptor.Write(child.node);
				descriptor.Write(child.position);
				descriptor.Write(child.speed);
				descriptor.WriteString(child.parameter);
			}
		}

		descriptor.Write(static_cast<u32>(machines.size()));
		for (const StateMachine& machine : machines) {
			descriptor.WriteString(machine.name);
			descriptor.Write(machine.parent);
			descriptor.Write(machine.defaultState);
			descriptor.Write(machine.position);
			descriptor.Write(machine.entryPosition);
			descriptor.Write(machine.anyStatePosition);
			descriptor.Write(machine.upPosition);
			descriptor.Write(static_cast<u32>(machine.anyState.size()));
			for (const Transition& transition : machine.anyState) {
				CookTransition(descriptor, transition);
			}
		}

		descriptor.Write(static_cast<u32>(states.size()));
		for (const State& state : states) {
			descriptor.WriteString(state.name);
			descriptor.Write(state.motion);
			descriptor.Write(state.speed);
			descriptor.Write(state.machine);
			descriptor.Write(state.position);
			descriptor.Write(static_cast<u32>(state.transitions.size()));
			for (const Transition& transition : state.transitions) {
				CookTransition(descriptor, transition);
			}
		}

		descriptor.Write(static_cast<u32>(layers.size()));
		for (const Layer& layer : layers) {
			descriptor.WriteString(layer.name);
			descriptor.Write(layer.machine);
			descriptor.Write(layer.weight);
			descriptor.Write(static_cast<u8>(layer.blend));
			descriptor.Write(static_cast<u32>(layer.mask.size()));
			for (const Layer::MaskEntry& entry : layer.mask) {
				descriptor.WriteString(entry.bone);
				descriptor.Write(entry.weight);
			}
		}
		return true;
	}

	bool AnimatorController::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;

		Vector<Parameter> readParameters{GetMemoryResource(MemTag::Asset)};
		Vector<MotionNode> readNodes{GetMemoryResource(MemTag::Asset)};
		Vector<State> readStates{GetMemoryResource(MemTag::Asset)};
		Vector<StateMachine> readMachines{GetMemoryResource(MemTag::Asset)};
		Vector<Layer> readLayers{GetMemoryResource(MemTag::Asset)};

		u32 count = 0;
		descriptor.Read(count);
		constexpr size_t BYTES_PER_PARAMETER = sizeof(u32) + sizeof(u8) + sizeof(f32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_PARAMETER) {
			return false;
		}
		readParameters.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			Parameter parameter;
			parameter.name = str(descriptor.ReadString());
			u8 kind = 0;
			descriptor.Read(kind);
			parameter.kind = static_cast<AnimatorParameterKind>(kind);
			descriptor.Read(parameter.defaultValue);
			readParameters.push_back(std::move(parameter));
		}

		descriptor.Read(count);
		constexpr size_t BYTES_PER_NODE = sizeof(u8) + sizeof(Guid) + sizeof(u32) * 3;
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_NODE) {
			return false;
		}
		readNodes.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			MotionNode node;
			u8 kind = 0;
			descriptor.Read(kind);
			node.kind = static_cast<AnimatorBlendKind>(kind);
			descriptor.Read(node.clipGuid);
			node.parameterX = str(descriptor.ReadString());
			node.parameterY = str(descriptor.ReadString());
			u32 children = 0;
			descriptor.Read(children);
			constexpr size_t BYTES_PER_CHILD = sizeof(i32) + sizeof(Vec2) + sizeof(f32) + sizeof(u32);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(children) * BYTES_PER_CHILD) {
				return false;
			}
			node.children.reserve(children);
			for (u32 child = 0; child < children; ++child) {
				MotionChild motion;
				descriptor.Read(motion.node);
				descriptor.Read(motion.position);
				descriptor.Read(motion.speed);
				motion.parameter = str(descriptor.ReadString());
				node.children.push_back(std::move(motion));
			}
			readNodes.push_back(std::move(node));
		}

		descriptor.Read(count);
		constexpr size_t BYTES_PER_MACHINE = sizeof(u32) + sizeof(i32) * 2 + sizeof(Vec2) * 3 + sizeof(u32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_MACHINE) {
			return false;
		}
		readMachines.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			StateMachine machine;
			machine.name = str(descriptor.ReadString());
			descriptor.Read(machine.parent);
			descriptor.Read(machine.defaultState);
			descriptor.Read(machine.position);
			descriptor.Read(machine.entryPosition);
			descriptor.Read(machine.anyStatePosition);
			descriptor.Read(machine.upPosition);
			u32 any = 0;
			descriptor.Read(any);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(any) * BYTES_PER_TRANSITION) {
				return false;
			}
			machine.anyState.reserve(any);
			for (u32 entry = 0; entry < any; ++entry) {
				Transition transition;
				if (!ReadTransition(descriptor, transition)) {
					return false;
				}
				machine.anyState.push_back(std::move(transition));
			}
			readMachines.push_back(std::move(machine));
		}

		descriptor.Read(count);
		constexpr size_t BYTES_PER_STATE = sizeof(u32) + sizeof(i32) * 2 + sizeof(f32) + sizeof(Vec2) + sizeof(u32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_STATE) {
			return false;
		}
		readStates.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			State state;
			state.name = str(descriptor.ReadString());
			descriptor.Read(state.motion);
			descriptor.Read(state.speed);
			descriptor.Read(state.machine);
			descriptor.Read(state.position);
			u32 transitions = 0;
			descriptor.Read(transitions);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(transitions) * BYTES_PER_TRANSITION) {
				return false;
			}
			state.transitions.reserve(transitions);
			for (u32 entry = 0; entry < transitions; ++entry) {
				Transition transition;
				if (!ReadTransition(descriptor, transition)) {
					return false;
				}
				state.transitions.push_back(std::move(transition));
			}
			readStates.push_back(std::move(state));
		}

		descriptor.Read(count);
		constexpr size_t BYTES_PER_LAYER = sizeof(u32) + sizeof(i32) + sizeof(f32) + sizeof(u8) + sizeof(u32);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_LAYER) {
			return false;
		}
		readLayers.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			Layer layer;
			layer.name = str(descriptor.ReadString());
			descriptor.Read(layer.machine);
			descriptor.Read(layer.weight);
			u8 blend = 0;
			descriptor.Read(blend);
			layer.blend = static_cast<AnimationBlendMode>(blend);
			u32 mask = 0;
			descriptor.Read(mask);
			constexpr size_t BYTES_PER_MASK_ENTRY = sizeof(u32) + sizeof(f32);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(mask) * BYTES_PER_MASK_ENTRY) {
				return false;
			}
			layer.mask.reserve(mask);
			for (u32 entry = 0; entry < mask; ++entry) {
				Layer::MaskEntry maskEntry;
				maskEntry.bone = str(descriptor.ReadString());
				descriptor.Read(maskEntry.weight);
				layer.mask.push_back(std::move(maskEntry));
			}
			readLayers.push_back(std::move(layer));
		}

		if (descriptor.Failed()) {
			return false;
		}
		parameters = std::move(readParameters);
		nodes = std::move(readNodes);
		states = std::move(readStates);
		machines = std::move(readMachines);
		layers = std::move(readLayers);
		return true;
	}

	namespace {

		class AnimatorControllerImporter : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return StringHash("AnimatorController"); }

			// bump when Cook output changes
			u32 GetImporterVersion() const override { return 3; }

			Asset* Import(const Path& sourcePath) override {
				auto* controller = New<AnimatorController>(MemTag::Asset);
				if (!controller->Import(sourcePath)) {
					Delete(controller);
					return nullptr;
				}
				return controller;
			}

			bool CanCreateAsset() const override { return true; }
			std::string_view GetCreateMenuLabel() const override { return "Animator Controller"; }
			std::string_view GetCreateDefaultFileName() const override { return "New Animator Controller.controller"; }

			Asset* CreateAsset() override {
				auto* controller = New<AnimatorController>(MemTag::Asset);
				AnimatorController::StateMachine root;
				root.name = "Base Layer";
				controller->machines.push_back(std::move(root));
				AnimatorController::Layer base;
				base.name = "Base Layer";
				base.machine = 0;
				controller->layers.push_back(std::move(base));
				return controller;
			}
		};

		AnimatorControllerImporter _importer;

	} // anonymous namespace

	void RegisterAnimatorControllerImporter(const void* owner) {
		AssetImporterRegistry::Register(StringHash("AnimatorController"), &_importer, {".controller"}, owner);
	}

} // namespace Shalltear
