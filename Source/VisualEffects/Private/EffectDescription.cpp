#include "EffectDescription.h"

#include "Shalltear/Core/Asset/PakFormat.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		constexpr std::string_view TIME_INPUT = "TimeParameters";
		constexpr std::string_view SMOOTH_DELTA_INPUT = "SmoothDeltaTime";
		constexpr std::string_view RANDOM_INPUTS[] = {"RandomKey/Particle", "RandomKey/Effect"};
		constexpr std::string_view CONSTANT_RANDOM_INPUTS[] = {"RandomKey/ParticleConstant", "RandomKey/EffectConstant"};
		constexpr std::string_view PARTICLE_RANDOM_INPUTS[] = {"RandomKey/Particle", "RandomKey/ParticleConstant", "RandomKey/Effect",
															   "RandomKey/EffectConstant"};
		constexpr std::string_view ROW_INPUTS[] = {"Space/Foreign/X", "Space/Foreign/Y", "Space/Foreign/Z"};
		constexpr std::string_view ATTRIBUTE_PREFIX = "Attribute/";
		constexpr std::string_view SOURCE_PREFIX = "SourceAttribute/";
		constexpr std::string_view PARTICLE_ID = "particleId";
		constexpr std::string_view SPAWN_INDEX = "spawnIndex";
		constexpr std::string_view SPAWN_COUNT = "spawnCount";

		void WriteStrings(CookWriter& out, const Vector<str>& strings) {
			out.Write(static_cast<u32>(strings.size()));
			for (const str& text : strings) {
				out.WriteString(text);
			}
		}

		void ReadStrings(CookReader& in, Vector<str>& out) {
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				out.push_back(str(in.ReadString()));
			}
		}

		void WriteKernelTextures(CookWriter& out, const Vector<EffectKernelTexture>& textures) {
			out.Write(static_cast<u32>(textures.size()));
			for (const EffectKernelTexture& texture : textures) {
				out.Write(texture.binding);
				out.Write(texture.samplerBinding);
				out.WriteAssetRef(texture.texture);
			}
		}

		void ReadKernelTextures(CookReader& in, Vector<EffectKernelTexture>& out) {
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectKernelTexture texture;
				in.Read(texture.binding);
				in.Read(texture.samplerBinding);
				in.Read(texture.texture);
				out.push_back(texture);
			}
		}

		template <typename Visit>
		void ForEachProgram(EffectDescription& description, Visit&& visit) {
			for (EffectSpawner& spawner : description.spawners) {
				for (EffectSpawnBlock& block : spawner.blocks) {
					visit(block.program);
				}
				visit(spawner.loop);
			}
			for (EffectSystem& system : description.systems) {
				visit(system.bounds);
			}
		}

		void WriteEventTargets(CookWriter& out, const Vector<EffectEventTarget>& targets) {
			out.Write(static_cast<u32>(targets.size()));
			for (const EffectEventTarget& target : targets) {
				out.Write(target.child);
				out.Write(target.lists);
				out.Write(target.arguments);
			}
		}

		void ReadEventTargets(CookReader& in, Vector<EffectEventTarget>& out) {
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectEventTarget target;
				in.Read(target.child);
				in.Read(target.lists);
				in.Read(target.arguments);
				out.push_back(target);
			}
		}

		void WriteOutputEvents(CookWriter& out, const Vector<EffectOutputEvent>& events) {
			out.Write(static_cast<u32>(events.size()));
			for (const EffectOutputEvent& event : events) {
				out.WriteString(event.name);
				out.Write(event.root);
			}
		}

		void ReadOutputEvents(CookReader& in, Vector<EffectOutputEvent>& out) {
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectOutputEvent event;
				event.name = str(in.ReadString());
				in.Read(event.root);
				out.push_back(std::move(event));
			}
		}

		void WriteCarried(CookWriter& out, const Vector<EffectCarried>& carried) {
			out.Write(static_cast<u32>(carried.size()));
			for (const EffectCarried& entry : carried) {
				out.WriteString(entry.attribute);
				out.Write(entry.root);
				out.Write(entry.components);
			}
		}

		void ReadCarried(CookReader& in, Vector<EffectCarried>& out) {
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectCarried entry;
				entry.attribute = str(in.ReadString());
				in.Read(entry.root);
				in.Read(entry.components);
				out.push_back(std::move(entry));
			}
		}

		bool EffectSlotOf(const EffectDescription& description, std::string_view name, u32& slot, u32& width) {
			if (name == TIME_INPUT) {
				slot = EFFECT_SLOT_TIME;
				width = 4;
				return true;
			}
			if (name == SMOOTH_DELTA_INPUT) {
				slot = EFFECT_SLOT_SMOOTH_DELTA;
				width = 1;
				return true;
			}
			bool found = false;
			for (u32 i = 0; i < description.properties.size(); ++i) {
				if (description.properties[i].reference == name) {
					slot = EFFECT_SLOT_PROPERTIES + i;
					width = description.properties[i].components;
					found = true;
				}
			}
			for (u32 i = 0; i < description.meshes.size(); ++i) {
				const str& binding = description.meshes[i].binding;
				for (const bool indices : {false, true}) {
					if (name == binding + str(indices ? EFFECT_MESH_INDEX_COUNT_INPUT : EFFECT_MESH_VERTEX_COUNT_INPUT)) {
						slot = EffectMeshSlot(description, i, indices);
						width = 1;
						found = true;
					}
				}
			}
			return found;
		}

		bool ParticleSlotOf(const EffectDescription& description, const EffectSystem& system, bool initialize, std::string_view name, u32& slot,
							u32& width) {
			if (EffectSlotOf(description, name, slot, width)) {
				return true;
			}
			const auto at = [&](u32 particle, u32 wide) {
				slot = EffectParticleSlot(description, particle);
				width = wide;
				return true;
			};
			for (u32 i = 0; i < std::size(PARTICLE_RANDOM_INPUTS); ++i) {
				if (name == PARTICLE_RANDOM_INPUTS[i]) {
					return at(EFFECT_PARTICLE_SLOT_RANDOM + i, 1);
				}
			}
			for (u32 i = 0; i < std::size(ROW_INPUTS); ++i) {
				if (name == ROW_INPUTS[i]) {
					return at(EFFECT_PARTICLE_SLOT_ROWS + i, 4);
				}
			}
			if (name.starts_with(ATTRIBUTE_PREFIX)) {
				const std::string_view attribute = name.substr(ATTRIBUTE_PREFIX.size());
				if (attribute == PARTICLE_ID) {
					return at(EFFECT_PARTICLE_SLOT_ID, 1);
				}
				if (initialize) {
					return attribute == SPAWN_INDEX && at(EFFECT_PARTICLE_SLOT_SPAWN_INDEX, 1);
				}
				for (u32 i = 0; i < system.stored.size(); ++i) {
					if (system.stored[i].attribute == attribute) {
						return at(EFFECT_PARTICLE_SLOT_STORED + i, system.stored[i].components);
					}
				}
				return false;
			}
			if (initialize && name.starts_with(SOURCE_PREFIX)) {
				const std::string_view attribute = name.substr(SOURCE_PREFIX.size());
				if (attribute == SPAWN_COUNT) {
					return at(EFFECT_PARTICLE_SLOT_SPAWN_COUNT, 1);
				}
				for (u32 i = 0; i < system.source.size(); ++i) {
					if (system.source[i].name == attribute) {
						return at(EFFECT_PARTICLE_SLOT_STORED + static_cast<u32>(system.stored.size()) + i, system.source[i].components);
					}
				}
			}
			return false;
		}

		bool CpuSystemFits(const EffectSystem& system, str& error) {
			if (!system.cpu) {
				if (!system.initializeProgram.IsEmpty() || !system.updateProgram.IsEmpty() || !system.initializeOutputEvents.empty() ||
					!system.updateOutputEvents.empty() || !system.initializeCarried.empty() || !system.updateCarried.empty() ||
					system.initializeBoundsRoot != EFFECT_NO_BINDING || system.updateBoundsRoot != EFFECT_NO_BINDING) {
					error = "a system simulated on the GPU has CPU programs";
					return false;
				}
				return true;
			}
			if (system.strips > 0 || system.parent != EFFECT_NO_BINDING || !system.initializeEvents.empty() || !system.updateEvents.empty() ||
				!system.initializeTextures.empty() || !system.updateTextures.empty() || system.initializeDepthBinding != EFFECT_NO_BINDING ||
				system.updateDepthBinding != EFFECT_NO_BINDING) {
				error = "a system the CPU simulates holds what only a kernel runs";
				return false;
			}
			for (const bool initialize : {true, false}) {
				const EffectProgram* program = initialize ? &system.initializeProgram : &system.updateProgram;
				if (!EffectPrograms::Validate(*program, error)) {
					return false;
				}
				const auto rootIs = [program](u32 root, u32 width) { return root < program->roots.size() && program->code[program->roots[root]].width == width; };
				bool answers = program->roots.size() >= system.stored.size() + 1 && rootIs(0, 1);
				for (u32 i = 0; answers && i < system.stored.size(); ++i) {
					answers = rootIs(i + 1, system.stored[i].components);
				}
				for (const EffectOutputEvent& event : initialize ? system.initializeOutputEvents : system.updateOutputEvents) {
					answers = answers && rootIs(event.root, 1);
				}
				for (const EffectCarried& carried : initialize ? system.initializeCarried : system.updateCarried) {
					answers = answers && carried.components >= 1 && carried.components <= 4 && rootIs(carried.root, carried.components);
				}
				const u32 sphere = initialize ? system.initializeBoundsRoot : system.updateBoundsRoot;
				answers = answers && (sphere == EFFECT_NO_BINDING ? !system.measuresBounds : system.measuresBounds && rootIs(sphere, 3) && rootIs(sphere + 1, 1));
				if (!answers) {
					error = "a CPU system's program doesn't answer whether its particle lives, each attribute it stores, what it sends and its sphere";
					return false;
				}
			}
			return true;
		}

		bool Answers(const EffectProgram& program, u32 roots, u32 width) {
			if (program.roots.size() != roots) {
				return false;
			}
			for (u32 root : program.roots) {
				if (program.code[root].width != width) {
					return false;
				}
			}
			return true;
		}

	} // anonymous namespace

	namespace EffectDescriptions {

		bool Bind(EffectDescription& description, str& error) {
			bool ok = true;
			const auto bound = [&](EffectInput& input, bool found, u32 slot, u32 width) {
				if (!found || width != input.width) {
					if (ok) {
						error = std::format("a program reads '{}' {} wide (no value of the effect is)", input.name, input.width);
					}
					ok = false;
					return;
				}
				input.slot = slot;
			};
			ForEachProgram(description, [&](EffectProgram& program) {
				for (EffectInput& input : program.inputs) {
					u32 slot = 0;
					u32 width = 0;
					bool found = true;
					if (std::find(std::begin(RANDOM_INPUTS), std::end(RANDOM_INPUTS), input.name) != std::end(RANDOM_INPUTS)) {
						slot = EFFECT_SLOT_RANDOM;
						width = 1;
					} else if (std::find(std::begin(CONSTANT_RANDOM_INPUTS), std::end(CONSTANT_RANDOM_INPUTS), input.name) != std::end(CONSTANT_RANDOM_INPUTS)) {
						slot = EFFECT_SLOT_CONSTANT_RANDOM;
						width = 1;
					} else {
						found = EffectSlotOf(description, input.name, slot, width);
					}
					bound(input, found, slot, width);
				}
			});
			for (EffectSystem& system : description.systems) {
				for (const bool initialize : {true, false}) {
					EffectProgram& program = initialize ? system.initializeProgram : system.updateProgram;
					for (EffectInput& input : program.inputs) {
						u32 slot = 0;
						u32 width = 0;
						const bool found = ParticleSlotOf(description, system, initialize, input.name, slot, width);
						bound(input, found, slot, width);
					}
				}
			}
			const auto meshes = [&](EffectProgram& program) {
				for (EffectMeshInput& mesh : program.meshes) {
					const auto found = std::find_if(description.meshes.begin(), description.meshes.end(),
													[&](const EffectMesh& held) { return held.binding == mesh.name; });
					if (found == description.meshes.end()) {
						if (ok) {
							error = std::format("a program reads mesh '{}' (no port of the effect holds it)", mesh.name);
						}
						ok = false;
						continue;
					}
					mesh.mesh = static_cast<u32>(found - description.meshes.begin());
				}
			};
			ForEachProgram(description, meshes);
			for (EffectSystem& system : description.systems) {
				meshes(system.initializeProgram);
				meshes(system.updateProgram);
			}
			return ok;
		}

		bool Validate(const EffectDescription& description, u32 shaderCount, str& error) {
			auto refuse = [&](str why) {
				error = std::move(why);
				return false;
			};
			if (!(description.timing.prewarmDeltaTime >= 0.0f) || !std::isfinite(description.timing.prewarmDeltaTime)) {
				return refuse("the prewarm's delta time isn't a time");
			}
			for (const EffectProperty& property : description.properties) {
				if (property.components < 1 || property.components > 4 || property.offset + 4 * property.components > description.batchesOffset) {
					return refuse("property '" + property.reference + "' doesn't fit the frame block");
				}
			}
			if (description.cameraOffset != 0 && description.cameraOffset + EFFECT_CAMERA_RECORD_BYTES > description.batchesOffset) {
				return refuse("the camera's record doesn't fit the frame block");
			}
			for (const EffectMesh& mesh : description.meshes) {
				if (mesh.offset < 96 || mesh.offset % 8 != 0 || mesh.offset + EFFECT_MESH_RECORD_BYTES > description.batchesOffset) {
					return refuse("the record of mesh '" + mesh.binding + "' doesn't fit the frame block");
				}
			}
			for (const EffectSpawner& spawner : description.spawners) {
				for (const EffectSpawnBlock& block : spawner.blocks) {
					if (!EffectPrograms::Validate(block.program, error)) {
						return false;
					}
					const bool single = block.kind == EffectSpawnKind::ConstantRate || block.kind == EffectSpawnKind::IncrementStripIndex;
					const bool attribute = block.kind == EffectSpawnKind::SetAttribute;
					const bool answers = attribute ? block.components >= 1 && block.components <= 4 && Answers(block.program, 1, block.components)
												   : Answers(block.program, single ? 1 : 2, 1);
					if (block.kind > EffectSpawnKind::IncrementStripIndex || !answers) {
						return refuse("a spawn block's program doesn't answer what its kind reads");
					}
				}
				if (!EffectPrograms::Validate(spawner.loop, error)) {
					return false;
				}
				if (!spawner.loop.IsEmpty() && !Answers(spawner.loop, 4, 1)) {
					return refuse("a spawner's loop isn't a duration, a count and 2 delays");
				}
			}
			for (const EffectSystem& system : description.systems) {
				const bool kernels = system.cpu ? system.initialize == EFFECT_NO_BINDING && system.update == EFFECT_NO_BINDING
												: system.initialize < shaderCount && system.update < shaderCount;
				if (system.capacity == 0 || system.attributeCount == 0 || !kernels) {
					return refuse("a system uses a kernel the asset doesn't have, or is empty");
				}
				if (!CpuSystemFits(system, error)) {
					return false;
				}
				if (system.strips > 0 && system.capacity % system.strips != 0) {
					return refuse("a strip system's capacity isn't its strips' particles times their count");
				}
				for (const EffectStored& stored : system.stored) {
					if (stored.slot + stored.components > system.attributeCount) {
						return refuse("attribute '" + stored.attribute + "' is stored past the system's floats");
					}
				}
				for (const EffectField& field : system.source) {
					if (field.offset < 4 || field.offset + 4 * field.components > system.batchStride) {
						return refuse("source attribute '" + field.name + "' doesn't fit its batch");
					}
				}
				for (const EffectOutput& output : system.outputs) {
					if (output.shader >= shaderCount) {
						return refuse("an output uses a shader the asset doesn't have");
					}
					if (output.mesh != EFFECT_NO_BINDING ? output.mesh >= description.meshes.size() || !output.instanced : output.vertices == 0) {
						return refuse("an output draws nothing per particle, or a mesh the asset hasn't");
					}
					if (output.strip && (system.strips == 0 || output.instanced)) {
						return refuse("a strip output draws a system that holds no strips");
					}
					const bool sorted = output.sort != EffectSort::None;
					if (output.sort > EffectSort::Custom || sorted != (output.sortKernel != EFFECT_NO_BINDING) ||
						(sorted && output.sortKernel >= shaderCount)) {
						return refuse("an output sorts by no mode, or without a kernel the asset has");
					}
				}
				if ((system.initializeDepthBinding != EFFECT_NO_BINDING || system.updateDepthBinding != EFFECT_NO_BINDING) && description.cameraOffset == 0) {
					return refuse("a kernel reads the camera's depth (the frame block holds no record of it)");
				}
				for (u32 spawner : system.spawners) {
					if (spawner >= description.spawners.size()) {
						return refuse("a system uses a spawner the effect doesn't have");
					}
				}
				if (!EffectPrograms::Validate(system.bounds, error)) {
					return false;
				}
				if (!Answers(system.bounds, 3, 3)) {
					return refuse("a system's bounds aren't a center, a size and a padding");
				}
				if (system.boundsMode > EffectBoundsMode::Automatic ||
					(system.boundsMode == EffectBoundsMode::Automatic && (!system.measuresBounds || description.timing.cullSimulation))) {
					return refuse("a system's bounds are Automatic + not measured, or culled with its simulation");
				}
			}
			for (u32 s = 0; s < description.systems.size(); ++s) {
				const EffectSystem& system = description.systems[s];
				if (system.parent != EFFECT_NO_BINDING &&
					(system.parent >= s || system.parentBinding == EFFECT_NO_BINDING || !system.spawners.empty() || !system.events.empty() ||
					 description.systems[system.parent].cpu)) {
					return refuse("a system a GPU event feeds has no parent before it simulated on the GPU, or is fed by spawners or events too");
				}
				for (const Vector<EffectEventTarget>* targets : {&system.initializeEvents, &system.updateEvents}) {
					for (const EffectEventTarget& target : *targets) {
						if (target.child <= s || target.child >= description.systems.size() || description.systems[target.child].parent != s) {
							return refuse("a system appends GPU events to one that isn't its child");
						}
					}
				}
			}
			return true;
		}

		void Cook(const EffectDescription& description, CookWriter& out) {
			out.Write(static_cast<u32>(description.properties.size()));
			for (const EffectProperty& property : description.properties) {
				out.WriteString(property.name);
				out.WriteString(property.reference);
				out.Write(static_cast<u8>(property.type));
				out.Write(property.offset);
				out.Write(property.components);
				out.WriteBytes(property.defaultValue, sizeof(property.defaultValue));
			}
			out.Write(description.cameraOffset);
			out.Write(static_cast<u32>(description.meshes.size()));
			for (const EffectMesh& mesh : description.meshes) {
				out.WriteString(mesh.binding);
				out.Write(mesh.offset);
				out.WriteAssetRef(mesh.mesh);
			}
			out.Write(description.batchesOffset);
			out.Write(static_cast<u32>(description.spawners.size()));
			for (const EffectSpawner& spawner : description.spawners) {
				out.Write(static_cast<u32>(spawner.blocks.size()));
				for (const EffectSpawnBlock& block : spawner.blocks) {
					out.Write(static_cast<u8>(block.kind));
					EffectPrograms::Cook(block.program, out);
					out.WriteString(block.attribute);
					out.Write(block.components);
				}
				EffectPrograms::Cook(spawner.loop, out);
				WriteStrings(out, spawner.start);
				WriteStrings(out, spawner.stop);
				WriteStrings(out, spawner.outputEvents);
			}
			out.Write(static_cast<u32>(description.systems.size()));
			for (const EffectSystem& system : description.systems) {
				out.WriteString(system.name);
				out.Write(system.capacity);
				out.Write(system.strips);
				out.Write(static_cast<u8>(system.world ? 1 : 0));
				out.Write(system.attributeCount);
				out.Write(static_cast<u32>(system.stored.size()));
				for (const EffectStored& stored : system.stored) {
					out.WriteString(stored.attribute);
					out.Write(stored.slot);
					out.Write(stored.components);
				}
				out.Write(static_cast<u32>(system.source.size()));
				for (const EffectField& field : system.source) {
					out.WriteString(field.name);
					out.Write(field.offset);
					out.Write(field.components);
					out.WriteBytes(field.defaultValue, sizeof(field.defaultValue));
				}
				out.Write(system.batchStride);
				out.Write(system.initialize);
				out.Write(system.update);
				WriteKernelTextures(out, system.initializeTextures);
				WriteKernelTextures(out, system.updateTextures);
				out.Write(system.initializeDepthBinding);
				out.Write(system.updateDepthBinding);
				out.Write(static_cast<u32>(system.outputs.size()));
				for (const EffectOutput& output : system.outputs) {
					out.Write(output.shader);
					out.Write(output.vertices);
					out.Write(static_cast<u32>(output.instanced ? 1u : 0u));
					out.Write(output.mesh);
					out.Write(static_cast<u32>(output.textures.size()));
					for (const EffectOutputTexture& texture : output.textures) {
						out.WriteString(texture.slot);
						out.WriteAssetRef(texture.texture);
					}
					out.Write(static_cast<u32>(output.sort));
					out.Write(static_cast<u32>(output.revertSorting ? 1u : 0u));
					out.Write(output.sortKernel);
					out.Write(static_cast<u32>(output.castShadows ? 1u : 0u));
					out.Write(static_cast<u32>(output.strip ? 1u : 0u));
				}
				out.Write(static_cast<u32>(system.spawners.size()));
				out.WriteBytes(system.spawners.data(), system.spawners.size() * sizeof(u32));
				WriteStrings(out, system.events);
				EffectPrograms::Cook(system.bounds, out);
				out.Write(static_cast<u32>(system.boundsWorld ? 1u : 0u));
				out.Write(static_cast<u8>(system.boundsMode));
				out.Write(static_cast<u8>(system.measuresBounds ? 1u : 0u));
				out.Write(system.initializeBoundsRoot);
				out.Write(system.updateBoundsRoot);
				out.Write(system.parent);
				out.Write(system.parentBinding);
				WriteEventTargets(out, system.initializeEvents);
				WriteEventTargets(out, system.updateEvents);
				out.Write(static_cast<u32>(system.cpu ? 1u : 0u));
				EffectPrograms::Cook(system.initializeProgram, out);
				EffectPrograms::Cook(system.updateProgram, out);
				WriteOutputEvents(out, system.initializeOutputEvents);
				WriteOutputEvents(out, system.updateOutputEvents);
				WriteCarried(out, system.initializeCarried);
				WriteCarried(out, system.updateCarried);
			}
			WriteStrings(out, description.events);
			out.Write(static_cast<u32>(description.relays.size()));
			for (const EffectRelay& relay : description.relays) {
				out.WriteString(relay.event);
				out.WriteString(relay.output);
			}
			const EffectTiming& timing = description.timing;
			out.Write(static_cast<u32>((timing.fixedDeltaTime ? 1u : 0u) | (timing.exactFixedTime ? 2u : 0u) | (timing.ignoreTimeScale ? 4u : 0u)));
			out.Write(timing.prewarmStepCount);
			out.Write(timing.prewarmDeltaTime);
			out.Write(static_cast<u32>((timing.cullSimulation ? 1u : 0u) | (timing.cullBoundsUpdate ? 2u : 0u)));
			out.Write(description.instancing.mode);
			out.Write(description.instancing.capacity);
		}

		bool LoadCooked(CookReader& in, EffectDescription& out) {
			EffectDescription read;
			u32 count = 0;
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectProperty property;
				property.name = str(in.ReadString());
				property.reference = str(in.ReadString());
				u8 type = 0;
				in.Read(type);
				property.type = static_cast<EffectPropertyType>(type);
				in.Read(property.offset);
				in.Read(property.components);
				in.ReadBytes(property.defaultValue, sizeof(property.defaultValue));
				if (type > static_cast<u8>(EffectPropertyType::Color)) {
					return false;
				}
				read.properties.push_back(std::move(property));
			}
			in.Read(read.cameraOffset);
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectMesh mesh;
				mesh.binding = str(in.ReadString());
				in.Read(mesh.offset);
				in.Read(mesh.mesh);
				read.meshes.push_back(std::move(mesh));
			}
			in.Read(read.batchesOffset);
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectSpawner spawner;
				u32 blocks = 0;
				in.Read(blocks);
				for (u32 b = 0; b < blocks && !in.Failed(); ++b) {
					EffectSpawnBlock block;
					u8 kind = 0;
					in.Read(kind);
					block.kind = static_cast<EffectSpawnKind>(kind);
					if (!EffectPrograms::LoadCooked(in, block.program)) {
						return false;
					}
					block.attribute = str(in.ReadString());
					in.Read(block.components);
					spawner.blocks.push_back(std::move(block));
				}
				if (!EffectPrograms::LoadCooked(in, spawner.loop)) {
					return false;
				}
				ReadStrings(in, spawner.start);
				ReadStrings(in, spawner.stop);
				ReadStrings(in, spawner.outputEvents);
				read.spawners.push_back(std::move(spawner));
			}
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectSystem system;
				system.name = str(in.ReadString());
				in.Read(system.capacity);
				in.Read(system.strips);
				u8 world = 0;
				in.Read(world);
				system.world = world != 0;
				in.Read(system.attributeCount);
				u32 entries = 0;
				in.Read(entries);
				for (u32 e = 0; e < entries && !in.Failed(); ++e) {
					EffectStored stored;
					stored.attribute = str(in.ReadString());
					in.Read(stored.slot);
					in.Read(stored.components);
					system.stored.push_back(std::move(stored));
				}
				in.Read(entries);
				for (u32 e = 0; e < entries && !in.Failed(); ++e) {
					EffectField field;
					field.name = str(in.ReadString());
					in.Read(field.offset);
					in.Read(field.components);
					in.ReadBytes(field.defaultValue, sizeof(field.defaultValue));
					system.source.push_back(std::move(field));
				}
				in.Read(system.batchStride);
				in.Read(system.initialize);
				in.Read(system.update);
				ReadKernelTextures(in, system.initializeTextures);
				ReadKernelTextures(in, system.updateTextures);
				in.Read(system.initializeDepthBinding);
				in.Read(system.updateDepthBinding);
				in.Read(entries);
				for (u32 e = 0; e < entries && !in.Failed(); ++e) {
					EffectOutput output;
					in.Read(output.shader);
					in.Read(output.vertices);
					u32 instanced = 0;
					in.Read(instanced);
					output.instanced = instanced != 0;
					in.Read(output.mesh);
					u32 textures = 0;
					in.Read(textures);
					for (u32 t = 0; t < textures && !in.Failed(); ++t) {
						EffectOutputTexture texture;
						texture.slot = str(in.ReadString());
						in.Read(texture.texture);
						output.textures.push_back(std::move(texture));
					}
					u32 sort = 0;
					u32 revert = 0;
					in.Read(sort);
					in.Read(revert);
					in.Read(output.sortKernel);
					u32 casts = 0;
					in.Read(casts);
					u32 strip = 0;
					in.Read(strip);
					output.sort = static_cast<EffectSort>(std::min<u32>(sort, 255u));
					output.revertSorting = revert != 0;
					output.castShadows = casts != 0;
					output.strip = strip != 0;
					system.outputs.push_back(std::move(output));
				}
				in.Read(entries);
				in.ReadInto(system.spawners, entries);
				ReadStrings(in, system.events);
				if (!EffectPrograms::LoadCooked(in, system.bounds)) {
					return false;
				}
				u32 boundsWorld = 0;
				in.Read(boundsWorld);
				system.boundsWorld = boundsWorld != 0;
				u8 boundsMode = 0;
				u8 measures = 0;
				in.Read(boundsMode);
				in.Read(measures);
				system.boundsMode = static_cast<EffectBoundsMode>(boundsMode);
				system.measuresBounds = measures != 0;
				in.Read(system.initializeBoundsRoot);
				in.Read(system.updateBoundsRoot);
				in.Read(system.parent);
				in.Read(system.parentBinding);
				ReadEventTargets(in, system.initializeEvents);
				ReadEventTargets(in, system.updateEvents);
				u32 cpu = 0;
				in.Read(cpu);
				system.cpu = cpu != 0;
				if (!EffectPrograms::LoadCooked(in, system.initializeProgram) || !EffectPrograms::LoadCooked(in, system.updateProgram)) {
					return false;
				}
				ReadOutputEvents(in, system.initializeOutputEvents);
				ReadOutputEvents(in, system.updateOutputEvents);
				ReadCarried(in, system.initializeCarried);
				ReadCarried(in, system.updateCarried);
				read.systems.push_back(std::move(system));
			}
			ReadStrings(in, read.events);
			u32 relays = 0;
			in.Read(relays);
			for (u32 i = 0; i < relays && !in.Failed(); ++i) {
				EffectRelay relay;
				relay.event = str(in.ReadString());
				relay.output = str(in.ReadString());
				read.relays.push_back(std::move(relay));
			}
			u32 mode = 0;
			in.Read(mode);
			read.timing.fixedDeltaTime = (mode & 1u) != 0;
			read.timing.exactFixedTime = (mode & 2u) != 0;
			read.timing.ignoreTimeScale = (mode & 4u) != 0;
			in.Read(read.timing.prewarmStepCount);
			in.Read(read.timing.prewarmDeltaTime);
			u32 culling = 0;
			in.Read(culling);
			read.timing.cullSimulation = (culling & 1u) != 0;
			read.timing.cullBoundsUpdate = (culling & 2u) != 0;
			in.Read(read.instancing.mode);
			in.Read(read.instancing.capacity);
			if (in.Failed()) {
				return false;
			}
			out = std::move(read);
			return true;
		}

	} // namespace EffectDescriptions

	namespace EffectSpawners {

		namespace {

			void BeginLoop(const EffectSpawner& spawner, EffectSpawnerState& state, std::span<const EffectValue> slots, EffectMachine& machine,
						   std::span<const EffectMeshView> meshes) {
				state.loopDuration = -1.0f;
				state.delayBeforeLoop = 0.0f;
				state.delayAfterLoop = 0.0f;
				if (!spawner.loop.IsEmpty() && machine.Run(spawner.loop, slots, meshes)) {
					const f32 duration = machine.Root(spawner.loop, 0).v[0];
					state.loopDuration = duration < 0.0f ? -1.0f : duration;
					state.delayBeforeLoop = std::max(0.0f, machine.Root(spawner.loop, 2).v[0]);
					state.delayAfterLoop = std::max(0.0f, machine.Root(spawner.loop, 3).v[0]);
				}
				state.phase = EffectLoopPhase::DelayingBeforeLoop;
				state.phaseTime = 0.0f;
				state.time = 0.0f;
				state.fired.assign(spawner.blocks.size(), 0);
				state.next.assign(spawner.blocks.size(), -1.0f);
			}

			f32 RunBlocks(const EffectSpawner& spawner, EffectSpawnerState& state, f32 span, std::span<const EffectValue> slots, EffectMachine& machine,
						  std::span<const EffectMeshView> meshes) {
				const f32 end = state.time + span;
				f32 spawned = 0.0f;
				for (size_t i = 0; i < spawner.blocks.size(); ++i) {
					const EffectSpawnBlock& block = spawner.blocks[i];
					if (!machine.Run(block.program, slots, meshes)) {
						continue;
					}
					if (block.kind == EffectSpawnKind::SetAttribute) {
						state.values[i] = machine.Root(block.program, 0);
						continue;
					}
					if (block.kind == EffectSpawnKind::IncrementStripIndex) {
						continue;
					}
					const f32 first = std::max(0.0f, machine.Root(block.program, 0).v[0]);
					switch (block.kind) {
					case EffectSpawnKind::ConstantRate:
						spawned += first * span;
						break;
					case EffectSpawnKind::SingleBurst: {
						const f32 delay = std::max(0.0f, machine.Root(block.program, 1).v[0]);
						if (state.fired[i] == 0 && end >= delay) {
							spawned += first;
							state.fired[i] = 1;
						}
						break;
					}
					case EffectSpawnKind::PeriodicBurst: {
						const f32 delay = std::max(0.0f, machine.Root(block.program, 1).v[0]);
						if (delay <= 0.0f) {
							spawned += first;
							break;
						}
						if (state.next[i] < 0.0f) {
							state.next[i] = delay;
						}
						while (end >= state.next[i]) {
							spawned += first;
							state.next[i] += delay;
						}
						break;
					}
					default:
						break;
					}
				}
				state.time = end;
				return spawned;
			}

			constexpr u32 MAX_PHASES_PER_FRAME = 4096;

		} // anonymous namespace

		void Start(const EffectSpawner& spawner, EffectSpawnerState& state, std::span<const EffectValue> slots, EffectMachine& machine,
				   std::span<const EffectMeshView> meshes) {
			state.playing = true;
			state.carry = 0.0f;
			state.loopIndex = 0;
			state.loopCount = -1.0f;
			state.values.assign(spawner.blocks.size(), EffectValue{});
			if (!spawner.loop.IsEmpty() && machine.Run(spawner.loop, slots, meshes)) {
				const f32 count = machine.Root(spawner.loop, 1).v[0];
				state.loopCount = count < 0.0f ? -1.0f : std::floor(count);
			}
			state.stripIndices.resize(spawner.blocks.size(), 0.0f);
			for (size_t i = 0; i < spawner.blocks.size(); ++i) {
				const EffectSpawnBlock& block = spawner.blocks[i];
				if (block.kind != EffectSpawnKind::IncrementStripIndex || !machine.Run(block.program, slots, meshes)) {
					continue;
				}
				const f32 count = std::max(1.0f, std::floor(std::max(0.0f, machine.Root(block.program, 0).v[0])));
				state.stripIndices[i] = std::fmod(state.stripIndices[i] + 1.0f, count);
				state.values[i] = EffectValue{{state.stripIndices[i], 0.0f, 0.0f, 0.0f}};
			}
			BeginLoop(spawner, state, slots, machine, meshes);
		}

		void Stop(EffectSpawnerState& state) {
			state.playing = false;
			state.phase = EffectLoopPhase::Finished;
			state.carry = 0.0f;
			std::fill(state.stripIndices.begin(), state.stripIndices.end(), 0.0f);
		}

		u32 Advance(const EffectSpawner& spawner, EffectSpawnerState& state, f32 deltaTime, std::span<const EffectValue> slots, EffectMachine& machine,
					std::span<const EffectMeshView> meshes) {
			if (!state.playing) {
				return 0;
			}
			if (state.fired.size() != spawner.blocks.size() || state.values.size() != spawner.blocks.size()) {
				state.fired.assign(spawner.blocks.size(), 0);
				state.next.assign(spawner.blocks.size(), -1.0f);
				state.values.assign(spawner.blocks.size(), EffectValue{});
			}
			if (state.loopCount == 0.0f) {
				Stop(state);
				return 0;
			}
			f32 remaining = std::max(0.0f, deltaTime);
			f32 spawned = 0.0f;
			bool looped = false;
			for (u32 phases = 0; phases < MAX_PHASES_PER_FRAME && state.playing; ++phases) {
				if (state.phase == EffectLoopPhase::DelayingBeforeLoop || state.phase == EffectLoopPhase::DelayingAfterLoop) {
					const bool before = state.phase == EffectLoopPhase::DelayingBeforeLoop;
					const f32 left = (before ? state.delayBeforeLoop : state.delayAfterLoop) - state.phaseTime;
					if (remaining < left) {
						state.phaseTime += remaining;
						break;
					}
					remaining -= std::max(0.0f, left);
					state.phaseTime = 0.0f;
					if (before) {
						state.phase = EffectLoopPhase::Looping;
						continue;
					}
					++state.loopIndex;
					if (state.loopCount >= 0.0f && static_cast<f32>(state.loopIndex) >= state.loopCount) {
						state.playing = false;
						state.phase = EffectLoopPhase::Finished;
						break;
					}
					BeginLoop(spawner, state, slots, machine, meshes);
					const f32 period = state.delayBeforeLoop + std::max(0.0f, state.loopDuration) + state.delayAfterLoop;
					if (looped && (remaining <= 0.0f || (state.loopDuration >= 0.0f && period <= 0.0f))) {
						break;
					}
					continue;
				}
				const f32 span = state.loopDuration < 0.0f ? remaining : std::min(remaining, std::max(0.0f, state.loopDuration - state.phaseTime));
				spawned += RunBlocks(spawner, state, span, slots, machine, meshes);
				looped = true;
				state.phaseTime += span;
				remaining -= span;
				if (state.loopDuration >= 0.0f && state.phaseTime >= state.loopDuration) {
					state.phase = EffectLoopPhase::DelayingAfterLoop;
					state.phaseTime = 0.0f;
					continue;
				}
				break;
			}
			state.carry += spawned;
			const f32 whole = std::floor(state.carry);
			state.carry -= whole;
			return whole >= 4294967040.0f ? 0xFFFFFF00u : static_cast<u32>(whole);
		}

		bool FindAttribute(const EffectSpawner& spawner, const EffectSpawnerState& state, std::string_view attribute, EffectValue& out) {
			bool found = false;
			for (size_t i = 0; i < spawner.blocks.size() && i < state.values.size(); ++i) {
				const EffectSpawnKind kind = spawner.blocks[i].kind;
				if ((kind == EffectSpawnKind::SetAttribute || kind == EffectSpawnKind::IncrementStripIndex) && spawner.blocks[i].attribute == attribute) {
					out = state.values[i];
					found = true;
				}
			}
			return found;
		}

	} // namespace EffectSpawners

} // namespace Shalltear::VisualEffectsImpl
