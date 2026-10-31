#include "EffectGraphBuild.h"

#include "EffectAsset.h"
#include "EffectProgram.h"

#include "GraphDefinitions.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

namespace Shalltear::VisualEffectsEditor {

	using namespace VisualEffectsImpl;

	namespace {

		constexpr std::string_view GENERATED_DIRECTORY = "project://Library/VisualEffects";

		void Report(const GraphDocument& document, GraphDefinitions& definitions, std::string_view graph, const GraphDiagnostic& diagnostic) {
			if (diagnostic.node == 0) {
				SLT_LOG_ERROR("VisualEffects", "'{}': {}", graph, diagnostic.message);
				return;
			}
			const GraphNode* node = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), diagnostic.node);
			const GraphNodeDefinition* definition = node ? definitions.Find(document, *node) : nullptr;
			const str what = definition ? str(definition->name) : (node ? node->def : str("?"));
			SLT_LOG_ERROR("VisualEffects", "'{}': node {} ({}) {}", graph, diagnostic.node, what, diagnostic.message);
		}

		EffectPropertyType TypeOf(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Integer:
				return EffectPropertyType::Integer;
			case GraphPropertyKind::Boolean:
				return EffectPropertyType::Boolean;
			case GraphPropertyKind::Vector2:
				return EffectPropertyType::Vector2;
			case GraphPropertyKind::Vector3:
				return EffectPropertyType::Vector3;
			case GraphPropertyKind::Vector4:
				return EffectPropertyType::Vector4;
			case GraphPropertyKind::Color:
				return EffectPropertyType::Color;
			default:
				return EffectPropertyType::Float;
			}
		}

		bool AssembleProgram(std::string_view graph, const str& text, EffectProgram& out) {
			str error;
			if (!EffectPrograms::Assemble(text, out, error)) {
				SLT_LOG_ERROR("VisualEffects", "'{}': a CPU program it compiled to didn't assemble, {}", graph, error);
				return false;
			}
			return true;
		}

		void Free(Vector<ShaderAsset*>& shaders) {
			for (ShaderAsset* shader : shaders) {
				if (shader) {
					Delete(shader);
				}
			}
			shaders.clear();
		}

		template <typename Fn>
		void ForEachShader(const GraphEffect& effect, Fn&& visit) {
			for (size_t s = 0; s < effect.systems.size(); ++s) {
				const GraphEffectSystem& system = effect.systems[s];
				if (!system.cpu) {
					visit(std::format("System{}.Initialize.hlsl", s), system.initializeKernel);
					visit(std::format("System{}.Update.hlsl", s), system.updateKernel);
				}
				for (size_t o = 0; o < system.outputs.size(); ++o) {
					visit(std::format("System{}.Output{}.hlsl", s, o), system.outputs[o].shader);
					if (system.outputs[o].sort != GraphEffectSort::None) {
						visit(std::format("System{}.Output{}.Sort.hlsl", s, o), system.outputs[o].sortKernel);
					}
				}
			}
		}

	} // anonymous namespace

	str GeneratedDirectory(const Path& sourcePath, const Path& virtualPath, std::string_view leaf) {
		const Path library = FileSystem::Resolve(Path(str(GENERATED_DIRECTORY)));
		if (library.ToString().find("://") != str::npos) {
			return {};
		}
		const Guid guid = virtualPath.ToString().empty() ? Guid{} : AssetDatabase::LookupGuid(virtualPath);
		const str stem = guid.IsValid() ? guid.ToString() : std::format("{:016x}", StringHash(sourcePath.ToString()).GetValue());
		str directory = library.ToString() + "/" + stem;
		if (!leaf.empty()) {
			directory += "/" + str(leaf);
		}
		return directory;
	}

	bool Prepare(const GraphDocument& document, std::string_view graph, const str& directory, EffectBuild& out, Vector<GraphDiagnostic>* diagnostics,
				 const GraphEffectCompileOptions& options) {
		GraphDefinitions definitions;
		Vector<GraphDiagnostic> said{GetMemoryResource(MemTag::Editor)};
		const bool compiled = GraphEffectTarget::Compile(document, graph, out.effect, said, &definitions, options);
		for (const GraphDiagnostic& diagnostic : said) {
			if (!compiled && diagnostic.severity == GraphDiagnosticSeverity::Error) {
				Report(document, definitions, graph, diagnostic);
			}
		}
		if (diagnostics) {
			*diagnostics = std::move(said);
		}
		if (!compiled) {
			return false;
		}
		out.dependencies.clear();
		Vector<const GraphReadDocument*> read{GetMemoryResource(MemTag::Editor)};
		definitions.Read(read);
		for (const GraphReadDocument* document : read) {
			if (!document->physicalPath.empty()) {
				out.dependencies.push_back({document->physicalPath, document->contentHash});
			}
		}
		out.shaders.clear();
		ForEachShader(out.effect, [&](const str& name, const str& text) { out.shaders.push_back({directory + "/" + name, text}); });
		return true;
	}

	bool WriteShaders(const EffectBuild& build, std::string_view graph) {
		for (const EffectShaderText& shader : build.shaders) {
			const Path file(shader.file);
			if (FileSystem::ReadTextFile(file, MemTag::Editor) == shader.text) {
				continue;
			}
			FileSystem::CreateDirectory(Path(shader.file.substr(0, shader.file.find_last_of('/'))));
			if (!FileSystem::WriteTextFile(file, shader.text)) {
				SLT_LOG_ERROR("VisualEffects", "'{}': couldn't write its generated shader to '{}'", graph, shader.file);
				return false;
			}
		}
		return true;
	}

	bool Describe(const GraphEffect& effect, std::string_view graph, EffectDescription& description) {
		description = EffectDescription{};
		description.timing = {effect.timing.fixedDeltaTime,
							  effect.timing.exactFixedTime,
							  effect.timing.ignoreTimeScale,
							  effect.timing.prewarmStepCount,
							  effect.timing.prewarmDeltaTime,
							  (effect.timing.cullingFlags & GRAPH_EFFECT_CULL_SIMULATION) != 0,
							  (effect.timing.cullingFlags & GRAPH_EFFECT_CULL_BOUNDS_UPDATE) != 0};
		description.instancing = {effect.instancing.refused != 0 ? EFFECT_INSTANCING_DISABLED : effect.instancing.mode, effect.instancing.capacity};
		for (size_t i = 0; i < effect.properties.size(); ++i) {
			const GraphPropertyInfo& info = effect.properties[i];
			EffectProperty property;
			property.name = info.name;
			property.reference = info.reference;
			property.type = TypeOf(info.kind);
			property.offset = effect.propertyFields[i].offset;
			property.components = effect.propertyFields[i].components;
			for (u32 c = 0; c < 4; ++c) {
				property.defaultValue[c] = info.defaultValue[c];
			}
			description.properties.push_back(std::move(property));
		}
		description.cameraOffset = effect.cameraOffset;
		for (const GraphEffectMesh& mesh : effect.meshes) {
			description.meshes.push_back({mesh.binding, mesh.offset, mesh.mesh});
		}
		description.batchesOffset = effect.batchesOffset;
		description.events.assign(effect.events.begin(), effect.events.end());
		for (const GraphEffectRelay& relay : effect.relays) {
			description.relays.push_back({relay.event, relay.output});
		}
		for (const GraphEffectSpawner& compiled : effect.spawners) {
			EffectSpawner spawner;
			spawner.start.assign(compiled.start.begin(), compiled.start.end());
			spawner.stop.assign(compiled.stop.begin(), compiled.stop.end());
			spawner.outputEvents.assign(compiled.outputEvents.begin(), compiled.outputEvents.end());
			for (const GraphEffectSpawnBlock& block : compiled.blocks) {
				EffectSpawnBlock entry;
				switch (block.kind) {
				case GraphEffectSpawnBlock::Kind::ConstantRate:
					entry.kind = EffectSpawnKind::ConstantRate;
					break;
				case GraphEffectSpawnBlock::Kind::SingleBurst:
					entry.kind = EffectSpawnKind::SingleBurst;
					break;
				case GraphEffectSpawnBlock::Kind::PeriodicBurst:
					entry.kind = EffectSpawnKind::PeriodicBurst;
					break;
				case GraphEffectSpawnBlock::Kind::SetAttribute:
					entry.kind = EffectSpawnKind::SetAttribute;
					break;
				case GraphEffectSpawnBlock::Kind::IncrementStripIndex:
					entry.kind = EffectSpawnKind::IncrementStripIndex;
					break;
				}
				entry.attribute = block.attribute;
				entry.components = block.components;
				if (!AssembleProgram(graph, block.assembly, entry.program)) {
					return false;
				}
				spawner.blocks.push_back(std::move(entry));
			}
			if (!compiled.loopAssembly.empty() && !AssembleProgram(graph, compiled.loopAssembly, spawner.loop)) {
				return false;
			}
			description.spawners.push_back(std::move(spawner));
		}

		u32 next = 0;
		for (const GraphEffectSystem& compiled : effect.systems) {
			EffectSystem system;
			system.name = compiled.name;
			system.capacity = compiled.capacity;
			system.strips = compiled.strips;
			system.world = compiled.world;
			system.attributeCount = compiled.attributeCount;
			for (const GraphEffectStored& stored : compiled.stored) {
				system.stored.push_back({stored.attribute, stored.slot, stored.components});
			}
			for (const GraphEffectField& field : compiled.source) {
				EffectField source{field.name, field.offset, field.components};
				if (const GraphEffectAttributeInfo* attribute = GraphEffectAttributes::Find(field.name)) {
					for (u32 c = 0; c < 4; ++c) {
						source.defaultValue[c] = attribute->defaultValue[c];
					}
				}
				system.source.push_back(std::move(source));
			}
			system.batchStride = compiled.batchStride;
			system.spawners.assign(compiled.spawners.begin(), compiled.spawners.end());
			system.events.assign(compiled.events.begin(), compiled.events.end());
			system.cpu = compiled.cpu;
			if (compiled.cpu) {
				system.initialize = EFFECT_NO_BINDING;
				system.update = EFFECT_NO_BINDING;
				if (!AssembleProgram(graph, compiled.initializeAssembly, system.initializeProgram) ||
					!AssembleProgram(graph, compiled.updateAssembly, system.updateProgram)) {
					return false;
				}
				for (const GraphEffectOutputEvent& event : compiled.initializeOutputEvents) {
					system.initializeOutputEvents.push_back({event.name, event.root});
				}
				for (const GraphEffectOutputEvent& event : compiled.updateOutputEvents) {
					system.updateOutputEvents.push_back({event.name, event.root});
				}
				for (const GraphEffectCarried& carried : compiled.initializeCarried) {
					system.initializeCarried.push_back({carried.attribute, carried.root, carried.components});
				}
				for (const GraphEffectCarried& carried : compiled.updateCarried) {
					system.updateCarried.push_back({carried.attribute, carried.root, carried.components});
				}
			} else {
				system.initialize = next++;
				system.update = next++;
			}
			for (const GraphEffectKernelTexture& texture : compiled.initializeTextures) {
				system.initializeTextures.push_back({texture.binding, texture.samplerBinding, texture.texture});
			}
			for (const GraphEffectKernelTexture& texture : compiled.updateTextures) {
				system.updateTextures.push_back({texture.binding, texture.samplerBinding, texture.texture});
			}
			system.initializeDepthBinding = compiled.initializeDepthBinding;
			system.updateDepthBinding = compiled.updateDepthBinding;
			if (!AssembleProgram(graph, compiled.boundsAssembly, system.bounds)) {
				return false;
			}
			system.boundsWorld = compiled.boundsWorld;
			system.boundsMode = static_cast<EffectBoundsMode>(compiled.boundsMode);
			system.measuresBounds = compiled.measuresBounds;
			system.initializeBoundsRoot = compiled.cpu ? compiled.initializeBoundsRoot : EFFECT_NO_BINDING;
			system.updateBoundsRoot = compiled.cpu ? compiled.updateBoundsRoot : EFFECT_NO_BINDING;
			system.parent = compiled.parent;
			system.parentBinding = compiled.parentBinding;
			for (const GraphEffectEventTarget& target : compiled.initializeEvents) {
				system.initializeEvents.push_back({target.child, target.listsBinding, target.argumentsBinding});
			}
			for (const GraphEffectEventTarget& target : compiled.updateEvents) {
				system.updateEvents.push_back({target.child, target.listsBinding, target.argumentsBinding});
			}
			for (const GraphEffectOutput& compiledOutput : compiled.outputs) {
				EffectOutput output;
				output.shader = next++;
				output.vertices = compiledOutput.vertices;
				output.instanced = compiledOutput.instanced;
				output.mesh = compiledOutput.mesh;
				output.castShadows = compiledOutput.castShadows;
				output.strip = compiledOutput.strip;
				for (const GraphEffectOutputTexture& texture : compiledOutput.textures) {
					output.textures.push_back({texture.slot, texture.texture});
				}
				if (compiledOutput.sort != GraphEffectSort::None) {
					output.sort = static_cast<EffectSort>(compiledOutput.sort);
					output.revertSorting = compiledOutput.revertSorting;
					output.sortKernel = next++;
				}
				system.outputs.push_back(std::move(output));
			}
			description.systems.push_back(std::move(system));
		}
		return true;
	}

	u64 Fingerprint(const GraphEffect& effect, std::string_view graph) {
		EffectDescription description;
		if (!Describe(effect, graph, description)) {
			return 0;
		}
		Vector<u8> cooked{GetMemoryResource(MemTag::Editor)};
		PakStringPool strings;
		CookWriter writer(cooked, strings);
		EffectDescriptions::Cook(description, writer);
		u64 hash = PakContentHash(cooked.data(), cooked.size());
		const auto mix = [&hash](std::string_view text) { hash = (hash ^ PakContentHash(text.data(), text.size())) * 0x100000001b3ull; };
		for (const str& text : strings.Texts()) {
			mix(text);
		}
		ForEachShader(effect, [&](const str&, const str& text) { mix(text); });
		return hash;
	}

	VisualEffectAsset* Assemble(const EffectBuild& build, std::string_view graph, Vector<ShaderAsset*>& shaders) {
		EffectDescription description;
		if (shaders.size() != build.shaders.size() || !Describe(build.effect, graph, description)) {
			Free(shaders);
			return nullptr;
		}

		Vector<EffectDependency> dependencies{build.dependencies.begin(), build.dependencies.end(), GetMemoryResource(MemTag::Asset)};
		for (const ShaderAsset* shader : shaders) {
			if (!shader) {
				continue;
			}
			for (const ShaderAsset::Dependency& read : shader->dependencies) {
				const bool listed = std::any_of(dependencies.begin(), dependencies.end(), [&](const EffectDependency& held) { return held.physicalPath == read.physicalPath; });
				if (!listed) {
					dependencies.push_back({read.physicalPath, read.contentHash});
				}
			}
		}
		VisualEffectAsset* asset = New<VisualEffectAsset>(MemTag::Asset);
		if (!EffectAssetAccess::Adopt(*asset, std::move(description), shaders)) {
			Delete(asset);
			return nullptr;
		}
		EffectAssetAccess::SetDependencies(*asset, std::move(dependencies));
		return asset;
	}

} // namespace Shalltear::VisualEffectsEditor
