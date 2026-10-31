#include "GraphEffect.h"

#include "GraphDefinitions.h"
#include "GraphEffectBlocks.h"
#include "GraphHLSL.h"
#include "GraphNodeLibrary.h"
#include "GraphShading.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>

namespace Shalltear {

	namespace {

		using T = GraphValueType;

		const GraphBinding EFFECT_BINDINGS[] = {
			{"TimeParameters", "SltParticleEffectTime()", T::Float4},
			{"SmoothDeltaTime", "SltParticleEffectSmoothDelta()", T::Float},
			{str(GRAPH_EFFECT_RANDOM_PARTICLE), "SltParticleRandomKey(sltParticleId)", T::Float},
			{str(GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT), "SltParticleConstantRandomKey(sltParticleId)", T::Float},
			{str(GRAPH_EFFECT_RANDOM_EFFECT), "SltParticleEffectRandomKey()", T::Float},
			{str(GRAPH_EFFECT_RANDOM_EFFECT_CONSTANT), "SltParticleEffectConstantRandomKey()", T::Float},
		};

		str AttributeLocal(std::string_view name) { return "sltAttr_" + str(name); }
		str SourceLocal(std::string_view name) { return "sltSource_" + str(name); }

		str FoggedPixel(std::string_view pixel, RHI::BlendMode blend) {
			str name(RHI::BlendModeName(blend));
			for (char& c : name) {
				c = static_cast<char>(c - 'a' + 'A');
			}
			return std::format("\treturn ShalltearExpose(ShalltearApplyFog({}, sltIn.positionWS, SHALLTEAR_BLEND_{}), SHALLTEAR_BLEND_{});", pixel,
							   name, name);
		}

		GraphValueType TextureTypeOf(GraphPortType type) {
			switch (type) {
			case GraphPortType::Texture2D:
				return T::Texture2D;
			case GraphPortType::Texture2DArray:
				return T::Texture2DArray;
			case GraphPortType::Texture3D:
				return T::Texture3D;
			case GraphPortType::Cubemap:
				return T::TextureCube;
			default:
				return T::None;
			}
		}

		constexpr u32 MAX_SUBGRAPH_DEPTH = 32;

		template <typename Fn>
		void ForEachCompiledNode(const GraphDocument& document, GraphDefinitions& definitions, Fn&& visit, Vector<u32>& within, Vector<Guid>& chain) {
			const auto node = [&](const GraphNode& held) {
				const GraphNodeDefinition* definition = definitions.Find(document, held);
				if (!definition) {
					return;
				}
				visit(std::span<const u32>(within), held, *definition);
				const GraphSubgraph* subgraph = definition->subgraph;
				if (!subgraph || within.size() >= MAX_SUBGRAPH_DEPTH || std::find(chain.begin(), chain.end(), subgraph->asset) != chain.end()) {
					return;
				}
				within.push_back(held.id);
				chain.push_back(subgraph->asset);
				ForEachCompiledNode(subgraph->document, definitions, visit, within, chain);
				within.pop_back();
				chain.pop_back();
			};
			for (const GraphNode& held : document.nodes) {
				node(held);
			}
			for (const GraphContext& context : document.contexts) {
				node(context.node);
				for (const GraphNode& block : context.blocks) {
					node(block);
				}
			}
		}

		template <typename Fn>
		void ForEachCompiledNode(const GraphDocument& document, GraphDefinitions& definitions, Fn&& visit) {
			Vector<u32> within{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> chain{GetMemoryResource(MemTag::Editor)};
			ForEachCompiledNode(document, definitions, visit, within, chain);
		}

		template <typename Fn>
		void ForEachPortTexture(const GraphDocument& document, GraphDefinitions& definitions, Fn&& visit) {
			ForEachCompiledNode(document, definitions, [&](std::span<const u32> within, const GraphNode& held, const GraphNodeDefinition& definition) {
				for (u32 i = 0; i < definition.inputs.size(); ++i) {
					const GraphPortDefinition& port = definition.inputs[i];
					const GraphValueType type = TextureTypeOf(port.type);
					if (type != T::None && port.binding.empty()) {
						visit(within, held, i, port, type);
					}
				}
			});
		}

		template <typename Fn>
		void ForEachPortMesh(const GraphDocument& document, GraphDefinitions& definitions, Fn&& visit) {
			ForEachCompiledNode(document, definitions, [&](std::span<const u32> within, const GraphNode& held, const GraphNodeDefinition& definition) {
				for (const GraphPortDefinition& port : definition.inputs) {
					if (port.type == GraphPortType::Mesh && port.binding.empty()) {
						visit(within, held, port);
					}
				}
			});
		}

		str PortTextureName(std::span<const u32> within, u32 node, u32 index) {
			str name = "sltTexture";
			for (const u32 use : within) {
				name += std::to_string(use) + "_";
			}
			return name + std::format("{}_{}", node, index);
		}

		void CameraDepthBindings(u32 offset, std::string_view unavailable, Vector<GraphBinding>& out) {
			GraphBinding image{str(GRAPH_EFFECT_CAMERA_DEPTH), "sltCameraDepth", T::Texture2D};
			image.unavailable = str(unavailable);
			out.push_back(std::move(image));
			for (u32 matrix = 0; matrix < std::size(GRAPH_EFFECT_CAMERA_DEPTH_MATRICES); ++matrix) {
				for (u32 row = 0; row < 4; ++row) {
					GraphBinding read{str(GRAPH_EFFECT_CAMERA_DEPTH_MATRICES[matrix]) + std::to_string(row),
									  std::format("SltParticleCameraRow({}u, {}u, {}u)", offset, matrix, row), T::Float4};
					read.unavailable = str(unavailable);
					out.push_back(std::move(read));
				}
			}
			GraphBinding size{str(GRAPH_EFFECT_CAMERA_DEPTH_SIZE), std::format("SltParticleCameraSize({}u)", offset), T::Float4};
			size.unavailable = str(unavailable);
			out.push_back(std::move(size));
		}

		bool HoldsCameraDepth(const GraphDocument& document, GraphDefinitions& definitions) {
			bool holds = false;
			ForEachCompiledNode(document, definitions, [&holds](std::span<const u32>, const GraphNode& node, const GraphNodeDefinition&) {
				holds = holds || node.def == GRAPH_EFFECT_COLLISION_DEPTH || node.def == GRAPH_EFFECT_POSITION_DEPTH;
			});
			return holds;
		}

		constexpr std::string_view PARTICLE_ID = "particleId";
		constexpr std::string_view SPAWN_INDEX = "spawnIndex";
		constexpr std::string_view SPAWN_COUNT = "spawnCount";

		enum class ContextKind : u8 {
			Event,
			Spawn,
			GPUEvent,
			OutputEvent,
			Initialize,
			Update,
			Output,
			Subgraph,
			Other
		};

		ContextKind KindOf(std::string_view def) {
			if (def == GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
				return ContextKind::Subgraph;
			}
			if (def == GRAPH_EFFECT_EVENT) {
				return ContextKind::Event;
			}
			if (def == GRAPH_EFFECT_SPAWN) {
				return ContextKind::Spawn;
			}
			if (def == GRAPH_EFFECT_GPU_EVENT) {
				return ContextKind::GPUEvent;
			}
			if (def == GRAPH_EFFECT_OUTPUT_EVENT) {
				return ContextKind::OutputEvent;
			}
			if (def == GRAPH_EFFECT_INITIALIZE) {
				return ContextKind::Initialize;
			}
			if (def == GRAPH_EFFECT_UPDATE) {
				return ContextKind::Update;
			}
			if (def == GRAPH_EFFECT_OUTPUT || def == GRAPH_EFFECT_OUTPUT_MESH || def == GRAPH_EFFECT_OUTPUT_POINT || def == GRAPH_EFFECT_OUTPUT_LINE ||
				def == GRAPH_EFFECT_OUTPUT_LIT || def == GRAPH_EFFECT_OUTPUT_LIT_MESH || def == GRAPH_EFFECT_OUTPUT_STRIP ||
				GraphEffectShading::IsNodeShaderOutput(def)) {
				return ContextKind::Output;
			}
			return ContextKind::Other;
		}

		bool IsStripOutput(std::string_view def) { return def == GRAPH_EFFECT_OUTPUT_STRIP || def == GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP; }

		constexpr std::string_view STRIP_INDEX = "stripIndex";
		constexpr std::string_view PARTICLE_INDEX_IN_STRIP = "particleIndexInStrip";
		constexpr std::string_view PARTICLE_COUNT_IN_STRIP = "particleCountInStrip";
		constexpr std::string_view SPAWN_INDEX_IN_STRIP = "spawnIndexInStrip";

		bool IsStripAttribute(std::string_view name) {
			return name == STRIP_INDEX || name == PARTICLE_INDEX_IN_STRIP || name == PARTICLE_COUNT_IN_STRIP || name == SPAWN_INDEX_IN_STRIP;
		}

		enum class StripRead : u8 {
			None,
			Spawn,
			Kernel,
			Output
		};

		class Settings {
		public:
			explicit Settings(const GraphNode& node) : _node(node), _arena(1024, MemTag::Editor) {}

			const JSONValue* Find(std::string_view key) {
				for (const GraphSetting& setting : _node.settings) {
					if (setting.key == key) {
						_held = JSONParse(setting.value, _arena);
						return &_held;
					}
				}
				return nullptr;
			}

			f32 Number(std::string_view key, f32 fallback) {
				const JSONValue* value = Find(key);
				return value && value->IsNumber() ? static_cast<f32>(value->AsNumber()) : fallback;
			}

			bool Toggle(std::string_view key, bool fallback) {
				const JSONValue* value = Find(key);
				return value && value->IsBool() ? value->AsBool() : fallback;
			}

			str Text(std::string_view key, std::string_view fallback) {
				const JSONValue* value = Find(key);
				return value && value->IsString() ? str(value->AsString()) : str(fallback);
			}

			u32 Choice(std::string_view key, std::span<const std::string_view> options, u32 fallback) {
				const JSONValue* value = Find(key);
				if (!value) {
					return fallback;
				}
				for (u32 i = 0; i < options.size(); ++i) {
					if (value->IsString() && value->AsString() == options[i]) {
						return i;
					}
				}
				return static_cast<u32>(options.size());
			}
		private:
			const GraphNode& _node;
			LinearAllocator _arena;
			JSONValue _held;
		};

		constexpr std::string_view SPACES[] = {"Local", "World"};
		constexpr std::string_view BLEND_MODES[] = {"Additive", "Alpha", "AlphaPremultiplied", "Opaque"};
		constexpr std::string_view REPEATS[] = {"Single", "Periodic"};
		constexpr std::string_view RANGE_MODES[] = {"Constant", "Random"};

		constexpr f32 MAX_CAPACITY = 16777216.0f;

		struct Writer {
			str text;
			Vector<u32> lines{GetMemoryResource(MemTag::Editor)};

			void Line(std::string_view line, u32 node = 0) {
				text += line;
				text += '\n';
				lines.push_back(node);
			}

			void Lines(std::string_view block, std::span<const u32> nodes) {
				text += block;
				for (size_t i = 0, line = 0; i < block.size(); ++i) {
					if (block[i] == '\n') {
						lines.push_back(line < nodes.size() ? nodes[line] : 0);
						++line;
					}
				}
			}
		};

		str Literal(const GraphEffectAttributeInfo& attribute) {
			if (attribute.type == T::Bool) {
				return attribute.defaultValue[0] != 0.0f ? "true" : "false";
			}
			if (attribute.type == T::Float) {
				return GraphHLSL::FloatLiteral(attribute.defaultValue[0]);
			}
			return std::format("float3({}, {}, {})", GraphHLSL::FloatLiteral(attribute.defaultValue[0]), GraphHLSL::FloatLiteral(attribute.defaultValue[1]),
							   GraphHLSL::FloatLiteral(attribute.defaultValue[2]));
		}

		u32 ComponentsOf(const GraphEffectAttributeInfo& attribute) { return attribute.type == T::Float3 ? 3u : 1u; }

		str LoadExpression(const GraphEffectStored& stored, std::string_view load) {
			if (stored.components == 1) {
				return std::format("{}({}, sltIndex)", load, stored.slot);
			}
			return std::format("float3({0}({1}, sltIndex), {0}({2}, sltIndex), {0}({3}, sltIndex))", load, stored.slot, stored.slot + 1,
							   stored.slot + 2);
		}

		struct Stack {
			GraphProgram program;
			HashMap<str, GraphValueId> current{GetMemoryResource(MemTag::Editor)};
			HashSet<str> written{GetMemoryResource(MemTag::Editor)};
			HashSet<str> read{GetMemoryResource(MemTag::Editor)};
			Vector<std::pair<u32, GraphValueId>> events{GetMemoryResource(MemTag::Editor)};

			GraphValueId Current(const GraphEffectAttributeInfo& attribute) {
				const str binding = GraphEffectAttributes::CurrentBinding(attribute.name);
				if (auto found = current.find(binding); found != current.end()) {
					return found->second;
				}
				GraphProgramBuilder builder(program);
				return builder.External(attribute.type, binding, AttributeLocal(attribute.name));
			}

			void Write(const GraphEffectAttributeInfo& attribute, GraphValueId value) {
				current[GraphEffectAttributes::CurrentBinding(attribute.name)] = value;
				written.insert(str(attribute.name));
			}
		};

		void ReadsOf(const GraphProgram& program, std::string_view prefix, HashSet<str>& out) {
			for (const GraphValue& value : program.values) {
				if (value.op != GraphOp::External) {
					continue;
				}
				const str& name = program.externals[value.payload].name;
				if (name.starts_with(prefix)) {
					out.insert(name.substr(prefix.size()));
				}
			}
		}

		bool ReadsExternal(const GraphProgram& program, std::string_view name) {
			for (const GraphValue& value : program.values) {
				if (value.op == GraphOp::External && program.externals[value.payload].name == name) {
					return true;
				}
			}
			return false;
		}

		void AttributeReadsOf(const GraphProgram& program, HashSet<str>& out) {
			ReadsOf(program, "Attribute/", out);
			if (ReadsExternal(program, GRAPH_EFFECT_RANDOM_PARTICLE) || ReadsExternal(program, GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT)) {
				out.insert(str(PARTICLE_ID));
			}
		}

		struct SlotFeed {
			Vector<str> events{GetMemoryResource(MemTag::Editor)};
			Vector<u32> spawners{GetMemoryResource(MemTag::Editor)};
		};

		class EffectCompile {
		public:
			EffectCompile(const GraphDocument& document, std::string_view sourceName, GraphDefinitions& definitions,
						  Vector<GraphDiagnostic>& diagnostics, const GraphEffectCompileOptions& options)
				: _document(document), _root(document), _sourceName(sourceName), _definitions(definitions), _diagnostics(diagnostics), _options(options) {}

			EffectCompile(const EffectCompile& parent, const GraphSubgraph& subgraph, const GraphContext& use, HashMap<str, SlotFeed> slots)
				: _document(subgraph.document), _root(parent._root), _sourceName(parent._sourceName), _definitions(parent._definitions),
				  _diagnostics(parent._diagnostics), _options(parent._options), _inner(true) {
				_within.assign(parent._within.begin(), parent._within.end());
				_within.push_back(use.node.id);
				_report = parent._inner ? parent._report : use.node.id;
				_where = parent._where + (parent._inner ? parent.Describe(use.node.id) + " " : str()) + "in subgraph '" + subgraph.name + "', ";
				_label = parent._label + " in subgraph '" + subgraph.name + "'";
				_systemChain.assign(parent._systemChain.begin(), parent._systemChain.end());
				_systemChain.push_back({subgraph.asset, subgraph.name});
				_slots = std::move(slots);
				_properties = parent._properties;
				_propertyFields = parent._propertyFields;
				_portTextures.assign(parent._portTextures.begin(), parent._portTextures.end());
				_portTextureAssets.assign(parent._portTextureAssets.begin(), parent._portTextureAssets.end());
				_cpuBindings.assign(parent._cpuBindings.begin(), parent._cpuBindings.end());
				_portMeshes.assign(parent._portMeshes.begin(), parent._portMeshes.end());
				_meshesOffset = parent._meshesOffset;
				_cameraOffset = parent._cameraOffset;
			}

			bool Run(GraphEffect& out) {
				if (_document.type != GRAPH_EFFECT_DOCUMENT_TYPE) {
					Report(0, "is a '" + _document.type + "' document (not an effect graph)");
					return false;
				}
				ReadBlackboard(out);
				GraphEffectTarget::PortTextureBindings(_document, _definitions, _portTextures);
				ForEachPortTexture(_document, _definitions, [this](std::span<const u32>, const GraphNode& node, u32, const GraphPortDefinition& port, GraphValueType) {
					_portTextureAssets.push_back(GraphEffectTarget::HeldTexture(node, port.name));
				});
				GraphEffectTarget::PortMeshBindings(_document, _definitions, _portMeshes, _meshesOffset);
				_cpuBindings.assign(std::begin(EFFECT_BINDINGS), std::end(EFFECT_BINDINGS));
				for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
					for (const bool source : {false, true}) {
						GraphBinding read{source ? GraphEffectAttributes::SourceBinding(attribute.name) : GraphEffectAttributes::CurrentBinding(attribute.name),
										  source ? SourceLocal(attribute.name) : AttributeLocal(attribute.name), attribute.type};
						read.unavailable = "only a particle context reads a particle's attributes (a Spawn's values and a system's bounds are the CPU's)";
						_cpuBindings.push_back(std::move(read));
					}
				}
				GraphEffectTarget::PortTextureBindings(_document, _definitions, _cpuBindings,
													   "only a particle context samples a texture (a Spawn's values and a system's bounds are the CPU's)");
				_cpuBindings.insert(_cpuBindings.end(), _portMeshes.begin(), _portMeshes.end());
				CameraDepthBindings(_cameraOffset, "only a particle kernel reads the camera's depth (a Spawn's values and a system's bounds are the CPU's)",
									_cpuBindings);
				const bool compiled = CompileDocument(out);
				std::sort(out.events.begin(), out.events.end());
				out.events.erase(std::unique(out.events.begin(), out.events.end()), out.events.end());
				return compiled && !Failed();
			}

			void WarnStack(const GraphContext& stack, std::span<const GraphBinding> bindings) {
				Vector<StackBlock> blocks{GetMemoryResource(MemTag::Editor)};
				ExpandBlocks(stack, blocks);
				WarnBlocks(stack, blocks, bindings, GraphSpace::Local);
			}

			void WarnOperators() {
				for (const GraphNode& node : _document.nodes) {
					const GraphNodeDefinition* definition = node.def == GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR ? _definitions.Find(_document, node) : nullptr;
					for (const str& warning : definition && definition->customHLSL ? std::span<const str>(definition->customHLSL->warnings) : std::span<const str>()) {
						Warn(node.id, "hlsl-duplicate-function", warning);
					}
				}
			}
		private:
			bool CompileDocument(GraphEffect& out) {
				if (_inner) {
					CheckSubgraphBlackboard();
				}
				IndexContexts();
				CheckFlow();
				WarnOperators();
				if (Failed()) {
					return false;
				}
				ReadEvents(out);
				ReadGPUEvents();
				ReadOutputEvents();
				ReadOutputEventFlows(out);
				ReadAccumulators();
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) == ContextKind::Initialize) {
						BuildStacks(*context, out);
					}
				}
				if (Failed()) {
					return false;
				}
				BuildSystems(out);
				if (Failed()) {
					return false;
				}
				CompileSystemSubgraphs(out);
				return !Failed();
			}

			struct SystemBuild;

			bool Failed() const { return _failed; }

			void Report(u32 node, str message) {
				if (_inner) {
					message = _where + (node != 0 ? Describe(node) + " " : str()) + message;
					node = _report;
				}
				Pass({node, std::move(message)});
			}

			void Adopt(GraphDiagnostic diagnostic) {
				if (diagnostic.node == 0) {
					Report(0, std::move(diagnostic.message));
					return;
				}
				Pass(std::move(diagnostic));
			}

			void Pass(GraphDiagnostic diagnostic) {
				_failed = true;
				for (const GraphDiagnostic& said : _diagnostics) {
					if (said.node == diagnostic.node && said.message == diagnostic.message) {
						return;
					}
				}
				_diagnostics.push_back(std::move(diagnostic));
			}

			void Warn(u32 node, std::string_view key, std::string_view message) {
				str said(message);
				if (_inner) {
					said = _where + (node != 0 ? Describe(node) + ": " : str()) + said;
					node = _report;
				}
				const GraphNode* holder = node != 0 ? RootNode(node) : nullptr;
				if (holder && GraphEffectRules::IgnoresError(*holder, key)) {
					return;
				}
				for (const GraphDiagnostic& diagnostic : _diagnostics) {
					if (diagnostic.node == node && diagnostic.key == key && diagnostic.message == said) {
						return;
					}
				}
				_diagnostics.push_back({node, std::move(said), GraphDiagnosticSeverity::Warning, str(key)});
			}

			const GraphNode* RootNode(u32 id) const {
				for (const GraphNode& node : _root.nodes) {
					if (node.id == id) {
						return &node;
					}
				}
				for (const GraphContext& context : _root.contexts) {
					if (context.node.id == id) {
						return &context.node;
					}
					for (const GraphNode& block : context.blocks) {
						if (block.id == id) {
							return &block;
						}
					}
				}
				return nullptr;
			}

			str Describe(u32 node) const {
				for (const GraphContext& context : _document.contexts) {
					if (context.node.id == node) {
						return "context " + std::to_string(node);
					}
					for (const GraphNode& block : context.blocks) {
						if (block.id == node) {
							return "block " + std::to_string(node);
						}
					}
				}
				return "node " + std::to_string(node);
			}

			u32 Attributed(u32 node) const { return _inner ? _report : node; }

			void ReadBlackboard(GraphEffect& out) {
				const size_t before = _diagnostics.size();
				if (!GraphProperties::ParseBlackboard(_document, out.properties, _diagnostics)) {
					_failed = _diagnostics.size() > before || _failed;
				}
				u32 offset = GraphEffectTarget::FRAME_HEADER_BYTES;
				for (const GraphProperty& property : _document.properties) {
					if (GraphSubgraphs::IsOutput(property)) {
						Report(0, "property '" + property.name + "' is an Output (only a subgraph operator has one)");
					}
				}
				for (const GraphPropertyInfo& property : out.properties) {
					const GraphValueType type = GraphProperties::ValueType(property.kind);
					if (GraphProperties::IsTexture(property.kind) || GraphProperties::IsKeyword(property.kind)) {
						Report(0, "property '" + property.name + "' is a " + (GraphProperties::IsTexture(property.kind) ? "texture" : "keyword") +
									  " (an effect can't read it yet)");
						continue;
					}
					out.propertyFields.push_back({property.reference, offset, GraphTypes::Components(type)});
					offset += 16;
				}
				if (HoldsCameraDepth(_document, _definitions)) {
					out.cameraOffset = offset;
					offset += GRAPH_EFFECT_CAMERA_RECORD_BYTES;
				}
				_meshesOffset = offset;
				ForEachPortMesh(_document, _definitions, [&](std::span<const u32> within, const GraphNode& node, const GraphPortDefinition& port) {
					out.meshes.push_back({GraphCompiler::PortMeshBinding(node.id, port.name, within), offset, GraphEffectTarget::HeldMesh(node, port.name)});
					offset += GRAPH_EFFECT_MESH_RECORD_BYTES;
				});
				out.batchesOffset = offset;
				_cameraOffset = out.cameraOffset;
				_properties = &out.properties;
				_propertyFields = &out.propertyFields;
			}

			void CheckSubgraphBlackboard() {
				for (const GraphProperty& property : _document.properties) {
					GraphPropertyInfo info;
					str error;
					if (!GraphProperties::Parse(property, info, error)) {
						Report(0, "property '" + property.name + "' " + error);
					} else if (GraphProperties::IsTexture(info.kind) || GraphProperties::IsKeyword(info.kind)) {
						Report(0, "property '" + property.name + "' is a " + (GraphProperties::IsTexture(info.kind) ? "texture" : "keyword") +
									  " (an effect can't read it yet)");
					}
				}
			}

			void IndexContexts() {
				for (const GraphContext& context : _document.contexts) {
					_contexts.push_back(&context);
					const GraphNodeDefinition* definition = GraphNodeLibrary::Find(context.node.def);
					if (!definition) {
						Report(context.node.id, "has no definition '" + context.node.def + "' (unchanged, compiles nothing)");
					} else if (KindOf(context.node.def) == ContextKind::Other) {
						Report(context.node.id, "is a " + str(definition->name) + " context (an effect graph has none)");
					}
				}
			}

			const GraphContext* ContextById(u32 id) const {
				for (const GraphContext* context : _contexts) {
					if (context->node.id == id) {
						return context;
					}
				}
				return nullptr;
			}

			std::string_view NameOf(const GraphContext& context) const {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(context.node.def);
				return definition ? definition->name : std::string_view(context.node.def);
			}

			std::span<const std::string_view> FlowInputsOf(const GraphContext& context) {
				if (KindOf(context.node.def) == ContextKind::Subgraph) {
					const GraphNodeDefinition* built = _definitions.Find(_document, context.node);
					return built ? built->flowInputs : std::span<const std::string_view>();
				}
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(context.node.def);
				return definition ? definition->flowInputs : std::span<const std::string_view>();
			}

			void CheckFlow() {
				for (const GraphContext* context : _contexts) {
					const GraphNodeDefinition* definition = GraphNodeLibrary::Find(context->node.def);
					const bool subgraph = KindOf(context->node.def) == ContextKind::Subgraph;
					const auto slots = FlowInputsOf(*context);
					u32 feeders = 0;
					for (const GraphFlow& flow : context->flows) {
						const GraphContext* source = ContextById(flow.from);
						if (!source || !definition) {
							continue;
						}
						const bool known = std::find(slots.begin(), slots.end(), std::string_view(flow.slot)) != slots.end();
						if (!known && subgraph) {
							continue;
						}
						if (!known) {
							Report(context->node.id, "is linked at '" + flow.slot + "', a flow input it doesn't have");
							continue;
						}
						if (!GraphEffectRules::MayFeed(source->node.def, context->node.def, flow.slot)) {
							Report(context->node.id, "is fed by " + str(NameOf(*source)) + " context " + std::to_string(source->node.id) +
														 ", which cannot feed a " + str(NameOf(*context)));
							continue;
						}
						++feeders;
					}
					if (GraphEffectRules::TakesOneSource(context->node.def) && feeders > 1) {
						Report(context->node.id, "is fed by " + std::to_string(feeders) + " contexts, and one system's " + str(NameOf(*context)) +
													 " is fed by one");
					}
				}
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::Initialize) {
						continue;
					}
					u32 updates = 0;
					for (const GraphContext* reader : _contexts) {
						if (KindOf(reader->node.def) == ContextKind::Update && FedBy(*reader, context->node.id)) {
							if (++updates > 1) {
								Report(reader->node.id, "is a 2nd Update fed by Initialize context " + std::to_string(context->node.id) +
															" (a system has 1)");
							}
						}
					}
				}
			}

			static bool FedBy(const GraphContext& context, u32 source) {
				return std::any_of(context.flows.begin(), context.flows.end(), [source](const GraphFlow& flow) { return flow.from == source; });
			}

			void ReadEvents(GraphEffect& out) {
				if (!_inner) {
					out.events.push_back(str(GRAPH_EFFECT_ON_PLAY));
					out.events.push_back(str(GRAPH_EFFECT_ON_STOP));
				}
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) == ContextKind::Event) {
						const SlotFeed feed = Resolve(EventName(*context));
						out.events.insert(out.events.end(), feed.events.begin(), feed.events.end());
					}
				}
			}

			static str EventName(const GraphContext& context) { return Settings(context.node).Text("Event Name", GRAPH_EFFECT_ON_PLAY); }
			static str OutputEventName(const GraphContext& context) { return Settings(context.node).Text("Event Name", GRAPH_EFFECT_DEFAULT_OUTPUT_EVENT_NAME); }

			SlotFeed Resolve(const str& name) const {
				if (const auto found = _slots.find(name); found != _slots.end()) {
					return found->second;
				}
				SlotFeed feed;
				feed.events.push_back(name);
				return feed;
			}

			const GraphContext* HolderOf(u32 block) const {
				for (const GraphContext* context : _contexts) {
					for (const GraphNode& held : context->blocks) {
						if (held.id == block) {
							return context;
						}
					}
				}
				return nullptr;
			}

			u32 SystemOf(const GraphContext& context) const {
				if (KindOf(context.node.def) == ContextKind::Initialize) {
					return context.node.id;
				}
				if (KindOf(context.node.def) == ContextKind::Update) {
					for (const GraphFlow& flow : context.flows) {
						const GraphContext* source = ContextById(flow.from);
						if (source && KindOf(source->node.def) == ContextKind::Initialize) {
							return source->node.id;
						}
					}
				}
				return 0;
			}

			void ReadGPUEvents() {
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::GPUEvent) {
						continue;
					}
					for (const GraphInput& input : context->node.inputs) {
						if (input.port != GRAPH_EFFECT_EVT || input.fromNode == 0) {
							continue;
						}
						const GraphContext* holder = HolderOf(input.fromNode);
						const GraphNode* trigger = nullptr;
						for (const GraphNode& block : holder ? std::span<const GraphNode>(holder->blocks) : std::span<const GraphNode>()) {
							trigger = block.id == input.fromNode ? &block : trigger;
						}
						if (!trigger || trigger->def != GRAPH_EFFECT_TRIGGER_EVENT || input.fromPort != GRAPH_EFFECT_EVT) {
							Report(context->node.id, "takes its Evt from a node that isn't a Trigger Event");
							continue;
						}
						_wiredEvents[trigger->id].push_back(context->node.id);
						_eventSystem[context->node.id] = SystemOf(*holder);
					}
				}
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::Initialize) {
						continue;
					}
					u32 spawns = 0;
					u32 events = 0;
					u32 event = 0;
					for (const GraphFlow& flow : context->flows) {
						const GraphContext* source = ContextById(flow.from);
						const ContextKind kind = source ? KindOf(source->node.def) : ContextKind::Other;
						spawns += kind == ContextKind::Spawn || kind == ContextKind::Event ? 1u : 0u;
						if (kind == ContextKind::GPUEvent) {
							++events;
							event = flow.from;
						}
					}
					if (events > 0 && spawns > 0) {
						Report(context->node.id, "is fed by a GPU Event and by a Spawn or an Event (an Initialize spawns from the GPU or the CPU, not both)");
						continue;
					}
					if (events > 1) {
						Report(context->node.id, std::format("is fed by {} GPU Events (an Initialize spawns from 1)", events));
						continue;
					}
					if (events == 0) {
						continue;
					}
					_feedingEvent[context->node.id] = event;
					const auto parent = _eventSystem.find(event);
					if (parent == _eventSystem.end() || parent->second == 0) {
						continue;
					}
					if (parent->second == context->node.id) {
						Report(context->node.id, std::format("is fed by GPU Event context {} (triggered by a Trigger Event of its system)", event));
						continue;
					}
					_parentOf[context->node.id] = parent->second;
				}
				for (const auto& [child, first] : _parentOf) {
					u32 at = first;
					for (size_t steps = 0; at != 0 && steps <= _parentOf.size(); ++steps) {
						if (at == child) {
							Report(child, "is spawned from a system its particles' events spawn (a cycle of GPU events)");
							break;
						}
						const auto up = _parentOf.find(at);
						at = up == _parentOf.end() ? 0u : up->second;
					}
				}
				HashMap<u32, u32> stripChildren{GetMemoryResource(MemTag::Editor)};
				for (const GraphContext* context : _contexts) {
					const auto parent = _parentOf.find(context->node.id);
					if (parent == _parentOf.end() || !GraphEffectRules::MakesStrips(context->node)) {
						continue;
					}
					if (++stripChildren[parent->second] > 1) {
						Report(context->node.id, std::format("is a 2nd strip system spawned from the particles of Initialize context {} "
															 "(each of its particles holds 1 strip)",
															 parent->second));
					}
				}
			}

			void ReadOutputEvents() {
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::OutputEvent) {
						continue;
					}
					for (const GraphInput& input : context->node.inputs) {
						if (input.port != GRAPH_EFFECT_EVT || input.fromNode == 0) {
							continue;
						}
						const GraphContext* holder = HolderOf(input.fromNode);
						const GraphNode* trigger = nullptr;
						for (const GraphNode& block : holder ? std::span<const GraphNode>(holder->blocks) : std::span<const GraphNode>()) {
							trigger = block.id == input.fromNode ? &block : trigger;
						}
						if (!trigger || trigger->def != GRAPH_EFFECT_TRIGGER_EVENT || input.fromPort != GRAPH_EFFECT_EVT) {
							Report(context->node.id, "takes its Evt from a node that isn't a Trigger Event");
							continue;
						}
						const GraphContext* initialize = ContextById(SystemOf(*holder));
						const bool cpu = initialize && Settings(initialize->node).Choice(GRAPH_EFFECT_SIMULATION_TARGET, GRAPH_EFFECT_SIMULATION_TARGETS, 0) == 1;
						if (!cpu) {
							Report(context->node.id, std::format("takes its Evt from a Trigger Event of Initialize context {} (the GPU simulates that "
																 "system, an Output Event only forwards what a CPU-simulated system sends)",
																 initialize ? initialize->node.id : 0u));
							continue;
						}
						_wiredOutputEvents[trigger->id].push_back(context->node.id);
					}
				}
			}

			void ReadOutputEventFlows(GraphEffect& out) {
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::OutputEvent) {
						continue;
					}
					const str name = OutputEventName(*context);
					for (const GraphFlow& flow : context->flows) {
						const GraphContext* source = ContextById(flow.from);
						if (source && KindOf(source->node.def) == ContextKind::Spawn) {
							out.spawners[Spawner(*source, out)].outputEvents.push_back(name);
						} else if (source && KindOf(source->node.def) == ContextKind::Event) {
							const SlotFeed feed = Resolve(EventName(*source));
							for (const str& event : feed.events) {
								out.relays.push_back({event, name});
							}
							for (const u32 spawner : feed.spawners) {
								out.spawners[spawner].outputEvents.push_back(name);
							}
						}
					}
				}
			}

			void ReadAccumulators() {
				for (const GraphContext* context : _contexts) {
					for (const GraphNode& block : context->blocks) {
						if (block.def == GRAPH_EFFECT_TRIGGER_EVENT) {
							_accumulatorNames.push_back(GraphEffectAttributes::RateCount(block.id));
						}
					}
				}
				for (const str& name : _accumulatorNames) {
					_accumulators.push_back({std::string_view(name), T::Float});
					_accumulatorBindings.push_back({GraphEffectAttributes::CurrentBinding(name), AttributeLocal(name), T::Float});
				}
			}

			const GraphEffectAttributeInfo* FindAttribute(std::string_view name) const {
				if (const GraphEffectAttributeInfo* table = GraphEffectAttributes::Find(name)) {
					return table;
				}
				for (const GraphEffectAttributeInfo& accumulator : _accumulators) {
					if (accumulator.name == name) {
						return &accumulator;
					}
				}
				return nullptr;
			}

			template <typename Fn>
			void ForEachAttribute(Fn&& visit) const {
				for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
					visit(attribute);
				}
				for (const GraphEffectAttributeInfo& accumulator : _accumulators) {
					visit(accumulator);
				}
			}

			struct StackBlock {
				const GraphDocument* document = nullptr;
				const GraphNode* block = nullptr;
				Vector<u32> within{GetMemoryResource(MemTag::Editor)};
				u32 report = 0;
				str where;
			};

			struct ChainLink {
				Guid asset;
				str name;
			};

			bool InBlockSubgraph(const StackBlock& entry) const { return entry.within.size() > _within.size(); }

			str Here(const StackBlock& entry) const {
				return InBlockSubgraph(entry) ? entry.where + "block " + std::to_string(entry.block->id) + " " : str();
			}

			void ExpandBlocks(const GraphContext& context, Vector<StackBlock>& out) {
				Vector<u32> within{_within.begin(), _within.end(), GetMemoryResource(MemTag::Editor)};
				Vector<ChainLink> chain{GetMemoryResource(MemTag::Editor)};
				for (const GraphNode& block : context.blocks) {
					ExpandBlock(context, _document, block, within, chain, block.id, str(), out);
				}
			}

			void ExpandBlock(const GraphContext& context, const GraphDocument& document, const GraphNode& block, Vector<u32>& within,
							 Vector<ChainLink>& chain, u32 report, const str& where, Vector<StackBlock>& out) {
				if (block.disabled) {
					return;
				}
				if (block.def != GRAPH_EFFECT_SUBGRAPH_BLOCK) {
					StackBlock& entry = out.emplace_back();
					entry.document = &document;
					entry.block = &block;
					entry.within.assign(within.begin(), within.end());
					entry.report = report;
					entry.where = where;
					return;
				}
				StackBlock probe;
				probe.block = &block;
				probe.within.assign(within.begin(), within.end());
				probe.where = where;
				const str here = Here(probe);
				str error;
				const GraphNodeDefinition* definition = _definitions.Find(document, block, &error);
				if (!definition || !definition->subgraph) {
					Report(report, here + error);
					return;
				}
				const GraphSubgraph& subgraph = *definition->subgraph;
				if (!GraphEffectRules::Suits(subgraph.suitable, context.node.def)) {
					Report(report, here + "uses '" + subgraph.name + "' (its Suitable Contexts are " + str(GRAPH_EFFECT_SUITABLE_CONTEXTS[subgraph.suitable]) +
									   ", " + str(NameOf(context)) + " context " + std::to_string(context.node.id) + " isn't one)");
					return;
				}
				for (size_t i = 0; i < chain.size(); ++i) {
					if (chain[i].asset != subgraph.asset) {
						continue;
					}
					str names;
					for (size_t j = i; j < chain.size(); ++j) {
						names += "'" + chain[j].name + "' -> ";
					}
					Report(report, here + "uses subgraph '" + subgraph.name + "', which uses itself: " + names + "'" + subgraph.name + "'");
					return;
				}
				if (chain.size() >= MAX_SUBGRAPH_DEPTH) {
					Report(report, here + "uses subgraph '" + subgraph.name + "' (nests subgraphs more than " + std::to_string(MAX_SUBGRAPH_DEPTH) + " deep)");
					return;
				}
				const GraphContext* stack = nullptr;
				for (const GraphContext& held : subgraph.document.contexts) {
					stack = held.node.id == subgraph.blockContext ? &held : stack;
				}
				if (!stack) {
					return;
				}
				within.push_back(block.id);
				chain.push_back({subgraph.asset, subgraph.name});
				const str inside = here + "in subgraph '" + subgraph.name + "', ";
				for (const GraphNode& held : stack->blocks) {
					ExpandBlock(context, subgraph.document, held, within, chain, report, inside, out);
				}
				within.pop_back();
				chain.pop_back();
			}

			bool CompileRoots(std::span<const GraphCompileRoot> roots, std::span<const GraphBinding> bindings, GraphProgram& out,
							  GraphSpace space = GraphSpace::None) {
				GraphCompileRequest request;
				request.roots = roots;
				request.bindings = bindings;
				request.definitions = &_definitions;
				request.space = space;
				Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
				if (!GraphCompiler::Compile(_root, request, out, diagnostics)) {
					for (GraphDiagnostic& diagnostic : diagnostics) {
						Adopt(std::move(diagnostic));
					}
					return false;
				}
				return ResolveIncludes(out);
			}

			bool ResolveIncludes(GraphProgram& program) {
				for (const GraphValue& value : program.values) {
					if (value.op != GraphOp::Call) {
						continue;
					}
					str missing;
					auto resolve = [this](std::string_view path) { return IncludePath(path); };
					if (!GraphCustomHLSL::ResolveIncludes(program.calls[value.payload].definition, resolve, missing)) {
						Adopt({value.node, "includes '" + missing + "' (neither the graph's folder nor the project has it)"});
						return false;
					}
				}
				return true;
			}

			str IncludePath(std::string_view path) const {
				if (std::filesystem::path(path).is_absolute()) {
					return FileSystem::Exists(Path(str(path))) ? str(path) : str();
				}
				Vector<str> candidates{GetMemoryResource(MemTag::Editor)};
				const size_t slash = _sourceName.find_last_of("/\\");
				if (slash != std::string_view::npos) {
					candidates.push_back(str(_sourceName.substr(0, slash + 1)) + str(path));
				}
				candidates.push_back("project://" + str(path));
				for (const str& candidate : candidates) {
					if (FileSystem::Exists(Path(candidate))) {
						return FileSystem::Resolve(Path(candidate)).ToString();
					}
				}
				return {};
			}

			void RunBlocks(const GraphContext& context, bool world, Stack& stack, u32 cpu = 0) {
				Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
				const ContextKind kind = KindOf(context.node.def);
				const bool source = kind == ContextKind::Initialize;
				const GraphEffectKernel kernel = kind == ContextKind::Initialize ? GraphEffectKernel::Initialize
																				 : (kind == ContextKind::Output ? GraphEffectKernel::Output : GraphEffectKernel::Update);
				GraphEffectTarget::KernelBindings(kernel, bindings, world, IsStripOutput(context.node.def));
				bindings.insert(bindings.end(), _portTextures.begin(), _portTextures.end());
				bindings.insert(bindings.end(), _portMeshes.begin(), _portMeshes.end());
				bindings.insert(bindings.end(), _accumulatorBindings.begin(), _accumulatorBindings.end());
				AppendCameraDepth(kernel, bindings);
				if (cpu != 0) {
					RefuseOnCpu(bindings, cpu);
				}
				if (kind == ContextKind::Update) {
					stack.current[GraphEffectAttributes::CurrentBinding("oldVelocity")] = stack.Current(*GraphEffectAttributes::Find("velocity"));
					stack.current[GraphEffectAttributes::CurrentBinding("oldPosition")] = stack.Current(*GraphEffectAttributes::Find("position"));
				}
				Vector<StackBlock> blocks{GetMemoryResource(MemTag::Editor)};
				ExpandBlocks(context, blocks);
				WarnBlocks(context, blocks, bindings, world ? GraphSpace::World : GraphSpace::Local);
				for (const StackBlock& entry : blocks) {
					const GraphNode& block = *entry.block;
					const str here = Here(entry);
					if (!GraphEffectRules::Holds(context.node.def, block.def)) {
						const GraphNodeDefinition* table = GraphNodeLibrary::Find(block.def);
						Report(entry.report, here + "is " + (table ? "a " + str(table->name) + " block" : "no block an effect runs") + ", which " +
												 str(NameOf(context)) + " context " + std::to_string(context.node.id) + " cannot hold");
						continue;
					}
					str error;
					const GraphNodeDefinition* definition = _definitions.Find(*entry.document, block, &error);
					if (!definition) {
						Report(entry.report, here + error);
						continue;
					}
					if (!GraphEffectRules::HoldsBlock(context.node.def, block)) {
						Report(entry.report, here + "is a " + str(definition->name) + " block, which " + str(NameOf(context)) + " context " +
												 std::to_string(context.node.id) + " cannot hold");
						continue;
					}
					if (block.def == GRAPH_EFFECT_TRIGGER_EVENT && InBlockSubgraph(entry)) {
						Report(entry.report, here + "is a Trigger Event. No GPU Event of a graph using its subgraph can take its Evt");
						continue;
					}
					if (block.def == GRAPH_EFFECT_TRIGGER_EVENT && cpu != 0 && _wiredEvents.contains(block.id)) {
						Report(entry.report, here + "is a Trigger Event (only a system simulated on the GPU triggers its GPU events). " + CpuSystem(cpu));
						continue;
					}
					if (definition->customHLSL && !definition->customHLSL->error.empty()) {
						Report(entry.report, here + definition->customHLSL->error);
						continue;
					}
					if (definition->outputs.empty()) {
						continue;
					}
					if (!source && Settings(block).Choice("Source", GRAPH_EFFECT_SOURCES, 0) == 1) {
						Report(entry.report, here + "reads its value from the source (only an Initialize context has one)");
						continue;
					}
					Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
					for (const GraphPortDefinition& output : definition->outputs) {
						roots.push_back({block.id, str(output.name), GraphStageVertex, false, entry.within});
					}
					GraphProgram program;
					if (!CompileRoots(roots, bindings, program, world ? GraphSpace::World : GraphSpace::Local)) {
						continue;
					}
					ReadsOf(program, "Attribute/", stack.read);
					Vector<GraphValueId> values{GetMemoryResource(MemTag::Editor)};
					if (!GraphEffectSplice::Append(stack.program, program, stack.current, values)) {
						Report(entry.report, here + "defines a function another block of " + str(NameOf(context)) + " context " + std::to_string(context.node.id) +
												 " defines differently (a kernel holds 1 function per name)");
						continue;
					}
					if (block.def == GRAPH_EFFECT_TRIGGER_EVENT) {
						const u32 mode = Settings(block).Choice("Mode", GRAPH_EFFECT_TRIGGER_MODES, 0);
						if ((GraphEffectBlocks::TRIGGER_RATE_MODES & (1u << mode)) != 0) {
							stack.Write(*FindAttribute(GraphEffectAttributes::RateCount(block.id)), values[1]);
						}
						const auto wired = _wiredEvents.find(block.id);
						for (const u32 event : wired == _wiredEvents.end() ? std::span<const u32>() : std::span<const u32>(wired->second)) {
							AddEvent(stack, event, values[0], block.id);
						}
						const auto sent = _wiredOutputEvents.find(block.id);
						for (const u32 event : sent == _wiredOutputEvents.end() ? std::span<const u32>() : std::span<const u32>(sent->second)) {
							AddEvent(stack, event, values[0], block.id);
						}
						continue;
					}
					for (size_t i = 0; i < definition->outputs.size(); ++i) {
						if (const GraphEffectAttributeInfo* attribute = GraphEffectAttributes::Find(definition->outputs[i].name)) {
							stack.Write(*attribute, values[i]);
						}
					}
				}
			}

			void AddEvent(Stack& stack, u32 event, GraphValueId count, u32 node) {
				for (auto& [held, sum] : stack.events) {
					if (held == event) {
						GraphProgramBuilder builder(stack.program);
						builder.SetNode(Attributed(node));
						sum = builder.Add(sum, count);
						return;
					}
				}
				stack.events.emplace_back(event, count);
			}

			static bool IsCollision(std::string_view def) { return def == GRAPH_EFFECT_COLLISION_SHAPE || def == GRAPH_EFFECT_COLLISION_DEPTH; }

			str WarnHere(const StackBlock& entry) const {
				return InBlockSubgraph(entry) ? entry.where + "block " + std::to_string(entry.block->id) + ": " : str();
			}

			Vector<f32> ConstantPort(const StackBlock& entry, std::string_view port, std::span<const GraphBinding> bindings, GraphSpace space) {
				Vector<f32> out{GetMemoryResource(MemTag::Editor)};
				const GraphCompileRoot root{entry.block->id, str(port), GraphStageVertex, true, entry.within};
				GraphCompileRequest request;
				request.roots = {&root, 1};
				request.bindings = bindings;
				request.definitions = &_definitions;
				request.space = space;
				GraphProgram program;
				Vector<GraphDiagnostic> unsaid{GetMemoryResource(MemTag::Editor)};
				if (!GraphCompiler::Compile(_root, request, program, unsaid) || program.roots.empty()) {
					return out;
				}
				const GraphValueId value = program.roots[0].value;
				if (value < program.values.size() && program.values[value].op == GraphOp::Constant) {
					const std::span<const f32> held = program.ConstantOf(value);
					out.assign(held.begin(), held.end());
				}
				return out;
			}

			void WarnBlocks(const GraphContext& context, std::span<const StackBlock> blocks, std::span<const GraphBinding> bindings, GraphSpace space) {
				using namespace GraphEffectBlocks;
				const bool initialize = KindOf(context.node.def) == ContextKind::Initialize;
				const auto behavior = [](const GraphNode& block) { return Settings(block).Choice("Behavior", BEHAVIORS, BehaviorCollision); };
				const auto contacts = [](const GraphNode& block) { return Settings(block).Choice("Collision Attributes", COLLISION_ATTRIBUTES, 0); };
				const auto writes = [&](const StackBlock& entry, std::string_view attribute) {
					const GraphNodeDefinition* definition = _definitions.Find(*entry.document, *entry.block);
					return definition && std::any_of(definition->outputs.begin(), definition->outputs.end(),
													 [attribute](const GraphPortDefinition& output) { return output.name == attribute; });
				};
				for (size_t i = 0; i < blocks.size(); ++i) {
					const StackBlock& entry = blocks[i];
					const GraphNode& block = *entry.block;
					const str here = WarnHere(entry);
					if (const GraphNodeDefinition* definition = _definitions.Find(*entry.document, block); definition && definition->customHLSL) {
						for (const str& warning : definition->customHLSL->warnings) {
							Warn(entry.report, "hlsl-duplicate-function", here + warning);
						}
					}
					if (IsCollision(block.def)) {
						const u32 does = behavior(block);
						const u32 writesContacts = contacts(block);
						if (does == BehaviorNone && writesContacts == 0) {
							Warn(entry.report, "collision-does-nothing",
								 here + "This block does nothing (its behavior is None, it writes no collision attributes).");
						}
						if (does != BehaviorCollision && writesContacts == 1) {
							Warn(entry.report, "punctual-contacts-need-bounce",
								 here + "Only the Collision behavior writes punctual contacts. Set Collision Attributes to Write Always to record contacts "
										"with this behavior.");
						}
						for (size_t j = i + 1; does == BehaviorCollision && j < blocks.size(); ++j) {
							if (!IsCollision(blocks[j].block->def) && writes(blocks[j], "velocity")) {
								Warn(entry.report, "velocity-after-collision",
									 here + "A later block in this context writes velocity and overrides this bounce. Write velocity before the "
											"collision blocks.");
								break;
							}
						}
					}
					if (block.def == GRAPH_EFFECT_TRIGGER_EVENT) {
						const u32 mode = Settings(block).Choice("Mode", GRAPH_EFFECT_TRIGGER_MODES, TriggerAlways);
						bool heard = false;
						for (size_t j = 0; j < i && !heard; ++j) {
							heard = IsCollision(blocks[j].block->def) && contacts(*blocks[j].block) != 0;
						}
						if (mode == TriggerOnCollide && !heard) {
							Warn(entry.report, "trigger-collide-without-contacts",
								 here + "This sends no event. No collision block above it writes collision attributes. Add one (or set its Collision "
										"Attributes to write).");
						}
						if (initialize && (mode == TriggerOverTime || mode == TriggerOverDistance)) {
							Warn(entry.report, "trigger-rate-in-initialize",
								 here + "Over Time/Over Distance count across frames, an Initialize runs once per particle. Use Always here (or "
										"move the block to Update).");
						}
					}
					if (block.def == GRAPH_EFFECT_SET_ATTRIBUTE) {
						const str attribute = Settings(block).Text(GRAPH_EFFECT_ATTRIBUTE_SETTING, "");
						const GraphNodeDefinition* definition = _definitions.Find(*entry.document, block);
						if (!definition || definition->inputs.empty() || (attribute != "lifetime" && attribute != "scale")) {
							continue;
						}
						const Vector<f32> value = ConstantPort(entry, definition->inputs[0].name, bindings, space);
						if (attribute == "lifetime" && value.size() == 1 && value[0] > 1.0e4f) {
							const u64 minutes = static_cast<u64>(value[0] / 60.0f);
							Warn(entry.report, "lifetime-very-long",
								 here + std::format("This lifetime is {:02}h{:02}m.\nA particle with no Set Lifetime block never dies (if that's the "
													"intent).",
													minutes / 60, minutes % 60));
						}
						f32 product = 1.0f;
						for (const f32 component : value) {
							product *= component;
						}
						if (attribute == "scale" && !value.empty() && std::abs(product) < 8.0f * std::numeric_limits<f32>::denorm_min()) {
							Warn(entry.report, "scale-zero", here + "The scale is 0 (these particles are invisible)");
						}
					}
					if (block.def == GRAPH_EFFECT_POSITION_SEQUENTIAL && Settings(block).Choice("Shape", SEQUENTIAL_SHAPES, SequentialLine) == SequentialCircle) {
						const Vector<f32> count = ConstantPort(entry, "Count", bindings, space);
						if (count.size() == 1 && count[0] == 0.0f) {
							Warn(entry.report, "circle-count-zero", here + "A circle with a Count of 0 places no particle");
						}
						const Vector<f32> normal = ConstantPort(entry, "Normal", bindings, space);
						const Vector<f32> up = ConstantPort(entry, "Up", bindings, space);
						if (normal.size() != 3 || up.size() != 3) {
							continue;
						}
						const auto invalid = [](const Vector<f32>& v) { return std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]); };
						if (invalid(normal)) {
							Warn(entry.report, "circle-normal-nan", here + "The Normal is not a number.");
						}
						if (invalid(up)) {
							Warn(entry.report, "circle-up-nan", here + "Up is not a number.");
						}
						const Vec3 cross = Vec3::Cross(Vec3(normal[0], normal[1], normal[2]), Vec3(up[0], up[1], up[2]));
						if (Vec3::Dot(cross, cross) < 10e-5f) {
							Warn(entry.report, "circle-axes-parallel", here + "Normal and Up are parallel (the circle has no orientation).");
						}
					}
				}
			}

			static bool VariesPerParticle(const GraphProgram& program) {
				for (const GraphValue& value : program.values) {
					if (value.op != GraphOp::External) {
						continue;
					}
					const std::string_view name = program.externals[value.payload].name;
					if (name.starts_with(GraphEffectAttributes::CurrentBinding("")) || name.starts_with(GraphEffectAttributes::SourceBinding("")) ||
						name == GRAPH_EFFECT_RANDOM_PARTICLE || name == GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT) {
						return true;
					}
				}
				return false;
			}

			void AppendCameraDepth(GraphEffectKernel kernel, Vector<GraphBinding>& bindings) const {
				std::string_view unavailable;
				if (kernel == GraphEffectKernel::Output) {
					unavailable = "only an Initialize or an Update reads the camera's depth (the simulation reads it before the views)";
				} else if (kernel == GraphEffectKernel::Sort) {
					unavailable = "a Sort Key reads no camera's depth";
				}
				CameraDepthBindings(_cameraOffset, unavailable, bindings);
			}

			static str CpuSystem(u32 initialize) { return std::format("Initialize context {} simulates its system on the CPU", initialize); }

			static void RefuseOnCpu(Vector<GraphBinding>& bindings, u32 initialize) {
				for (GraphBinding& binding : bindings) {
					if (!binding.unavailable.empty()) {
						continue;
					}
					if (binding.name.starts_with("CameraDepth/")) {
						binding.unavailable = "only a system simulated on the GPU reads the camera's depth. " + CpuSystem(initialize);
					} else if (GraphTypes::IsTexture(binding.type)) {
						binding.unavailable = "only a system simulated on the GPU samples a texture. " + CpuSystem(initialize);
					}
				}
			}

			bool CompileInputs(const GraphNode& node, std::span<const std::string_view> ports, GraphProgram& out, std::span<const u32> within) {
				Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
				for (std::string_view port : ports) {
					roots.push_back({node.id, str(port), GraphStageVertex, true, within});
				}
				return CompileRoots(roots, _cpuBindings, out);
			}

			bool Assemble(const GraphProgram& program, str& out, std::string_view system = {}) {
				Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
				if (GraphEffectAssembly::Emit(program, out, diagnostics, system)) {
					return true;
				}
				for (GraphDiagnostic& diagnostic : diagnostics) {
					Adopt(std::move(diagnostic));
				}
				return false;
			}

			u32 Spawner(const GraphContext& context, GraphEffect& out) {
				if (const auto made = _spawnerOf.find(context.node.id); made != _spawnerOf.end()) {
					return made->second;
				}
				GraphEffectSpawner spawner;
				spawner.context = context.node.id;
				bool startLinked = false;
				bool stopLinked = false;
				const auto take = [&](const str& name, Vector<str>& into, std::string_view slot) {
					const SlotFeed feed = Resolve(name);
					into.insert(into.end(), feed.events.begin(), feed.events.end());
					if (!feed.spawners.empty()) {
						Report(context.node.id, std::format("{}s on '{}' (a Spawn of the graph using this subgraph feeds it, a Spawn {}s on an event, "
															"never on another Spawn)",
															slot == GRAPH_EFFECT_START ? "start" : "stop", name, slot == GRAPH_EFFECT_START ? "start" : "stop"));
					}
				};
				for (const GraphFlow& flow : context.flows) {
					const GraphContext* event = ContextById(flow.from);
					if (event && KindOf(event->node.def) == ContextKind::Event) {
						const bool start = flow.slot == GRAPH_EFFECT_START;
						(start ? startLinked : stopLinked) = true;
						take(EventName(*event), start ? spawner.start : spawner.stop, start ? GRAPH_EFFECT_START : GRAPH_EFFECT_STOP);
					}
				}
				if (!startLinked) {
					take(str(GRAPH_EFFECT_ON_PLAY), spawner.start, GRAPH_EFFECT_START);
				}
				if (!stopLinked) {
					take(str(GRAPH_EFFECT_ON_STOP), spawner.stop, GRAPH_EFFECT_STOP);
				}
				Vector<StackBlock> blocks{GetMemoryResource(MemTag::Editor)};
				ExpandBlocks(context, blocks);
				for (const StackBlock& held : blocks) {
					const GraphNode& block = *held.block;
					const str here = Here(held);
					if (!GraphEffectRules::Holds(context.node.def, block.def)) {
						const GraphNodeDefinition* table = GraphNodeLibrary::Find(block.def);
						Report(held.report, here + "is " + (table ? "a " + str(table->name) + " block" : "no block an effect runs") + ", which Spawn context " +
												std::to_string(context.node.id) + " cannot hold");
						continue;
					}
					GraphEffectSpawnBlock entry;
					entry.node = held.report;
					if (block.def == GRAPH_EFFECT_CONSTANT_RATE) {
						entry.kind = GraphEffectSpawnBlock::Kind::ConstantRate;
						constexpr std::string_view PORTS[] = {"Rate"};
						if (!CompileInputs(block, PORTS, entry.program, held.within) || !Assemble(entry.program, entry.assembly)) {
							continue;
						}
					} else if (block.def == GRAPH_EFFECT_BURST) {
						Settings settings(block);
						const u32 repeat = settings.Choice("Repeat", REPEATS, 0);
						const u32 count = settings.Choice("Spawn Mode", RANGE_MODES, 0);
						const u32 delay = settings.Choice("Delay Mode", RANGE_MODES, 0);
						if (repeat >= std::size(REPEATS) || count >= std::size(RANGE_MODES) || delay >= std::size(RANGE_MODES)) {
							Report(held.report, here + "has a setting that isn't one of its options");
							continue;
						}
						entry.kind = repeat == 0 ? GraphEffectSpawnBlock::Kind::SingleBurst : GraphEffectSpawnBlock::Kind::PeriodicBurst;
						const std::string_view ports[] = {count == 0 ? "Count" : "Count Range", delay == 0 ? "Delay" : "Delay Range"};
						if (!CompileInputs(block, ports, entry.program, held.within)) {
							continue;
						}
						const u8 ranged[] = {static_cast<u8>(count), static_cast<u8>(delay)};
						if (!DrawRanges(held.report, GraphCompiler::Identity(block.id, held.within), ranged, entry.program) ||
							!Assemble(entry.program, entry.assembly)) {
							continue;
						}
					} else if (block.def == GRAPH_EFFECT_INCREMENT_STRIP_INDEX) {
						entry.kind = GraphEffectSpawnBlock::Kind::IncrementStripIndex;
						entry.attribute = str(STRIP_INDEX);
						entry.components = 1;
						constexpr std::string_view PORTS[] = {"Strip Max Count"};
						if (!CompileInputs(block, PORTS, entry.program, held.within) || !Assemble(entry.program, entry.assembly)) {
							continue;
						}
					} else {
						str error;
						const GraphNodeDefinition* definition = _definitions.Find(*held.document, block, &error);
						if (!definition) {
							Report(held.report, here + error);
							continue;
						}
						entry.kind = GraphEffectSpawnBlock::Kind::SetAttribute;
						entry.attribute = str(definition->outputs[0].name);
						const GraphEffectAttributeInfo* attribute = GraphEffectAttributes::Find(entry.attribute);
						entry.components = attribute ? ComponentsOf(*attribute) : 1u;
						const GraphCompileRoot root{block.id, entry.attribute, GraphStageVertex, false, held.within};
						if (!CompileRoots({&root, 1}, _cpuBindings, entry.program) || !Assemble(entry.program, entry.assembly)) {
							continue;
						}
					}
					spawner.blocks.push_back(std::move(entry));
				}
				if (LoopProgram(context, spawner.loop)) {
					Assemble(spawner.loop, spawner.loopAssembly);
				}
				out.spawners.push_back(std::move(spawner));
				_spawnerOf[context.node.id] = static_cast<u32>(out.spawners.size() - 1);
				return static_cast<u32>(out.spawners.size() - 1);
			}

			bool DrawRanges(u32 node, u32 identity, std::span<const u8> ranged, GraphProgram& program) {
				GraphProgramBuilder builder(program);
				builder.SetNode(Attributed(node));
				const GraphBinding& stream = *std::find_if(std::begin(EFFECT_BINDINGS), std::end(EFFECT_BINDINGS),
														   [](const GraphBinding& binding) { return binding.name == GRAPH_EFFECT_RANDOM_EFFECT; });
				for (u32 i = 0; i < ranged.size() && i < program.roots.size(); ++i) {
					if (ranged[i] == 0) {
						continue;
					}
					const GraphValueId range = program.roots[i].value;
					const GraphValueId draw = builder.Random(builder.External(stream.type, stream.name, stream.expression), builder.Scalar(0.0f), identity * 8u + i);
					program.roots[i].value = builder.Lerp(builder.Swizzle(range, "x"), builder.Swizzle(range, "y"), draw);
				}
				if (builder.Failed()) {
					Report(node, "couldn't draw from its ranges: " + builder.Error());
					return false;
				}
				return true;
			}

			bool LoopProgram(const GraphContext& context, GraphProgram& out) {
				constexpr std::string_view LOOP_SETTINGS[] = {"Loop Duration", "Loop Count", "Delay Before Loop", "Delay After Loop"};
				constexpr std::string_view LOOP_MODES[] = {"Infinite", "Constant", "Random"};
				constexpr std::string_view DELAY_MODES[] = {"None", "Constant", "Random"};
				Settings settings(context.node);
				u8 modes[4] = {};
				Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
				for (u32 i = 0; i < 4; ++i) {
					const std::span<const std::string_view> options = i < 2 ? std::span<const std::string_view>(LOOP_MODES) : std::span<const std::string_view>(DELAY_MODES);
					const u32 mode = settings.Choice(LOOP_SETTINGS[i], options, 0);
					if (mode > 2) {
						Report(context.node.id, "has a " + str(LOOP_SETTINGS[i]) + " that isn't one of its options");
						return false;
					}
					modes[i] = static_cast<u8>(mode);
					if (mode != 0) {
						roots.push_back({context.node.id, str(LOOP_SETTINGS[i]) + (mode == 2 ? " Range" : ""), GraphStageVertex, true, _within});
					}
				}
				GraphProgram program;
				if (!roots.empty() && !CompileRoots(roots, _cpuBindings, program)) {
					return false;
				}
				u8 ranged[4] = {};
				GraphRoot finals[4];
				GraphProgramBuilder builder(program);
				builder.SetNode(Attributed(context.node.id));
				u32 next = 0;
				for (u32 i = 0; i < 4; ++i) {
					ranged[i] = modes[i] == 2 ? 1 : 0;
					finals[i].stage = GraphStageVertex;
					finals[i].value = modes[i] == 0 ? builder.Scalar(i < 2 ? -1.0f : 0.0f) : program.roots[next++].value;
				}
				program.roots.assign(std::begin(finals), std::end(finals));
				if (!DrawRanges(context.node.id, GraphCompiler::Identity(context.node.id, _within), ranged, program)) {
					return false;
				}
				if (modes[1] == 2) {
					program.roots[1].value = builder.Unary(GraphOp::Truncate, program.roots[1].value);
				}
				out = std::move(program);
				return true;
			}

			void BuildStacks(const GraphContext& initialize, GraphEffect& out) {
				SystemBuild& build = _builds.emplace_back();
				build.initialize = &initialize;
				GraphEffectSystem& system = build.system;
				system.initialize = initialize.node.id;
				Settings settings(initialize.node);
				const u32 dataType = settings.Choice("Data Type", GRAPH_EFFECT_DATA_TYPES, 0);
				if (dataType >= std::size(GRAPH_EFFECT_DATA_TYPES)) {
					Report(initialize.node.id, "has a Data Type that isn't one of its options");
					return;
				}
				const auto whole = [&](std::string_view key, f32 fallback, std::string_view what, f32& out) {
					out = settings.Number(key, fallback);
					if (out >= 1.0f && out <= MAX_CAPACITY && std::floor(out) == out) {
						return true;
					}
					Report(initialize.node.id, std::format("has a {} of {} (a system holds a whole number of {} from 1 to {})", key, out, what,
														   static_cast<u32>(MAX_CAPACITY)));
					return false;
				};
				f32 capacity = 0.0f;
				if (dataType == 1) {
					f32 strips = 0.0f;
					f32 perStrip = 0.0f;
					if (!whole("Strip Capacity", 1.0f, "strips", strips) || !whole("Particle Per Strip Count", 128.0f, "particles a strip", perStrip)) {
						return;
					}
					capacity = strips * perStrip;
					if (capacity > MAX_CAPACITY) {
						Report(initialize.node.id, std::format("holds {} strips of {} particles, past the {} particles a system holds", strips, perStrip,
															   static_cast<u32>(MAX_CAPACITY)));
						return;
					}
					system.strips = static_cast<u32>(strips);
				} else if (!whole("Capacity", 128.0f, "particles", capacity)) {
					return;
				}
				system.capacity = static_cast<u32>(capacity);
				const u32 space = settings.Choice("Space", SPACES, 0);
				if (space >= std::size(SPACES)) {
					Report(initialize.node.id, "has a Space that isn't one of its options");
					return;
				}
				system.world = space == 1;
				system.name = settings.Text(GRAPH_EFFECT_SYSTEM_NAME, "");
				const u32 target = settings.Choice(GRAPH_EFFECT_SIMULATION_TARGET, GRAPH_EFFECT_SIMULATION_TARGETS, 0);
				if (target >= std::size(GRAPH_EFFECT_SIMULATION_TARGETS)) {
					Report(initialize.node.id, "has a Simulation Target that isn't one of its options");
					return;
				}
				system.cpu = target == 1;
				const u32 cpu = system.cpu ? initialize.node.id : 0u;
				if (system.cpu && system.strips > 0) {
					Report(initialize.node.id, "makes strips (only a system simulated on the GPU does). Set its Data Type to Particle or its "
											   "Simulation Target to GPU");
					return;
				}
				if (system.cpu && _feedingEvent.contains(initialize.node.id)) {
					Report(initialize.node.id, "is fed by a GPU Event (only a system simulated on the GPU spawns from GPU events). Set its Simulation "
											   "Target to GPU");
					return;
				}
				system.boundsMode = GraphEffectRules::BoundsModeOf(initialize.node);
				if (system.boundsMode >= std::size(GRAPH_EFFECT_BOUNDS_MODES)) {
					Report(initialize.node.id, "has a Bounds Setting Mode that isn't one of its options");
					return;
				}
				system.measuresBounds = system.boundsMode == GRAPH_EFFECT_BOUNDS_AUTOMATIC ||
										(system.boundsMode == GRAPH_EFFECT_BOUNDS_RECORDED && _options.recordBounds && !GraphEffectRules::ComputesBounds(initialize.node));

				const GraphContext*& update = build.update;
				Vector<const GraphContext*>& outputs = build.outputs;
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) == ContextKind::Update && FedBy(*context, initialize.node.id)) {
						update = context;
					}
				}
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) == ContextKind::Output &&
						(FedBy(*context, initialize.node.id) || (update && FedBy(*context, update->node.id)))) {
						outputs.push_back(context);
					}
				}
				system.update = update ? update->node.id : 0;
				for (const GraphContext* output : outputs) {
					if (IsStripOutput(output->node.def) && system.strips == 0) {
						Report(output->node.id, std::format("draws strips (Initialize context {} makes particles). Set its Data Type to Particle Strip",
															initialize.node.id));
					}
				}
				const auto spawnedBy = [&system](u32 spawner) {
					if (std::find(system.spawners.begin(), system.spawners.end(), spawner) == system.spawners.end()) {
						system.spawners.push_back(spawner);
					}
				};
				for (const GraphFlow& flow : initialize.flows) {
					const GraphContext* source = ContextById(flow.from);
					if (source && KindOf(source->node.def) == ContextKind::Spawn) {
						spawnedBy(Spawner(*source, out));
					} else if (source && KindOf(source->node.def) == ContextKind::Event) {
						const SlotFeed feed = Resolve(EventName(*source));
						system.events.insert(system.events.end(), feed.events.begin(), feed.events.end());
						for (const u32 spawner : feed.spawners) {
							spawnedBy(spawner);
						}
					}
				}

				Stack& initializeStack = build.initializeStack;
				RunBlocks(initialize, system.world, initializeStack, cpu);
				initializeStack.written.insert(str(PARTICLE_ID));
				initializeStack.written.insert(str(SPAWN_INDEX));
				if (system.strips > 0) {
					initializeStack.written.insert(str(SPAWN_INDEX_IN_STRIP));
					BuildStripIndex(initialize, system.world, build.stripIndex);
				}
				Vector<OutputForm>& forms = build.forms;
				forms.resize(outputs.size());
				for (size_t i = 0; i < outputs.size(); ++i) {
					ReadOutputForm(*outputs[i], forms[i]);
				}
				Stack& updateStack = build.updateStack;
				if (update) {
					for (size_t i = 0; i < outputs.size(); ++i) {
						if (forms[i].Flipbook()) {
							updateStack.current[str(GRAPH_EFFECT_FLIPBOOK_SIZE)] =
								SpliceInput(*outputs[i], "Flip Book Size", GraphEffectKernel::Output, system.world, updateStack);
							GraphProgramBuilder builder(updateStack.program);
							const f32 blending = forms[i].blend ? 1.0f : 0.0f;
							updateStack.current[str(GRAPH_EFFECT_FLIPBOOK_BLENDING)] = builder.Constant(T::Bool, {&blending, 1});
							break;
						}
					}
					RunBlocks(*update, system.world, updateStack, cpu);
				}
				Vector<Stack>& outputStacks = build.outputStacks;
				outputStacks.resize(outputs.size());
				for (size_t i = 0; i < outputs.size(); ++i) {
					RunBlocks(*outputs[i], system.world, outputStacks[i]);
					OutputForm& form = forms[i];
					if (form.Flipbook()) {
						form.size = SpliceInput(*outputs[i], "Flip Book Size", GraphEffectKernel::Output, system.world, outputStacks[i]);
					} else if (form.ScaleAndBias()) {
						form.scale = SpliceInput(*outputs[i], "UV Scale", GraphEffectKernel::Output, system.world, outputStacks[i]);
						form.bias = SpliceInput(*outputs[i], "UV Bias", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.shape == OutputShape::Primitive && form.primitive == PRIMITIVE_OCTAGON) {
						form.crop = SpliceInput(*outputs[i], "Crop Factor", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.shape == OutputShape::Line && form.targetOffset) {
						form.offset = SpliceInput(*outputs[i], "Target Offset", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.shape == OutputShape::Strip && form.tiling == 2) {
						form.texCoord = SpliceInput(*outputs[i], "Tex Coord", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.DepthClips()) {
						form.threshold = SpliceInput(*outputs[i], "Alpha Threshold", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.soft) {
						form.softDistance =
							SpliceInput(*outputs[i], "Soft Particle Fade Distance", GraphEffectKernel::Output, system.world, outputStacks[i]);
					}
					if (form.lit) {
						const auto splice = [&](std::string_view port) {
							return SpliceInput(*outputs[i], port, GraphEffectKernel::Output, system.world, outputStacks[i]);
						};
						form.smoothness = splice("Smoothness");
						form.metallic = splice("Metallic");
						if (form.maps[2]) {
							form.normalScale = splice("Normal Scale");
						}
						if (form.maps[3]) {
							form.emissiveScale = splice("Emissive Scale");
						}
						if (!form.UsesBaseColor()) {
							form.baseColor = splice("Base Color");
						}
						if (form.emissive) {
							form.emissiveColor = splice("Emissive Color");
						}
						if (form.bending) {
							form.bendingFactor = splice("Normal Bending Factor");
						}
					}
					if (form.nodeShader) {
						form.propertyValues.assign(form.functions.properties.size(), GRAPH_NO_VALUE);
						for (size_t p = 0; p < form.functions.properties.size(); ++p) {
							const GraphPropertyInfo& property = form.functions.properties[p];
							if (!GraphProperties::IsKeyword(property.kind) && !GraphProperties::IsTexture(property.kind) && !property.hidden) {
								form.propertyValues[p] = SpliceInput(*outputs[i], property.reference, GraphEffectKernel::Output, system.world, outputStacks[i]);
							}
						}
					}
				}
				Vector<Stack>& keyStacks = build.keyStacks;
				keyStacks.resize(outputs.size());
				for (size_t i = 0; i < outputs.size(); ++i) {
					BuildSortKey(*outputs[i], forms[i], system.world, keyStacks[i]);
				}
				const bool authored = system.boundsMode != GRAPH_EFFECT_BOUNDS_AUTOMATIC;
				const bool padded = system.boundsMode != GRAPH_EFFECT_BOUNDS_MANUAL;
				Vector<std::string_view> shown{GetMemoryResource(MemTag::Editor)};
				if (authored) {
					shown.push_back(GRAPH_EFFECT_BOUNDS_CENTER);
					shown.push_back(GRAPH_EFFECT_BOUNDS_SIZE);
				}
				if (padded) {
					shown.push_back(GRAPH_EFFECT_BOUNDS_PADDING);
				}
				if (CompileInputs(initialize.node, shown, system.bounds, _within)) {
					const Vector<GraphRoot> compiled = system.bounds.roots;
					GraphProgramBuilder builder(system.bounds);
					builder.SetNode(Attributed(initialize.node.id));
					constexpr f32 NONE[3] = {0.0f, 0.0f, 0.0f};
					const GraphRoot zero{builder.Constant(T::Float3, NONE), GraphStageVertex};
					system.bounds.roots.clear();
					system.bounds.roots.push_back(authored ? compiled[0] : zero);
					system.bounds.roots.push_back(authored ? compiled[1] : zero);
					system.bounds.roots.push_back(padded ? compiled.back() : zero);
					Assemble(system.bounds, system.boundsAssembly);
				}
				for (const GraphInput& input : initialize.node.inputs) {
					system.boundsWorld = system.boundsWorld || (authored && input.port == GRAPH_EFFECT_BOUNDS_CENTER && input.space == GRAPH_SPACE_WORLD);
				}
				if (Failed()) {
					return;
				}

				if (update) {
					bool sortsByAge = false;
					for (const Stack& key : keyStacks) {
						sortsByAge = sortsByAge || ReadsExternal(key.program, GraphEffectAttributes::CurrentBinding("age"));
					}
					AddImplicit(*update, initializeStack, updateStack, sortsByAge);
					build.implicitReap = Settings(update->node).Toggle("Reap Particles", true) &&
										 (initializeStack.written.contains("lifetime") || updateStack.written.contains("lifetime"));
					const str oldPosition("oldPosition");
					const str read = GraphEffectAttributes::CurrentBinding(oldPosition);
					bool used = false;
					for (const Stack* stack : {&initializeStack, &updateStack}) {
						used = used || stack->read.contains(oldPosition) || stack->written.contains(oldPosition);
					}
					for (size_t i = 0; i < outputs.size(); ++i) {
						used = used || outputStacks[i].read.contains(oldPosition) || ReadsExternal(keyStacks[i].program, read);
					}
					if (used) {
						updateStack.written.insert(oldPosition);
					}
				}
				WarnSystem(build);
			}

			void BuildSystems(GraphEffect& out) {
				HashMap<u32, u32> byInitialize{GetMemoryResource(MemTag::Editor)};
				for (u32 i = 0; i < _builds.size(); ++i) {
					byInitialize[_builds[i].initialize->node.id] = i;
				}
				for (u32 i = 0; i < _builds.size(); ++i) {
					const auto parent = _parentOf.find(_builds[i].initialize->node.id);
					const auto found = parent == _parentOf.end() ? byInitialize.end() : byInitialize.find(parent->second);
					if (found != byInitialize.end()) {
						_builds[i].parent = found->second;
						_builds[found->second].children.push_back(i);
					}
				}
				Vector<u32> order{GetMemoryResource(MemTag::Editor)};
				HashSet<u32> placed{GetMemoryResource(MemTag::Editor)};
				for (bool progressed = true; progressed && order.size() < _builds.size();) {
					progressed = false;
					for (u32 i = 0; i < _builds.size(); ++i) {
						const u32 parent = _builds[i].parent;
						if (!placed.contains(i) && (parent == SystemBuild::NONE || placed.contains(parent))) {
							order.push_back(i);
							placed.insert(i);
							progressed = true;
						}
					}
				}
				for (auto at = order.rbegin(); at != order.rend(); ++at) {
					FinishStores(_builds[*at]);
				}
				if (Failed()) {
					return;
				}
				const u32 base = static_cast<u32>(out.systems.size());
				for (u32 at = 0; at < order.size(); ++at) {
					_builds[order[at]].index = base + at;
				}
				for (const u32 i : order) {
					EmitSystem(_builds[i], out);
				}
			}

			void CompileSystemSubgraphs(GraphEffect& out) {
				for (const GraphContext* context : _contexts) {
					if (KindOf(context->node.def) != ContextKind::Subgraph) {
						continue;
					}
					const u32 id = context->node.id;
					str error;
					const GraphNodeDefinition* definition = _definitions.Find(_document, context->node, &error);
					if (!definition || !definition->subgraph) {
						Report(id, error.empty() ? str("has no system subgraph") : error);
						continue;
					}
					const GraphSubgraph& subgraph = *definition->subgraph;
					if (!context->blocks.empty()) {
						Report(id, "holds blocks (a system subgraph's context runs none, its systems' contexts hold them)");
						continue;
					}
					bool cycle = false;
					for (size_t i = 0; i < _systemChain.size() && !cycle; ++i) {
						if (_systemChain[i].asset != subgraph.asset) {
							continue;
						}
						str chain;
						for (size_t j = i; j < _systemChain.size(); ++j) {
							chain += "'" + _systemChain[j].name + "' -> ";
						}
						Report(id, "uses subgraph '" + subgraph.name + "', which uses itself: " + chain + "'" + subgraph.name + "'");
						cycle = true;
					}
					if (cycle) {
						continue;
					}
					if (_systemChain.size() >= MAX_SUBGRAPH_DEPTH) {
						Report(id, "uses subgraph '" + subgraph.name + "' (nests subgraphs more than " + std::to_string(MAX_SUBGRAPH_DEPTH) + " deep)");
						continue;
					}
					HashMap<str, SlotFeed> slots{GetMemoryResource(MemTag::Editor)};
					for (const std::string_view slot : subgraph.flowSlots) {
						SlotFeed feed;
						bool linked = false;
						for (const GraphFlow& flow : context->flows) {
							const GraphContext* source = flow.slot == slot ? ContextById(flow.from) : nullptr;
							if (source && KindOf(source->node.def) == ContextKind::Event) {
								const SlotFeed named = Resolve(EventName(*source));
								feed.events.insert(feed.events.end(), named.events.begin(), named.events.end());
								feed.spawners.insert(feed.spawners.end(), named.spawners.begin(), named.spawners.end());
								linked = true;
							} else if (source && KindOf(source->node.def) == ContextKind::Spawn) {
								feed.spawners.push_back(Spawner(*source, out));
								linked = true;
							}
						}
						slots[str(slot)] = linked ? std::move(feed) : Resolve(str(slot));
					}
					if (Failed()) {
						return;
					}
					EffectCompile inner(*this, subgraph, *context, std::move(slots));
					if (!inner.CompileDocument(out)) {
						_failed = true;
					}
				}
			}

			void FinishStores(SystemBuild& build) {
				HashSet<str> readLater{GetMemoryResource(MemTag::Editor)};
				for (size_t i = 0; i < build.outputs.size(); ++i) {
					FinishOutput(build.outputStacks[i], build.forms[i]);
					AttributeReadsOf(build.outputStacks[i].program, readLater);
					AttributeReadsOf(build.keyStacks[i].program, readLater);
					if (ReadsExternal(build.outputStacks[i].program, GRAPH_EFFECT_STRIP_PREVIOUS) ||
						ReadsExternal(build.outputStacks[i].program, GRAPH_EFFECT_STRIP_NEXT)) {
						readLater.insert("position");
					}
				}
				for (const u32 child : build.children) {
					readLater.insert(_builds[child].sourceReads.begin(), _builds[child].sourceReads.end());
				}
				Store(build.system, build.initializeStack, build.updateStack, readLater);
				if (Failed()) {
					return;
				}
				if (build.system.cpu) {
					FinishCpuProgram(build.initializeStack, build.system, true, false);
					FinishCpuProgram(build.updateStack, build.system, false, build.implicitReap);
					return;
				}
				FinishKernel(build.initializeStack, build.system, true, build.initializeKeeps);
				FinishKernel(build.updateStack, build.system, false, build.updateKeeps);
				ReadsOf(build.initializeStack.program, "SourceAttribute/", build.sourceReads);
			}

			void EmitSystem(SystemBuild& build, GraphEffect& out) {
				GraphEffectSystem& system = build.system;
				system.parent = build.parent == SystemBuild::NONE ? GRAPH_EFFECT_NO_BINDING : _builds[build.parent].index;
				if (system.cpu) {
					EmitCpu(build);
				} else {
					EmitInitialize(build, out);
					EmitUpdate(build);
				}
				for (size_t i = 0; i < build.outputs.size(); ++i) {
					EmitOutput(build.outputStacks[i], *build.outputs[i], system, build.forms[i], out);
					EmitSortKernel(build.keyStacks[i], *build.outputs[i], system, build.forms[i]);
				}
				if (!Failed()) {
					out.systems.push_back(std::move(system));
				}
			}

			void BuildStripIndex(const GraphContext& initialize, bool world, GraphProgram& out) {
				Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
				GraphEffectTarget::KernelBindings(GraphEffectKernel::Initialize, bindings, world);
				for (GraphBinding& binding : bindings) {
					for (const std::string_view name : {STRIP_INDEX, PARTICLE_INDEX_IN_STRIP, PARTICLE_COUNT_IN_STRIP, SPAWN_INDEX_IN_STRIP}) {
						if (binding.name == GraphEffectAttributes::CurrentBinding(name)) {
							binding.unavailable = "decides the spawn's strip (doesnt read strip's attributes)";
						}
					}
				}
				bindings.insert(bindings.end(), _portTextures.begin(), _portTextures.end());
				bindings.insert(bindings.end(), _portMeshes.begin(), _portMeshes.end());
				AppendCameraDepth(GraphEffectKernel::Initialize, bindings);
				const GraphCompileRoot root{initialize.node.id, "Strip Index", GraphStageVertex, true, _within};
				if (CompileRoots({&root, 1}, bindings, out, world ? GraphSpace::World : GraphSpace::Local)) {
					RunPasses(out, initialize.node.id);
				}
			}

			void AddImplicit(const GraphContext& update, Stack& initialize, Stack& stack, bool ageReadElsewhere) {
				Settings settings(update.node);
				GraphProgramBuilder builder(stack.program);
				builder.SetNode(0);
				const GraphBinding& time = EFFECT_BINDINGS[0];
				const GraphValueId delta = builder.Swizzle(builder.External(time.type, time.name, time.expression), "w");
				auto attribute = [](std::string_view name) { return *GraphEffectAttributes::Find(name); };
				auto writes = [&](std::string_view name) { return initialize.written.contains(str(name)) || stack.written.contains(str(name)); };
				auto reads = [&](std::string_view name) {
					const str binding = GraphEffectAttributes::CurrentBinding(name);
					return ReadsExternal(initialize.program, binding) || ReadsExternal(stack.program, binding) || (ageReadElsewhere && name == "age");
				};
				if (settings.Toggle("Update Position", true) && writes("velocity")) {
					const GraphValueId moved = builder.Add(stack.Current(attribute("position")), builder.Multiply(stack.Current(attribute("velocity")), delta));
					stack.Write(attribute("position"), moved);
				}
				const bool lifetime = writes("lifetime");
				if (settings.Toggle("Age Particles", true) && (lifetime || writes("age") || reads("age") || reads("lifetime"))) {
					stack.Write(attribute("age"), builder.Add(stack.Current(attribute("age")), delta));
				}
				if (settings.Toggle("Reap Particles", true) && lifetime) {
					const GraphValueId expired = builder.Binary(GraphOp::Less, stack.Current(attribute("lifetime")), stack.Current(attribute("age")));
					const GraphValueId alive = builder.Binary(GraphOp::LogicalAnd, stack.Current(attribute("alive")), builder.Unary(GraphOp::LogicalNot, expired));
					stack.Write(attribute("alive"), alive);
				}
				if (builder.Failed()) {
					Report(update.node.id, "couldn't add its implicit blocks: " + builder.Error());
				}
			}

			enum class OutputShape : u8 {
				Primitive,
				Mesh,
				Point,
				Line,
				Strip
			};
			static constexpr u32 PRIMITIVE_TRIANGLE = 0;
			static constexpr u32 PRIMITIVE_QUAD = 1;
			static constexpr u32 PRIMITIVE_OCTAGON = 2;

			struct OutputForm {
				OutputShape shape = OutputShape::Primitive;
				u32 primitive = PRIMITIVE_QUAD;
				bool targetOffset = true;
				bool nativeLines = false;
				u32 map = 3;
				u32 uv = 0;
				bool blend = false;
				u32 blendMode = 1;
				bool clipping = false;
				bool soft = false;
				GraphEffectSort sort = GraphEffectSort::None;
				bool revert = false;
				u32 cullMode = 0;
				u32 zWriteMode = 0;
				u32 zTestMode = 0;
				GraphValueId size = GRAPH_NO_VALUE;
				GraphValueId scale = GRAPH_NO_VALUE;
				GraphValueId bias = GRAPH_NO_VALUE;
				GraphValueId crop = GRAPH_NO_VALUE;
				GraphValueId offset = GRAPH_NO_VALUE;
				GraphValueId threshold = GRAPH_NO_VALUE;
				GraphValueId softDistance = GRAPH_NO_VALUE;
				bool castShadows = false;
				bool lit = false;
				u32 colorMode = 1;
				bool emissive = false;
				bool maps[4] = {};
				u32 smoothnessSource = 0;
				bool doubleSided = false;
				bool receiveShadows = true;
				bool bending = false;
				GraphValueId smoothness = GRAPH_NO_VALUE;
				GraphValueId metallic = GRAPH_NO_VALUE;
				GraphValueId normalScale = GRAPH_NO_VALUE;
				GraphValueId emissiveScale = GRAPH_NO_VALUE;
				GraphValueId baseColor = GRAPH_NO_VALUE;
				GraphValueId emissiveColor = GRAPH_NO_VALUE;
				GraphValueId bendingFactor = GRAPH_NO_VALUE;
				bool nodeShader = false;
				str graphName;
				GraphSurfaceFunctions functions;
				RHI::BlendMode graphBlend = RHI::BlendMode::Alpha;
				Vector<str> keywords{GetMemoryResource(MemTag::Editor)};
				Vector<GraphValueId> propertyValues{GetMemoryResource(MemTag::Editor)};
				u32 tiling = 0;
				bool swapUV = false;
				GraphValueId texCoord = GRAPH_NO_VALUE;

				bool Opaque() const { return blendMode == 3; }
				bool DepthProgram() const { return true; }
				bool DepthClips() const { return clipping || (castShadows && !Opaque()); }
				bool UsesBaseColor() const { return colorMode == 1 || colorMode == 3; }
				bool ColorIsEmissive() const { return colorMode >= 2; }
				bool Emits() const { return maps[3] || ColorIsEmissive() || emissive; }

				std::array<GraphValueId, 12> Extras() const {
					return {crop, offset, threshold, softDistance, smoothness, metallic, normalScale, emissiveScale, baseColor, emissiveColor, bendingFactor, texCoord};
				}

				bool Sampled() const { return map != 0; }
				bool Flipbook() const { return map != 0 && uv == 1; }
				bool ScaleAndBias() const { return map != 0 && uv == 2; }
				bool SoftwareLine() const { return shape == OutputShape::Line && !nativeLines; }

				u32 Vertices() const {
					switch (shape) {
					case OutputShape::Primitive:
						return primitive == PRIMITIVE_TRIANGLE ? 3u : (primitive == PRIMITIVE_QUAD ? 6u : 24u);
					case OutputShape::Mesh:
						return 0;
					case OutputShape::Point:
						return 1;
					case OutputShape::Line:
						return nativeLines ? 2u : 6u;
					case OutputShape::Strip:
						return 6;
					}
					return 0;
				}

				std::string_view Name() const {
					switch (shape) {
					case OutputShape::Primitive:
						return GRAPH_EFFECT_PRIMITIVES[primitive];
					case OutputShape::Mesh:
						return "Mesh";
					case OutputShape::Point:
						return "Point";
					case OutputShape::Line:
						return "Line";
					case OutputShape::Strip:
						return "Strip Quad";
					}
					return {};
				}
			};

			struct SystemBuild {
				static constexpr u32 NONE = ~0u;
				const GraphContext* initialize = nullptr;
				const GraphContext* update = nullptr;
				Vector<const GraphContext*> outputs{GetMemoryResource(MemTag::Editor)};
				GraphEffectSystem system;
				Stack initializeStack;
				Stack updateStack;
				GraphProgram stripIndex;
				Vector<OutputForm> forms{GetMemoryResource(MemTag::Editor)};
				Vector<Stack> outputStacks{GetMemoryResource(MemTag::Editor)};
				Vector<Stack> keyStacks{GetMemoryResource(MemTag::Editor)};
				Vector<const GraphEffectStored*> initializeKeeps{GetMemoryResource(MemTag::Editor)};
				Vector<const GraphEffectStored*> updateKeeps{GetMemoryResource(MemTag::Editor)};
				bool implicitReap = false;
				u32 parent = NONE;
				Vector<u32> children{GetMemoryResource(MemTag::Editor)};
				HashSet<str> sourceReads{GetMemoryResource(MemTag::Editor)};
				u32 index = 0;
			};

			void WarnSystem(const SystemBuild& build) {
				using namespace GraphEffectBlocks;
				const GraphContext& initialize = *build.initialize;
				const GraphEffectSystem& system = build.system;
				const u32 id = initialize.node.id;
				if (system.capacity > 1000000u) {
					Warn(id, "capacity-over-a-million", "A capacity over a million particles can make this system slow");
				}
				if (system.boundsMode == GRAPH_EFFECT_BOUNDS_RECORDED && GraphEffectRules::ComputesBounds(initialize.node)) {
					Warn(id, "recording-overridden", "Operators set this system's bounds (a recording leaves them alone).");
				}
				if (system.boundsMode == GRAPH_EFFECT_BOUNDS_AUTOMATIC) {
					Warn(id, "automatic-bounds-culling", "Automatic bounds switch this effect's Culling Flags to 'Always simulate and update bounds'.");
				}
				const bool fedStripIndex = std::any_of(initialize.node.inputs.begin(), initialize.node.inputs.end(),
													   [](const GraphInput& input) { return input.port == "Strip Index" && input.fromNode != 0; });
				if (system.strips > 0 && fedStripIndex && VariesPerParticle(build.stripIndex) && build.initializeStack.read.contains(str(PARTICLE_COUNT_IN_STRIP))) {
					Warn(id, "strip-count-needs-constant-index",
						 "\"Get Particle Count In Strip\" and \"Get Ratio Over Strip\" are only right here when every particle has the same \"Strip "
						 "Index\".");
				}
				const auto written = [&](std::string_view attribute) {
					return build.initializeStack.written.contains(str(attribute)) || build.updateStack.written.contains(str(attribute));
				};
				if (build.update) {
					const bool flipbookOutput = std::any_of(build.forms.begin(), build.forms.end(), [](const OutputForm& form) { return form.Flipbook(); });
					Vector<StackBlock> blocks{GetMemoryResource(MemTag::Editor)};
					ExpandBlocks(*build.update, blocks);
					for (const StackBlock& entry : blocks) {
						if (entry.block->def != GRAPH_EFFECT_FLIPBOOK_PLAYER) {
							continue;
						}
						Settings settings(*entry.block);
						const u32 mode = settings.Choice("Mode", FLIPBOOK_MODES, ByFrameRate);
						const bool ranged = mode == ByCycles || settings.Toggle("Use Custom Range", false);
						const u32 range = settings.Choice("Animation Range", ANIMATION_RANGES, EntireFlipbook);
						if (mode == ByCycles && !written("lifetime")) {
							Warn(entry.report, "flipbook-cycles-need-lifetime", WarnHere(entry) + "Cycles mode needs particles with a lifetime");
						}
						if (ranged && range <= FlipbookColumn && !flipbookOutput) {
							Warn(entry.report, "flipbook-needs-output", WarnHere(entry) + "No output of this system draws a flipbook. Set an output's UV Mode to Flipbook");
						}
					}
				}
				for (size_t i = 0; i < build.outputs.size(); ++i) {
					const u32 output = build.outputs[i]->node.id;
					const OutputForm& form = build.forms[i];
					const HashSet<str>& writes = build.outputStacks[i].written;
					if (form.shape == OutputShape::Strip && writes.contains("position")) {
						Warn(output, "strip-output-writes-position", "Writing Position in a strip output can draw the strip in the wrong place");
					}
					if (system.boundsMode != GRAPH_EFFECT_BOUNDS_MANUAL) {
						const bool reshaped = std::any_of(writes.begin(), writes.end(), [](const str& name) {
							return name.find("size") != str::npos || name.find("position") != str::npos || name.find("scale") != str::npos ||
								   name.find("pivot") != str::npos;
						});
						if (reshaped) {
							Warn(output, "recorded-bounds-may-miss",
								 "Recorded bounds measured from Position/Size in Update (changing them here can leave particles outside). "
								 "Add padding to cover the difference.");
						}
						if (form.shape == OutputShape::Mesh) {
							Warn(output, "recorded-bounds-may-miss",
								 "Recorded bounds can't see this output's mesh size (may be too small/too large). Add padding to cover the "
								 "difference.");
						}
					}
					if (form.sort == GraphEffectSort::None) {
						continue;
					}
					HashSet<str> sorted{GetMemoryResource(MemTag::Editor)};
					if (form.sort == GraphEffectSort::Custom) {
						ReadsOf(build.keyStacks[i].program, GraphEffectAttributes::CurrentBinding(""), sorted);
					} else {
						sorted.insert(form.sort == GraphEffectSort::YoungestInFront ? str("age") : str("position"));
					}
					if (std::any_of(sorted.begin(), sorted.end(), [&writes](const str& name) { return writes.contains(name); })) {
						Warn(output, "sort-before-output",
							 "Sorting runs in Update, before this output's blocks (what they write here doesn't change the order).");
					}
					if (form.sort == GraphEffectSort::YoungestInFront && !written("age")) {
						Warn(output, "sort-without-age", "Youngest in Front sorts by age (nothing in this system sets or advances it).");
					}
				}
			}

			void ReadNodeShaderForm(const GraphContext& context, OutputForm& out) {
				Settings settings(context.node);
				const u32 id = context.node.id;
				out.nodeShader = true;
				out.map = 0;
				out.shape = context.node.def == GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH	? OutputShape::Mesh
							: context.node.def == GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP ? OutputShape::Strip
																						: OutputShape::Primitive;
				ReadStripForm(context, out);
				out.castShadows = settings.Toggle("Cast Shadows", false);
				out.clipping = settings.Toggle("Use Alpha Clipping", false);
				out.receiveShadows = settings.Toggle("Receive Shadows", true);
				if (out.shape == OutputShape::Primitive) {
					out.primitive = settings.Choice("Primitive Type", GRAPH_EFFECT_PRIMITIVES, PRIMITIVE_QUAD);
					if (out.primitive >= std::size(GRAPH_EFFECT_PRIMITIVES)) {
						Report(id, "has a Primitive Type that isn't one of its options");
						out.primitive = PRIMITIVE_QUAD;
					}
				}
				Guid asset;
				const GraphEffectShading::Held held = GraphEffectShading::HeldNodeShader(context.node, asset);
				if (held == GraphEffectShading::Held::None) {
					Report(id, "has no Node Shader. Assign one with Support Effects on");
					return;
				}
				const GraphShadingTarget* target = GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE);
				if (!target) {
					const str name = held == GraphEffectShading::Held::Default ? str(GraphEffectShading::DEFAULT_NAME) : GraphSubgraphs::NameOf(asset);
					Report(id, "shades with node shader '" + name + "' (Node Shader is off). Turn it on in the Package Manager");
					return;
				}
				str missing;
				const GraphReadDocument* graph = held == GraphEffectShading::Held::Default ? &_definitions.DefaultNodeShader() : _definitions.NodeShader(asset, &missing);
				if (!graph || !graph->error.empty()) {
					Report(id, graph ? "shades with " + str(GraphEffectShading::DEFAULT_NAME) + ", which " + graph->error : missing);
					return;
				}
				out.graphName = graph->name;
				if (graph->document.type != GRAPH_SHADER_DOCUMENT_TYPE) {
					Report(id, "shades with '" + graph->name + "', a " + graph->document.type + " document (not a node shader)");
					return;
				}
				if (str why; !target->canShadeEffects(graph->document, graph->name, why)) {
					Report(id, why);
					return;
				}
				Vector<GraphDiagnostic> said{GetMemoryResource(MemTag::Editor)};
				if (!target->generateFunctions(graph->document, out.functions, said, &_definitions)) {
					for (const GraphDiagnostic& diagnostic : said) {
						Report(id, diagnostic.node != 0 ? std::format("shades with '{}' (its node {} {})", graph->name, diagnostic.node, diagnostic.message)
														: std::format("shades with '{}' ({})", graph->name, diagnostic.message));
					}
					return;
				}

				out.graphBlend = out.functions.blend;
				switch (out.graphBlend) {
				case RHI::BlendMode::Opaque:
					out.blendMode = 3;
					break;
				case RHI::BlendMode::Alpha:
					out.blendMode = 1;
					break;
				case RHI::BlendMode::Premultiplied:
					out.blendMode = 2;
					break;
				default:
					out.blendMode = 0;
					break;
				}
				const u32 sort = settings.Choice("Sort", GRAPH_EFFECT_SORTS, 0);
				const u32 mode = settings.Choice("Sort Mode", GRAPH_EFFECT_SORT_MODES, 0);
				if (sort >= std::size(GRAPH_EFFECT_SORTS) || mode >= std::size(GRAPH_EFFECT_SORT_MODES)) {
					Report(id, "has a setting that isn't one of its options");
				} else {
					constexpr u32 SORT_AUTO = 0;
					constexpr u32 SORT_ON = 2;
					if (sort == SORT_ON || (sort == SORT_AUTO && (out.blendMode == 1 || out.blendMode == 2))) {
						out.sort = static_cast<GraphEffectSort>(mode + 1);
						out.revert = settings.Toggle("Revert Sorting", false);
					}
				}
				for (const GraphPropertyInfo& property : out.functions.properties) {
					if (!GraphProperties::IsKeyword(property.kind)) {
						continue;
					}
					const str& name = property.name.empty() ? property.reference : property.name;
					const bool own = std::any_of(graph->document.properties.begin(), graph->document.properties.end(),
												 [&](const GraphProperty& held) { return held.reference == property.reference; });
					if (own && IsOwnSetting(context.node.def, name)) {
						Report(id, "shades with '" + graph->name + "' (its keyword '" + name + "' is the name of one of the output's settings)");
						continue;
					}
					if (property.kind == GraphPropertyKind::BooleanKeyword) {
						if (own ? settings.Toggle(name, property.defaultValue[0] != 0.0f) : property.defaultValue[0] != 0.0f) {
							out.keywords.push_back(property.reference);
						}
						continue;
					}
					Vector<std::string_view> entries{GetMemoryResource(MemTag::Editor)};
					for (const GraphKeywordEntry& entry : property.entries) {
						entries.push_back(entry.name);
					}
					const u32 fallback = static_cast<u32>(property.defaultValue[0]);
					const u32 chosen = own ? settings.Choice(name, entries, fallback) : fallback;
					if (chosen >= property.entries.size()) {
						Report(id, "has a " + name + " that isn't one of its entries");
						continue;
					}
					out.keywords.push_back(GraphProperties::EntryKeyword(property, chosen));
				}
				for (const GraphPropertyInfo& property : out.functions.properties) {
					if (!GraphProperties::IsKeyword(property.kind) && !property.hidden && FindTablePort(context.node.def, property.reference)) {
						Report(id, "shades with '" + graph->name + "' (its property '" + property.reference + "' is the name of one of the output's ports)");
					}
				}
			}

			void ReadStripForm(const GraphContext& context, OutputForm& out) {
				if (out.shape != OutputShape::Strip) {
					return;
				}
				Settings settings(context.node);
				out.tiling = settings.Choice("Tiling Mode", GRAPH_EFFECT_STRIP_TILING_MODES, 0);
				if (out.tiling >= std::size(GRAPH_EFFECT_STRIP_TILING_MODES)) {
					Report(context.node.id, "has a Tiling Mode that isn't one of its options");
					out.tiling = 0;
				}
				out.swapUV = settings.Toggle("Swap UV", false);
			}

			static bool IsOwnSetting(std::string_view def, std::string_view key) {
				const GraphNodeDefinition* table = GraphNodeLibrary::Find(def);
				return key == "Receive Shadows" || (table && std::any_of(table->settings.begin(), table->settings.end(),
																		 [key](const GraphSettingDefinition& setting) { return setting.key == key; }));
			}

			static bool FindTablePort(std::string_view def, std::string_view name) {
				const GraphNodeDefinition* table = GraphNodeLibrary::Find(def);
				return table && GraphNodeLibrary::FindInput(*table, name) >= 0;
			}

			void ReadOutputForm(const GraphContext& context, OutputForm& out) {
				if (GraphEffectShading::IsNodeShaderOutput(context.node.def)) {
					ReadNodeShaderForm(context, out);
					return;
				}
				constexpr std::string_view MAPS[] = {"None", "Color", "Alpha", "ColorAndAlpha"};
				constexpr std::string_view UV_MODES[] = {"Default", "Flipbook", "ScaleAndBias"};
				Settings settings(context.node);
				const std::string_view def = context.node.def;
				out.lit = def == GRAPH_EFFECT_OUTPUT_LIT || def == GRAPH_EFFECT_OUTPUT_LIT_MESH;
				out.shape = def == GRAPH_EFFECT_OUTPUT_MESH || def == GRAPH_EFFECT_OUTPUT_LIT_MESH ? OutputShape::Mesh
							: def == GRAPH_EFFECT_OUTPUT_POINT									   ? OutputShape::Point
							: def == GRAPH_EFFECT_OUTPUT_LINE									   ? OutputShape::Line
							: def == GRAPH_EFFECT_OUTPUT_STRIP									   ? OutputShape::Strip
																								   : OutputShape::Primitive;
				ReadStripForm(context, out);
				out.castShadows = settings.Toggle("Cast Shadows", false);
				if (out.shape == OutputShape::Primitive) {
					out.primitive = settings.Choice("Primitive Type", GRAPH_EFFECT_PRIMITIVES, PRIMITIVE_QUAD);
					if (out.primitive >= std::size(GRAPH_EFFECT_PRIMITIVES)) {
						Report(context.node.id, "has a Primitive Type that isn't one of its options");
						out.primitive = PRIMITIVE_QUAD;
					}
				}
				out.targetOffset = settings.Toggle("Use Target Offset", true);
				out.nativeLines = settings.Toggle("Use Native Lines", false);
				out.blendMode = settings.Choice("Blend Mode", BLEND_MODES, 1);
				const u32 sort = settings.Choice("Sort", GRAPH_EFFECT_SORTS, 0);
				const u32 mode = settings.Choice("Sort Mode", GRAPH_EFFECT_SORT_MODES, 0);
				if (out.blendMode >= std::size(BLEND_MODES) || sort >= std::size(GRAPH_EFFECT_SORTS) || mode >= std::size(GRAPH_EFFECT_SORT_MODES)) {
					Report(context.node.id, "has a setting that isn't one of its options");
					out.blendMode = 1;
				} else {
					constexpr u32 OPAQUE = 3;
					constexpr u32 SORT_AUTO = 0;
					constexpr u32 SORT_ON = 2;
					const bool alphaBlended = out.blendMode == 1 || out.blendMode == 2;
					if (sort == SORT_ON || (sort == SORT_AUTO && alphaBlended)) {
						out.sort = static_cast<GraphEffectSort>(mode + 1);
						out.revert = settings.Toggle("Revert Sorting", false);
					}
					out.soft = out.blendMode != OPAQUE && settings.Toggle("Use Soft Particle", false);
				}
				out.clipping = settings.Toggle("Use Alpha Clipping", false);
				out.cullMode = settings.Choice(GRAPH_EFFECT_CULL_MODE, GRAPH_EFFECT_CULL_MODES, 0);
				out.zWriteMode = settings.Choice(GRAPH_EFFECT_Z_WRITE_MODE, GRAPH_EFFECT_Z_WRITE_MODES, 0);
				out.zTestMode = settings.Choice(GRAPH_EFFECT_Z_TEST_MODE, GRAPH_EFFECT_Z_TEST_MODES, 0);
				if (out.cullMode >= std::size(GRAPH_EFFECT_CULL_MODES) || out.zWriteMode >= std::size(GRAPH_EFFECT_Z_WRITE_MODES) ||
					out.zTestMode >= std::size(GRAPH_EFFECT_Z_TEST_MODES)) {
					Report(context.node.id, "has a render state that isn't one of its options");
					out.cullMode = out.zWriteMode = out.zTestMode = 0;
				}
				if (out.shape == OutputShape::Point || out.shape == OutputShape::Line) {
					out.map = 0;
					return;
				}
				out.map = settings.Choice("Use Base Color Map", MAPS, 3);
				out.uv = settings.Choice("UV Mode", UV_MODES, 0);
				out.blend = settings.Toggle("Flipbook Blend Frames", false);
				if (out.map >= std::size(MAPS) || out.uv >= std::size(UV_MODES)) {
					Report(context.node.id, "has a setting that isn't one of its options");
					out.map = 0;
				}
				if (!out.lit) {
					return;
				}
				out.colorMode = settings.Choice("Color Mode", GRAPH_EFFECT_COLOR_MODES, 1);
				out.smoothnessSource = settings.Choice("Smoothness Source", GRAPH_EFFECT_SMOOTHNESS_SOURCES, 0);
				if (out.colorMode >= std::size(GRAPH_EFFECT_COLOR_MODES) || out.smoothnessSource >= std::size(GRAPH_EFFECT_SMOOTHNESS_SOURCES)) {
					Report(context.node.id, "has a setting that isn't one of its options");
					out.colorMode = 1;
					out.smoothnessSource = 0;
				}
				out.emissive = !out.ColorIsEmissive() && settings.Toggle("Use Emissive", false);
				constexpr std::string_view MAP_TOGGLES[] = {"Use Occlusion Map", "Use Metallic Map", "Use Normal Map", "Use Emissive Map"};
				for (u32 map = 0; map < std::size(MAP_TOGGLES); ++map) {
					out.maps[map] = settings.Toggle(MAP_TOGGLES[map], false);
				}
				constexpr u32 METALLIC_ALPHA = 1;
				constexpr u32 ALBEDO_ALPHA = 2;
				if ((out.smoothnessSource == ALBEDO_ALPHA && !out.Sampled()) || (out.smoothnessSource == METALLIC_ALPHA && !out.maps[1])) {
					out.smoothnessSource = 0;
				}
				out.doubleSided = settings.Toggle("Double Sided", false);
				out.receiveShadows = settings.Toggle("Receive Shadows", true);
				out.bending = out.shape == OutputShape::Primitive && settings.Toggle("Normal Bending", false);
			}

			GraphValueId SpliceInput(const GraphContext& context, std::string_view port, GraphEffectKernel kernel, bool world, Stack& stack) {
				Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
				GraphEffectTarget::KernelBindings(kernel, bindings, world, kernel == GraphEffectKernel::Output && IsStripOutput(context.node.def));
				const size_t textures = bindings.size();
				bindings.insert(bindings.end(), _portTextures.begin(), _portTextures.end());
				for (size_t i = textures; kernel == GraphEffectKernel::Sort && i < bindings.size(); ++i) {
					bindings[i].unavailable = "a Sort Key samples no texture (a sort's kernel doesn't bind one)";
				}
				bindings.insert(bindings.end(), _portMeshes.begin(), _portMeshes.end());
				AppendCameraDepth(kernel, bindings);
				const GraphCompileRoot root{context.node.id, str(port), GraphStageVertex, true, _within};
				GraphProgram program;
				Vector<GraphValueId> values{GetMemoryResource(MemTag::Editor)};
				if (!CompileRoots({&root, 1}, bindings, program, world ? GraphSpace::World : GraphSpace::Local) ||
					!GraphEffectSplice::Append(stack.program, program, stack.current, values) || values.empty()) {
					Report(context.node.id, "couldn't read its " + str(port));
					return GRAPH_NO_VALUE;
				}
				return values[0];
			}

			void FinishOutput(Stack& stack, const OutputForm& form) {
				stack.program.roots.clear();
				for (std::string_view name : OutputReads(form)) {
					stack.program.roots.push_back({stack.Current(*GraphEffectAttributes::Find(name)), GraphStageVertex});
				}
				if (form.Flipbook()) {
					stack.program.roots.push_back({stack.Current(*GraphEffectAttributes::Find("texIndex")), GraphStageVertex});
					stack.program.roots.push_back({stack.Current(*GraphEffectAttributes::Find("texIndexBlend")), GraphStageVertex});
					stack.program.roots.push_back({form.size, GraphStageVertex});
				} else if (form.ScaleAndBias()) {
					stack.program.roots.push_back({form.scale, GraphStageVertex});
					stack.program.roots.push_back({form.bias, GraphStageVertex});
				}
				for (GraphValueId extra : form.Extras()) {
					if (extra != GRAPH_NO_VALUE) {
						stack.program.roots.push_back({extra, GraphStageVertex});
					}
				}
				for (GraphValueId value : form.propertyValues) {
					if (value != GRAPH_NO_VALUE) {
						stack.program.roots.push_back({value, GraphStageVertex});
					}
				}
				RunPasses(stack.program, 0);
			}

			void BuildSortKey(const GraphContext& context, const OutputForm& form, bool world, Stack& stack) {
				if (form.sort == GraphEffectSort::None) {
					return;
				}
				GraphValueId key = GRAPH_NO_VALUE;
				switch (form.sort) {
				case GraphEffectSort::YoungestInFront:
					key = stack.Current(*GraphEffectAttributes::Find("age"));
					break;
				case GraphEffectSort::Custom:
					key = SpliceInput(context, "Sort Key", GraphEffectKernel::Sort, world, stack);
					break;
				default:
					key = stack.Current(*GraphEffectAttributes::Find("position"));
					break;
				}
				stack.program.roots.clear();
				stack.program.roots.push_back({key, GraphStageVertex});
				RunPasses(stack.program, 0);
			}

			static constexpr std::string_view ELEMENT_READS[] = {"position", "size", "scale", "color", "alpha", "alive", "axisX", "axisY", "axisZ", "angle", "pivot"};
			static constexpr std::string_view POINT_READS[] = {"position", "color", "alpha", "alive"};
			static constexpr std::string_view TARGET_READS[] = {"position", "color", "alpha", "alive", "targetPosition"};
			static constexpr std::string_view GRAPH_READS[] = {"position", "size", "scale", "alive", "axisX", "axisY", "axisZ", "angle", "pivot"};
			static constexpr std::string_view STRIP_READS[] = {"position", "size", "scale", "color", "alpha", "axisX", "axisY", "axisZ", "angle", "pivot"};
			static constexpr std::string_view GRAPH_STRIP_READS[] = {"position", "size", "scale", "axisX", "axisY", "axisZ", "angle", "pivot"};

			static std::span<const std::string_view> OutputReads(const OutputForm& form) {
				if (form.shape == OutputShape::Strip) {
					return form.nodeShader ? std::span<const std::string_view>(GRAPH_STRIP_READS) : std::span<const std::string_view>(STRIP_READS);
				}
				if (form.nodeShader) {
					return GRAPH_READS;
				}
				if (form.shape == OutputShape::Point) {
					return POINT_READS;
				}
				return form.shape == OutputShape::Line && !form.targetOffset ? std::span<const std::string_view>(TARGET_READS)
																			 : std::span<const std::string_view>(ELEMENT_READS);
			}

			void Store(GraphEffectSystem& system, Stack& initialize, Stack& update, const HashSet<str>& outputReads) {
				HashSet<str> stored{GetMemoryResource(MemTag::Editor)};
				ForEachAttribute([&](const GraphEffectAttributeInfo& attribute) {
					const str name(attribute.name);
					if (attribute.type != T::Bool && (initialize.written.contains(name) || update.written.contains(name))) {
						stored.insert(name);
					}
				});
				HashMap<str, GraphValueId> finals{GetMemoryResource(MemTag::Editor)};
				ForEachAttribute([&](const GraphEffectAttributeInfo& attribute) {
					if (stored.contains(str(attribute.name)) || attribute.type == T::Bool) {
						finals[str(attribute.name)] = update.Current(attribute);
					}
				});
				for (u32 round = 0; round < 64; ++round) {
					GraphProgram probe;
					probe.values = update.program.values;
					probe.labels = update.program.labels;
					probe.constants = update.program.constants;
					probe.externals = update.program.externals;
					for (const str& name : stored) {
						if (update.written.contains(name)) {
							probe.roots.push_back({finals[name], GraphStageVertex});
						}
					}
					probe.roots.push_back({finals["alive"], GraphStageVertex});
					for (const auto& [event, count] : update.events) {
						probe.roots.push_back({count, GraphStageVertex});
					}
					GraphPasses::Eliminate(probe);
					HashSet<str> read{GetMemoryResource(MemTag::Editor)};
					AttributeReadsOf(probe, read);
					HashSet<str> narrowed{GetMemoryResource(MemTag::Editor)};
					for (const str& name : stored) {
						if (read.contains(name) || outputReads.contains(name)) {
							narrowed.insert(name);
						}
					}
					const bool settled = narrowed.size() == stored.size();
					stored = std::move(narrowed);
					if (settled) {
						break;
					}
				}
				ForEachAttribute([&](const GraphEffectAttributeInfo& attribute) {
					if (stored.contains(str(attribute.name))) {
						system.stored.push_back({str(attribute.name), system.attributeCount, ComponentsOf(attribute)});
						system.attributeCount += ComponentsOf(attribute);
					}
				});
				if (system.attributeCount == 0) {
					system.attributeCount = 1;
				}
			}

			void FinishKernel(Stack& stack, GraphEffectSystem& system, bool fresh, Vector<const GraphEffectStored*>& keeps) {
				keeps.clear();
				stack.program.roots.clear();
				stack.program.roots.push_back({stack.Current(*GraphEffectAttributes::Find("alive")), GraphStageVertex});
				for (const GraphEffectStored& stored : system.stored) {
					if (fresh || stack.written.contains(stored.attribute)) {
						keeps.push_back(&stored);
						stack.program.roots.push_back({stack.Current(*FindAttribute(stored.attribute)), GraphStageVertex});
					}
				}
				for (const auto& [event, count] : stack.events) {
					stack.program.roots.push_back({count, GraphStageVertex});
				}
				(fresh ? system.initializeBoundsRoot : system.updateBoundsRoot) = AddSphere(stack, system);
				RunPasses(stack.program, system.initialize);
			}

			u32 AddSphere(Stack& stack, const GraphEffectSystem& system) {
				if (!system.measuresBounds) {
					return GRAPH_EFFECT_NO_BINDING;
				}
				GraphProgramBuilder builder(stack.program);
				builder.SetNode(Attributed(system.initialize));
				const auto current = [&](std::string_view name) { return stack.Current(*GraphEffectAttributes::Find(name)); };
				const GraphEffectBlocks::ElementSphere sphere = GraphEffectBlocks::ElementBounds(
					builder, current("position"), current("size"), current("scale"), current("angle"), current("pivot"), current("axisX"), current("axisY"), current("axisZ"));
				if (builder.Failed()) {
					Report(system.initialize, "couldn't measure its particles' bounds: " + builder.Error());
					return GRAPH_EFFECT_NO_BINDING;
				}
				const u32 root = static_cast<u32>(stack.program.roots.size());
				stack.program.roots.push_back({sphere.center, GraphStageVertex});
				stack.program.roots.push_back({sphere.radius, GraphStageVertex});
				return root;
			}

			void FinishCpuProgram(Stack& stack, GraphEffectSystem& system, bool fresh, bool implicitReap) {
				const auto storedAs = [&system](std::string_view name) {
					return std::any_of(system.stored.begin(), system.stored.end(), [name](const GraphEffectStored& stored) { return stored.attribute == name; });
				};
				GraphProgram& program = stack.program;
				program.roots.clear();
				program.roots.push_back({stack.Current(*GraphEffectAttributes::Find("alive")), GraphStageVertex});
				for (const GraphEffectStored& stored : system.stored) {
					program.roots.push_back({stack.Current(*FindAttribute(stored.attribute)), GraphStageVertex});
				}
				Vector<GraphEffectOutputEvent>& events = fresh ? system.initializeOutputEvents : system.updateOutputEvents;
				Vector<GraphEffectCarried>& carried = fresh ? system.initializeCarried : system.updateCarried;
				events.clear();
				carried.clear();
				for (const auto& [event, count] : stack.events) {
					const GraphContext* context = ContextById(event);
					events.push_back({context ? OutputEventName(*context) : str(GRAPH_EFFECT_DEFAULT_OUTPUT_EVENT_NAME), static_cast<u32>(program.roots.size())});
					program.roots.push_back({count, GraphStageVertex});
				}
				if (!events.empty()) {
					for (u32 i = 0; i < system.stored.size(); ++i) {
						const GraphEffectStored& stored = system.stored[i];
						if (GraphEffectAttributes::Find(stored.attribute)) {
							carried.push_back({stored.attribute, i + 1, stored.components});
						}
					}
					for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
						if (stack.written.contains(str(attribute.name)) && !storedAs(attribute.name)) {
							carried.push_back({str(attribute.name), static_cast<u32>(program.roots.size()), GraphTypes::Components(attribute.type)});
							program.roots.push_back({stack.Current(attribute), GraphStageVertex});
						}
					}
				}
				(fresh ? system.initializeBoundsRoot : system.updateBoundsRoot) = AddSphere(stack, system);
				const str attributePrefix = GraphEffectAttributes::CurrentBinding("");
				for (GraphValue& value : program.values) {
					if (value.op != GraphOp::External) {
						continue;
					}
					const str& name = program.externals[value.payload].name;
					f32 constant[4] = {};
					u32 components = 0;
					if (name.starts_with(attributePrefix)) {
						const std::string_view attribute = std::string_view(name).substr(attributePrefix.size());
						const bool input = attribute == PARTICLE_ID ? (fresh || storedAs(attribute))
																	: (fresh ? attribute == SPAWN_INDEX : storedAs(attribute));
						const GraphEffectAttributeInfo* info = FindAttribute(attribute);
						if (input || !info) {
							continue;
						}
						if (attribute != PARTICLE_ID) {
							std::memcpy(constant, info->defaultValue, sizeof(constant));
						}
						components = GraphTypes::Components(value.type);
					} else if (name == GRAPH_EFFECT_FLIPBOOK_SIZE) {
						constant[0] = 1.0f;
						constant[1] = 1.0f;
						components = 2;
					} else if (name == GRAPH_EFFECT_FLIPBOOK_BLENDING) {
						components = 1;
					} else if (name == GRAPH_EFFECT_IMPLICIT_REAP) {
						constant[0] = !fresh && implicitReap ? 1.0f : 0.0f;
						components = 1;
					} else {
						continue;
					}
					value.op = GraphOp::Constant;
					value.operandCount = 0;
					value.payload = static_cast<u32>(program.constants.size());
					program.constants.insert(program.constants.end(), constant, constant + components);
				}
				RunPasses(program, system.initialize);
			}

			void RunPasses(GraphProgram& program, u32 node) {
				Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
				if (!GraphPasses::AssignStages(program, diagnostics)) {
					for (GraphDiagnostic& diagnostic : diagnostics) {
						if (diagnostic.node != 0) {
							Pass(std::move(diagnostic));
						} else {
							Report(node, std::move(diagnostic.message));
						}
					}
					return;
				}
				GraphPasses::Fold(program);
				GraphPasses::Deduplicate(program);
				GraphPasses::Eliminate(program);
			}

			void Header(Writer& file, std::string_view what) const {
				file.Line(std::format("// generated from '{}' -> {}{}. Do not edit manually", _sourceName, what, _label));
			}

			void DeclareProperties(Writer& file, const GraphProgram& program) const {
				for (size_t i = 0; i < _propertyFields->size(); ++i) {
					const GraphEffectField& field = (*_propertyFields)[i];
					if (!ReadsExternal(program, field.name)) {
						continue;
					}
					const GraphPropertyInfo* info = nullptr;
					for (const GraphPropertyInfo& property : *_properties) {
						if (property.reference == field.name) {
							info = &property;
						}
					}
					const std::string_view member = GraphProperties::MemberType(info->kind);
					const str reader = field.components == 1 ? str("SltParticleEffectFloat") : std::format("SltParticleEffectFloat{}", field.components);
					file.Line(std::format("\t{} {} = ({}){}({}u);", member, GraphProperties::MemberName(*info), member, reader, field.offset));
				}
			}

			void DeclareParticleId(Writer& file, const GraphProgram& program, const GraphEffectSystem& system, std::string_view load,
								   std::span<const GraphEffectStored* const> keeps, std::string_view base = "SltParticleEffectIdBase()") const {
				HashSet<str> read{GetMemoryResource(MemTag::Editor)};
				AttributeReadsOf(program, read);
				const bool kept = std::any_of(keeps.begin(), keeps.end(), [](const GraphEffectStored* stored) { return stored->attribute == PARTICLE_ID; });
				if (!read.contains(str(PARTICLE_ID)) && !kept) {
					return;
				}
				if (load.empty()) {
					file.Line(std::format("\tuint sltParticleId = {} + sltId.x;", base));
					return;
				}
				for (const GraphEffectStored& stored : system.stored) {
					if (stored.attribute == PARTICLE_ID) {
						file.Line(std::format("\tuint sltParticleId = asuint({}({}, sltIndex));", load, stored.slot));
						return;
					}
				}
				file.Line("\tuint sltParticleId = 0u;");
			}

			enum class AttributeSet : u8 {
				All,
				AllButStrip,
				Strip
			};

			static str StripExpression(std::string_view name, StripRead strips) {
				switch (strips) {
				case StripRead::Spawn:
					if (name == STRIP_INDEX) {
						return "float(sltStrip)";
					}
					if (name == PARTICLE_INDEX_IN_STRIP) {
						return "float(sltStripPlace)";
					}
					if (name == PARTICLE_COUNT_IN_STRIP) {
						return "float(SltParticleStripWord(sltStrip, SLT_PARTICLE_STRIP_BEGAN) + sltSpawnTotal)";
					}
					return "float(sltStripPlace - SltParticleStripWord(sltStrip, SLT_PARTICLE_STRIP_BEGAN))";
				case StripRead::Kernel:
					if (name == STRIP_INDEX) {
						return "float(SltParticleStripOf(sltIndex))";
					}
					if (name == PARTICLE_INDEX_IN_STRIP) {
						return "float(SltParticleStripPlace(sltIndex))";
					}
					return name == PARTICLE_COUNT_IN_STRIP ? str("float(SltParticleStripWord(SltParticleStripOf(sltIndex), SLT_PARTICLE_STRIP_COUNT))") : str();
				case StripRead::Output:
					if (name == STRIP_INDEX) {
						return "float(SltParticleOutputStripOf(sltIndex))";
					}
					if (name == PARTICLE_INDEX_IN_STRIP) {
						return "float(SltParticleOutputStripPlace(sltIndex))";
					}
					return name == PARTICLE_COUNT_IN_STRIP ? str("float(SltParticleOutputStripCount(SltParticleOutputStripOf(sltIndex)))") : str();
				default:
					return {};
				}
			}

			void DeclareAttributes(Writer& file, const GraphProgram& program, const GraphEffectSystem& system, std::string_view load,
								   StripRead strips = StripRead::None, AttributeSet set = AttributeSet::All) const {
				HashSet<str> read{GetMemoryResource(MemTag::Editor)};
				ReadsOf(program, "Attribute/", read);
				ForEachAttribute([&](const GraphEffectAttributeInfo& attribute) {
					if (!read.contains(str(attribute.name))) {
						return;
					}
					const bool inStrip = IsStripAttribute(attribute.name);
					if ((set == AttributeSet::AllButStrip && inStrip) || (set == AttributeSet::Strip && !inStrip)) {
						return;
					}
					str value = Literal(attribute);
					for (const GraphEffectStored& stored : system.stored) {
						if (stored.attribute == attribute.name && !load.empty()) {
							value = LoadExpression(stored, load);
						}
					}
					if (attribute.name == PARTICLE_ID) {
						value = "float(sltParticleId)";
					} else if (attribute.name == SPAWN_INDEX && load.empty()) {
						value = "float(sltSpawnIndex)";
					} else if (const str strip = inStrip ? StripExpression(attribute.name, strips) : str(); !strip.empty()) {
						value = strip;
					}
					file.Line(std::format("\t{} {} = {};", GraphTypes::HLSLName(attribute.type), AttributeLocal(attribute.name), value));
				});
			}

			static void DeclareImplicitReap(Writer& file, const GraphProgram& program, bool reaps) {
				if (ReadsExternal(program, GRAPH_EFFECT_IMPLICIT_REAP)) {
					file.Line(std::format("\tconst bool sltImplicitReap = {};", reaps ? "true" : "false"));
				}
			}

			struct KernelEnd {
				struct Append {
					str count;
					str lists;
					str arguments;
					u32 capacity = 0;
					u32 strips = 0;
					u32 outputs = 0;
				};
				bool strips = false;
				bool fresh = false;
				Vector<Append> appends{GetMemoryResource(MemTag::Editor)};
				u32 sphere = GRAPH_EFFECT_NO_BINDING;
			};

			void EndKernel(Writer& file, const GraphHLSLText& text, std::span<const GraphEffectStored* const> keeps, const KernelEnd& end) const {
				const bool triggers = !end.appends.empty();
				const KernelEnd::Append* holder = nullptr;
				for (const KernelEnd::Append& append : end.appends) {
					holder = !end.fresh && append.strips > 0 ? &append : holder;
				}
				const auto appendAll = [&]() {
					for (const KernelEnd::Append& append : end.appends) {
						const str call = std::format("SltParticleAppendEvents({}, {}, {}u, {}u, {}u, sltIndex, uint(max({}, 0.0)));", append.lists, append.arguments,
													 append.capacity, append.strips, append.outputs, append.count);
						file.Line(append.strips > 0 ? std::format("\tif (sltIndex < {}u) {{ {} }}", append.strips, call) : "\t" + call);
					}
				};
				const auto kill = [&]() {
					if (holder) {
						file.Line(std::format("\t\tif (sltIndex < {}u) {{", holder->strips));
						file.Line(std::format("\t\t\tSltParticleHoldForStrip({}, {}u, sltIndex);", holder->lists, holder->capacity));
						file.Line("\t\t} else {");
						file.Line("\t\t\tSltParticleKill(sltIndex);");
						file.Line("\t\t}");
					} else {
						file.Line("\t\tSltParticleKill(sltIndex);");
					}
				};
				const bool storeFirst = end.strips || (triggers && !end.fresh);
				if (triggers && !end.fresh) {
					appendAll();
				}
				if (!storeFirst) {
					file.Line(std::format("\tif (!({})) {{", text.roots[0]));
					kill();
					file.Line("\t\treturn;");
					file.Line("\t}");
				}
				for (size_t i = 0; i < keeps.size(); ++i) {
					const GraphEffectStored& stored = *keeps[i];
					const str& value = text.roots[i + 1];
					if (stored.attribute == PARTICLE_ID) {
						file.Line(std::format("\tSltParticleStore({}, sltIndex, asfloat(sltParticleId));", stored.slot));
						continue;
					}
					if (stored.components == 1) {
						file.Line(std::format("\tSltParticleStore({}, sltIndex, {});", stored.slot, value));
						continue;
					}
					for (u32 component = 0; component < stored.components; ++component) {
						file.Line(std::format("\tSltParticleStore({}, sltIndex, ({}).{});", stored.slot + component, value, "xyz"[component]));
					}
				}
				if (storeFirst) {
					file.Line(std::format("\tif (!({})) {{", text.roots[0]));
					if (!end.strips) {
						kill();
					}
					file.Line("\t\treturn;");
					file.Line("\t}");
				}
				if (end.strips) {
					file.Line("\tSltParticleStripLive(sltIndex);");
				}
				if (triggers && end.fresh) {
					appendAll();
				}
				if (end.sphere != GRAPH_EFFECT_NO_BINDING) {
					file.Line(std::format("\tsltSphere = float4({}, {});", text.roots[end.sphere], text.roots[end.sphere + 1]));
				}
				file.Line("\tSltParticleAppendAlive(sltIndex);");
				file.Line("}");
				if (end.sphere != GRAPH_EFFECT_NO_BINDING) {
					file.Line("");
					file.Line("[numthreads(SLT_PARTICLE_KERNEL_GROUP, 1, 1)]");
					file.Line("void CSMain(uint3 sltId : SV_DispatchThreadID, uint sltThread : SV_GroupIndex) {");
					file.Line("\tfloat4 sltSphere = SLT_PARTICLE_NO_SPHERE;");
					file.Line("\tSltParticleRun(sltId, sltSphere);");
					file.Line("\tSltParticleReduceBounds(sltThread, sltSphere);");
					file.Line("}");
				}
			}

			template <typename Fn>
			void DeclareTextures(Writer& file, const GraphProgram& program, u32 set, u32 first, Fn&& declared) const {
				u32 at = first;
				for (size_t i = 0; i < _portTextures.size(); ++i) {
					const GraphBinding& binding = _portTextures[i];
					if (!ReadsExternal(program, binding.name)) {
						continue;
					}
					file.Line(std::format("[[vk::binding({}, {})]] {} {};", at, set, GraphTypes::HLSLName(binding.type), binding.expression));
					file.Line(std::format("[[vk::binding({}, {})]] SamplerState {};", at + 1, set, binding.sampler));
					declared(at, at + 1, binding.expression, _portTextureAssets[i]);
					at += 2;
				}
				if (at != first) {
					file.Line("");
				}
			}

			void KernelOpening(Writer& file, const GraphHLSLText& text, const GraphProgram& program, Vector<GraphEffectKernelTexture>& textures,
							   u32& depth, std::span<const str> buffers, Vector<u32>* bindings, bool measured) const {
				file.Line("#pragma shalltear_kernel");
				file.Line("#include \"VisualEffects/ParticleSystem.hlsli\"");
				if (measured) {
					file.Line("#include \"VisualEffects/ParticleBounds.hlsli\"");
				}
				file.Line("");
				u32 next = GRAPH_EFFECT_KERNEL_TEXTURES;
				DeclareTextures(file, program, 0, GRAPH_EFFECT_KERNEL_TEXTURES, [&textures, &next](u32 binding, u32 sampler, const str&, const Guid& asset) {
					textures.push_back({binding, sampler, asset});
					next = sampler + 1;
				});
				depth = GRAPH_EFFECT_NO_BINDING;
				if (ReadsExternal(program, GRAPH_EFFECT_CAMERA_DEPTH)) {
					depth = next++;
					file.Line(std::format("[[vk::binding({}, 0)]] Texture2D sltCameraDepth;", depth));
					file.Line("");
				}
				for (const str& buffer : buffers) {
					file.Line(std::format("[[vk::binding({}, 0)]] RWByteAddressBuffer {};", next, buffer));
					bindings->push_back(next++);
				}
				if (!buffers.empty()) {
					file.Line("");
				}
				file.Lines(text.functions, text.functionLineNodes);
				const auto enter = [&file]() {
					file.Line("\tif (!SltParticleEnterUpdate(sltId)) {");
					file.Line("\t\treturn;");
					file.Line("\t}");
				};
				if (measured) {
					file.Line("void SltParticleRun(uint3 sltId, inout float4 sltSphere) {");
					enter();
					return;
				}
				file.Line("[numthreads(SLT_PARTICLE_KERNEL_GROUP, 1, 1)]");
				file.Line("void CSMain(uint3 sltId : SV_DispatchThreadID) {");
				enter();
			}

			struct KernelTarget {
				u32 child = 0;
				u32 root = 0;
			};
			void TargetsOf(const SystemBuild& build, const Stack& stack, size_t keeps, Vector<KernelTarget>& out, Vector<str>& buffers) const {
				for (u32 e = 0; e < stack.events.size(); ++e) {
					for (const u32 child : build.children) {
						const auto fed = _feedingEvent.find(_builds[child].initialize->node.id);
						if (fed != _feedingEvent.end() && fed->second == stack.events[e].first) {
							buffers.push_back(std::format("sltEventList{}", out.size()));
							buffers.push_back(std::format("sltEventArguments{}", out.size()));
							out.push_back({child, static_cast<u32>(1 + keeps + e)});
						}
					}
				}
			}

			void AppendsOf(std::span<const KernelTarget> targets, const GraphHLSLText& text, KernelEnd& end) const {
				for (u32 k = 0; k < targets.size(); ++k) {
					const SystemBuild& child = _builds[targets[k].child];
					end.appends.push_back({text.roots[targets[k].root], std::format("sltEventList{}", k), std::format("sltEventArguments{}", k),
										   child.system.capacity, child.system.strips, static_cast<u32>(child.outputs.size())});
				}
			}

			void LayBatch(GraphEffectSystem& system, const HashSet<str>& sources, bool child) {
				u32 offset = 4;
				for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
					if (!sources.contains(str(attribute.name)) || attribute.name == SPAWN_COUNT) {
						continue;
					}
					if (!child) {
						system.source.push_back({str(attribute.name), offset, ComponentsOf(attribute)});
						offset += 4 * ComponentsOf(attribute);
					} else if (attribute.type == T::Bool || (IsStripAttribute(attribute.name) && attribute.name != SPAWN_INDEX_IN_STRIP)) {
						Report(system.initialize, "reads its source's " + str(attribute.name) + " (the particles spawning it don't have it)");
					}
				}
				system.batchStride = offset;
			}

			void EmitCpu(SystemBuild& build) {
				if (Failed()) {
					return;
				}
				GraphEffectSystem& system = build.system;
				HashSet<str> sources{GetMemoryResource(MemTag::Editor)};
				ReadsOf(build.initializeStack.program, "SourceAttribute/", sources);
				LayBatch(system, sources, false);
				const str where = CpuSystem(system.initialize);
				Assemble(build.initializeStack.program, system.initializeAssembly, where);
				Assemble(build.updateStack.program, system.updateAssembly, where);
			}

			void EmitInitialize(SystemBuild& build, const GraphEffect& effect) {
				if (Failed()) {
					return;
				}
				GraphEffectSystem& system = build.system;
				const Stack& stack = build.initializeStack;
				const std::span<const GraphEffectStored* const> keeps = build.initializeKeeps;
				const bool strips = system.strips > 0;
				const bool child = build.parent != SystemBuild::NONE;
				const bool stripPort = strips && !child;
				GraphHLSLText stripText;
				if (stripPort) {
					GraphHLSL::Emit(build.stripIndex, GraphStageVertex, stripText);
				}
				GraphHLSLText text;
				GraphHLSL::Emit(stack.program, GraphStageVertex, text, stripPort ? &stripText.helpers : nullptr);
				GraphProgram reads = stack.program;
				if (stripPort) {
					Vector<GraphValueId> roots{GetMemoryResource(MemTag::Editor)};
					GraphEffectSplice::Append(reads, build.stripIndex, HashMap<str, GraphValueId>{GetMemoryResource(MemTag::Editor)}, roots);
					text.functions = stripText.functions + text.functions;
					text.functionLineNodes.insert(text.functionLineNodes.begin(), stripText.functionLineNodes.begin(), stripText.functionLineNodes.end());
				}

				HashSet<str> sources{GetMemoryResource(MemTag::Editor)};
				ReadsOf(reads, "SourceAttribute/", sources);
				LayBatch(system, sources, child);
				HashSet<str> read{GetMemoryResource(MemTag::Editor)};
				ReadsOf(reads, "Attribute/", read);
				const bool spawnIndex = read.contains(str(SPAWN_INDEX)) ||
										std::any_of(keeps.begin(), keeps.end(), [](const GraphEffectStored* stored) { return stored->attribute == SPAWN_INDEX; });
				const bool spawnCount = sources.contains(str(SPAWN_COUNT));
				if (Failed()) {
					return;
				}

				Vector<KernelTarget> targets{GetMemoryResource(MemTag::Editor)};
				Vector<str> buffers{GetMemoryResource(MemTag::Editor)};
				if (child) {
					buffers.push_back("sltParentAttributes");
				}
				TargetsOf(build, stack, keeps.size(), targets, buffers);
				Vector<u32> bindings{GetMemoryResource(MemTag::Editor)};

				Writer file;
				Header(file, std::format("the Initialize kernel of Initialize context {}", system.initialize));
				KernelOpening(file, text, reads, system.initializeTextures, system.initializeDepthBinding, buffers, &bindings, system.measuresBounds);
				u32 at = 0;
				if (child) {
					system.parentBinding = bindings[at++];
				}
				for (const KernelTarget& target : targets) {
					system.initializeEvents.push_back({_builds[target.child].index, bindings[at], bindings[at + 1]});
					at += 2;
				}
				if (child) {
					file.Line("\tuint sltEvents = SltParticleEventCount();");
					file.Line("\tif (sltId.x >= sltEvents) {");
				} else {
					file.Line("\tif (sltId.x >= SltParticleSpawnCount()) {");
				}
				file.Line("\t\treturn;");
				file.Line("\t}");
				if (strips) {
					file.Line(child ? "\tuint sltSpawnTotal = sltEvents;" : "\tuint sltSpawnTotal = SltParticleSpawnCount();");
				}
				if (!strips) {
					file.Line("\tuint sltIndex;");
					file.Line("\tif (!SltParticlePopDead(sltIndex)) {");
					file.Line("\t\treturn;");
					file.Line("\t}");
				}
				if (child) {
					const GraphEffectSystem& parent = effect.systems[_builds[build.parent].index];
					file.Line("\tuint sltSource = SltParticleEventSource(sltId.x);");
					for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
						if (!sources.contains(str(attribute.name)) || attribute.name == SPAWN_COUNT) {
							continue;
						}
						str value = Literal(attribute);
						for (const GraphEffectStored& stored : parent.stored) {
							if (stored.attribute != attribute.name) {
								continue;
							}
							const auto load = [&](u32 slot) {
								const str word = std::format("sltParentAttributes.Load(({}u * {}u + sltSource) * 4u)", slot, parent.capacity);
								return attribute.name == PARTICLE_ID ? "float(" + word + ")" : "asfloat(" + word + ")";
							};
							value = stored.components == 1 ? load(stored.slot)
														   : std::format("float3({}, {}, {})", load(stored.slot), load(stored.slot + 1), load(stored.slot + 2));
						}
						file.Line(std::format("\t{} {} = {};", GraphTypes::HLSLName(attribute.type), SourceLocal(attribute.name), value));
					}
					if (spawnIndex) {
						file.Line("\tuint sltSpawnIndex = sltId.x;");
					}
					if (spawnCount) {
						file.Line(std::format("\tfloat {} = float(sltEvents);", SourceLocal(SPAWN_COUNT)));
					}
				} else {
					if (!system.source.empty() || spawnIndex || spawnCount) {
						file.Line("\tuint sltSpawnFirst;");
						file.Line(std::format("\tuint sltBatch = SltParticleEffectBatch(sltId.x, {}u, {}u, sltSpawnFirst);", effect.batchesOffset,
											  system.batchStride));
						for (const GraphEffectField& field : system.source) {
							const GraphEffectAttributeInfo& attribute = *GraphEffectAttributes::Find(field.name);
							const str reader = field.components == 1 ? str("SltParticleEffectFloat") : str("SltParticleEffectFloat3");
							file.Line(std::format("\t{} {} = {}(sltBatch + {}u);", GraphTypes::HLSLName(attribute.type), SourceLocal(field.name), reader,
												  field.offset));
						}
					}
					if (spawnIndex) {
						file.Line("\tuint sltSpawnIndex = sltId.x - sltSpawnFirst;");
					}
					if (spawnCount) {
						file.Line(std::format("\tfloat {} = float(SltParticleEffectWord(sltBatch));", SourceLocal(SPAWN_COUNT)));
					}
				}
				DeclareParticleId(file, reads, system, {}, keeps, child ? "SltParticleEventIdBase()" : "SltParticleEffectIdBase()");
				DeclareProperties(file, reads);
				DeclareImplicitReap(file, reads, false);
				DeclareAttributes(file, reads, system, {}, strips ? StripRead::Spawn : StripRead::None, strips ? AttributeSet::AllButStrip : AttributeSet::All);
				if (strips) {
					if (child) {
						file.Line("\tuint sltStrip = sltSource;");
					} else {
						file.Line("\tuint sltStrip;");
						file.Line("\t{");
						file.Lines(stripText.body, stripText.lineNodes);
						file.Line(std::format("\t\tsltStrip = uint(max({}, 0.0));", stripText.roots[0]));
						file.Line("\t}");
					}
					file.Line("\tuint sltIndex;");
					file.Line("\tuint sltStripPlace;");
					file.Line("\tif (!SltParticleStripSpawn(sltStrip, sltIndex, sltStripPlace)) {");
					file.Line("\t\treturn;");
					file.Line("\t}");
					DeclareAttributes(file, reads, system, {}, StripRead::Spawn, AttributeSet::Strip);
				}
				file.Lines(text.body, text.lineNodes);
				KernelEnd end;
				end.strips = strips;
				end.fresh = true;
				end.sphere = system.initializeBoundsRoot;
				AppendsOf(targets, text, end);
				EndKernel(file, text, keeps, end);
				system.initializeKernel = std::move(file.text);
				system.initializeLines = std::move(file.lines);
			}

			void EmitUpdate(SystemBuild& build) {
				if (Failed()) {
					return;
				}
				GraphEffectSystem& system = build.system;
				const Stack& stack = build.updateStack;
				const std::span<const GraphEffectStored* const> keeps = build.updateKeeps;
				GraphHLSLText text;
				GraphHLSL::Emit(stack.program, GraphStageVertex, text);
				Vector<KernelTarget> targets{GetMemoryResource(MemTag::Editor)};
				Vector<str> buffers{GetMemoryResource(MemTag::Editor)};
				TargetsOf(build, stack, keeps.size(), targets, buffers);
				Vector<u32> bindings{GetMemoryResource(MemTag::Editor)};
				Writer file;
				Header(file, build.update ? std::format("the Update kernel of Update context {}", build.update->node.id)
										  : std::format("the Update kernel of Initialize context {} (no Update context)", system.initialize));
				KernelOpening(file, text, stack.program, system.updateTextures, system.updateDepthBinding, buffers, &bindings, system.measuresBounds);
				for (u32 k = 0; k < targets.size(); ++k) {
					system.updateEvents.push_back({_builds[targets[k].child].index, bindings[2 * k], bindings[2 * k + 1]});
				}
				file.Line("\tif (sltId.x >= SltParticleAliveInCount()) {");
				file.Line("\t\treturn;");
				file.Line("\t}");
				file.Line("\tuint sltIndex = SltParticleAliveIn(sltId.x);");
				const bool strips = system.strips > 0;
				DeclareParticleId(file, stack.program, system, "SltParticleLoad", keeps);
				DeclareProperties(file, stack.program);
				DeclareImplicitReap(file, stack.program, build.implicitReap);
				DeclareAttributes(file, stack.program, system, "SltParticleLoad", strips ? StripRead::Kernel : StripRead::None);
				file.Lines(text.body, text.lineNodes);
				KernelEnd end;
				end.strips = strips;
				end.sphere = system.updateBoundsRoot;
				AppendsOf(targets, text, end);
				EndKernel(file, text, keeps, end);
				system.updateKernel = std::move(file.text);
				system.updateLines = std::move(file.lines);
			}

			bool MeshRecord(const GraphContext& context, const GraphEffect& effect, GraphEffectOutput& output, u32& record) {
				const str binding = GraphCompiler::PortMeshBinding(context.node.id, "Mesh", _within);
				for (u32 i = 0; i < effect.meshes.size(); ++i) {
					if (effect.meshes[i].binding == binding) {
						output.mesh = i;
						record = effect.meshes[i].offset;
					}
				}
				if (output.mesh == GRAPH_EFFECT_NO_BINDING) {
					Report(context.node.id, "has its Mesh fed by a node (an effect takes a mesh asset on the port)");
					return false;
				}
				output.instanced = true;
				return true;
			}

			void StripVertexOpening(Writer& file) const {
				file.Line("SltParticleVertex VSMain(uint sltVertexId : SV_VertexID) {");
				file.Line("\tuint sltSegment;");
				file.Line("\tif (!SltParticleOutputSegment(sltVertexId / 6u, sltSegment)) {");
				file.Line("\t\tSltParticleVertex sltCulled = (SltParticleVertex)0;");
				file.Line("\t\tsltCulled.positionCS = SLT_PARTICLE_CULLED;");
				file.Line("\t\treturn sltCulled;");
				file.Line("\t}");
				file.Line("\tfloat2 sltQuad = SltParticleQuadCorner(SltParticleOutputCorner(sltVertexId));");
				file.Line("\tuint sltEnd = sltQuad.x > 0.0 ? 1u : 0u;");
				file.Line("\tuint sltIndex = sltEnd == 0u ? sltSegment : SltParticleOutputStripIndex(SltParticleOutputStripOf(sltSegment), "
						  "SltParticleOutputStripPlace(sltSegment) + 1u);");
			}

			void DeclareStripNeighbors(Writer& file, const GraphProgram& program, const GraphEffectSystem& system) const {
				const GraphEffectStored* position = nullptr;
				for (const GraphEffectStored& stored : system.stored) {
					position = stored.attribute == "position" ? &stored : position;
				}
				for (const std::string_view name : {GRAPH_EFFECT_STRIP_PREVIOUS, GRAPH_EFFECT_STRIP_NEXT}) {
					if (!ReadsExternal(program, name)) {
						continue;
					}
					const bool next = name == GRAPH_EFFECT_STRIP_NEXT;
					file.Line(std::format("\tfloat4 {} = {};", next ? "sltStripNext" : "sltStripPrevious",
										  position ? std::format("SltParticleOutputStripNeighbor(sltIndex, {}, {}u)", next ? "true" : "false", position->slot)
												   : str("float4(0.0, 0.0, 0.0, 0.0)")));
				}
			}

			void StripVertex(Writer& file, const OutputForm& form, std::string_view size, std::string_view scale, std::string_view element, bool uv,
							 std::string_view texCoord) const {
				file.Line(std::format("\tfloat3 sltSize = SltParticleStripSize({}, {});", size, scale));
				file.Line("\tfloat3 sltOffset = float3(0.0, sltQuad.y * 0.5, 0.0);");
				file.Line(std::format("\tfloat3 sltWorld = {};", element));
				if (uv) {
					StripUV(file, form, texCoord);
				}
			}

			void StripUV(Writer& file, const OutputForm& form, std::string_view texCoord) const {
				str u = "float(sltEnd)";
				if (form.tiling == 0) {
					u = "float(SltParticleOutputStripPlace(sltIndex)) / float(SltParticleOutputStripCount(SltParticleOutputStripOf(sltIndex)) - 1u)";
				} else if (form.tiling == 2) {
					u = str(texCoord);
				}
				file.Line(std::format("\tfloat2 sltUV = float2({}, sltQuad.y * 0.5 + 0.5);", u));
				if (form.swapUV) {
					file.Line("\tsltUV = float2(1.0 - sltUV.y, sltUV.x);");
				}
			}

			void EmitOutput(const Stack& stack, const GraphContext& context, GraphEffectSystem& system, const OutputForm& form, const GraphEffect& effect) {
				if (Failed()) {
					return;
				}
				if (form.nodeShader) {
					EmitNodeShaderOutput(stack, context, system, form, effect);
					return;
				}
				GraphEffectOutput output;
				output.context = context.node.id;
				constexpr RHI::BlendMode BLENDS[] = {RHI::BlendMode::Additive, RHI::BlendMode::Alpha, RHI::BlendMode::Premultiplied,
													 RHI::BlendMode::Opaque};
				output.blend = BLENDS[form.blendMode];
				output.sort = form.sort;
				output.revertSorting = form.revert;
				output.vertices = form.Vertices();
				output.castShadows = form.castShadows;
				output.strip = form.shape == OutputShape::Strip;
				const bool opaque = output.blend == RHI::BlendMode::Opaque;
				const std::string_view mainPort = form.lit ? "Base Color Map" : "Main Texture";
				for (const GraphInput& input : context.node.inputs) {
					if (input.fromNode == 0) {
						continue;
					}
					if (input.port == mainPort && form.Sampled()) {
						Report(context.node.id, std::format("has its {} fed by a node (an effect takes a texture asset on the port)", mainPort));
						return;
					}
					for (u32 map = 0; form.lit && map < std::size(GRAPH_EFFECT_LIT_MAP_PORTS); ++map) {
						if (form.maps[map] && input.port == GRAPH_EFFECT_LIT_MAP_PORTS[map]) {
							Report(context.node.id, std::format("has its {} fed by a node (an effect takes a texture asset on the port)",
																GRAPH_EFFECT_LIT_MAP_PORTS[map]));
							return;
						}
					}
				}
				if (form.Sampled()) {
					output.textures.push_back({str(GRAPH_EFFECT_MAIN_TEXTURE), GraphEffectTarget::HeldTexture(context.node, mainPort)});
				}
				u32 slotBinding = GRAPH_EFFECT_OUTPUT_TEXTURES;
				u32 mapBinding[std::size(GRAPH_EFFECT_LIT_MAP_PORTS)] = {};
				bool anyMap = false;
				for (u32 map = 0; form.lit && map < std::size(GRAPH_EFFECT_LIT_MAP_PORTS); ++map) {
					if (form.maps[map]) {
						mapBinding[map] = slotBinding;
						slotBinding += 2;
						anyMap = true;
						output.textures.push_back({str(GRAPH_EFFECT_LIT_MAP_SLOTS[map]), GraphEffectTarget::HeldTexture(context.node, GRAPH_EFFECT_LIT_MAP_PORTS[map])});
					}
				}
				u32 record = 0;
				if (form.shape == OutputShape::Mesh && !MeshRecord(context, effect, output, record)) {
					return;
				}
				const std::span<const std::string_view> reads = OutputReads(form);
				const auto read = [&](std::string_view name, const GraphHLSLText& text) -> const str& {
					return text.roots[static_cast<size_t>(std::find(reads.begin(), reads.end(), name) - reads.begin())];
				};
				u32 next = static_cast<u32>(reads.size());
				const u32 textureRoot = next;
				next += form.Flipbook() ? 3u : (form.ScaleAndBias() ? 2u : 0u);
				enum Extra : u32 { Crop,
								   Offset,
								   Threshold,
								   Soft,
								   Smoothness,
								   Metallic,
								   NormalScale,
								   EmissiveScale,
								   BaseColor,
								   EmissiveColor,
								   Bending,
								   TexCoord };
				const std::array<GraphValueId, 12> extras = form.Extras();
				u32 extraRoot[12] = {};
				for (u32 i = 0; i < extras.size(); ++i) {
					if (extras[i] != GRAPH_NO_VALUE) {
						extraRoot[i] = next++;
					}
				}
				const bool blended = form.Flipbook() && form.blend;
				const bool point = form.shape == OutputShape::Point;
				const bool softLine = form.SoftwareLine();
				const bool strip = form.shape == OutputShape::Strip;
				const bool uv = form.Sampled() || anyMap;
				const bool hasElement = form.shape == OutputShape::Primitive || form.shape == OutputShape::Mesh || strip;
				const bool depth = form.DepthProgram();
				const bool emission = form.lit && (form.ColorIsEmissive() || form.emissive);

				GraphHLSLText text;
				GraphHLSL::Emit(stack.program, GraphStageVertex, text);
				const auto root = [&](Extra extra) -> const str& { return text.roots[extraRoot[extra]]; };
				Writer file;
				Header(file, std::format("the {}{} of Output context {}", form.lit ? "lit " : "", form.Name(), context.node.id));
				file.Line(std::format("#pragma shalltear_blend {}", RHI::BlendModeName(output.blend)));
				const bool writes = form.zWriteMode == 0 ? opaque : form.zWriteMode == 2;
				file.Line(std::format("#pragma shalltear_depth {}", writes ? "test_write" : "test_only"));
				constexpr RHI::DepthCompare Z_TESTS[] = {RHI::DepthCompare::NearerOrEqual, RHI::DepthCompare::Nearer, RHI::DepthCompare::Farther,
														 RHI::DepthCompare::NearerOrEqual, RHI::DepthCompare::FartherOrEqual, RHI::DepthCompare::Equal,
														 RHI::DepthCompare::NotEqual, RHI::DepthCompare::Always};
				if (Z_TESTS[form.zTestMode] != RHI::DepthCompare::NearerOrEqual) {
					file.Line(std::format("#pragma shalltear_depth_compare {}", RHI::DepthCompareName(Z_TESTS[form.zTestMode])));
				}
				constexpr RHI::CullMode CULLS[] = {RHI::CullMode::Off, RHI::CullMode::Front, RHI::CullMode::Back, RHI::CullMode::Off};
				const RHI::CullMode cull = form.cullMode == 0 && form.shape == OutputShape::Mesh ? RHI::CullMode::Back : CULLS[form.cullMode];
				file.Line(std::format("#pragma shalltear_cull {}", RHI::CullModeName(cull)));
				if (point) {
					file.Line("#pragma shalltear_topology point");
				} else if (form.shape == OutputShape::Line && form.nativeLines) {
					file.Line("#pragma shalltear_topology line");
				}
				if (depth) {
					file.Line(form.DepthClips() ? "#pragma shalltear_depth_program clip value" : "#pragma shalltear_depth_program value");
				}
				file.Line("");
				file.Line("#include \"VisualEffects/ParticleOutput.hlsli\"");
				if (form.lit) {
					file.Line("#ifdef SHALLTEAR_DEPTH_PROGRAM");
					file.Line("#include \"Surface.hlsli\"");
					file.Line("#else");
					file.Line("#include \"VisualEffects/ParticleLit.hlsli\"");
					file.Line("#endif");
				}
				file.Line("#ifndef SHALLTEAR_DEPTH_PROGRAM");
				file.Line("#include \"Fog.hlsli\"");
				file.Line("#endif");
				if (ReadsExternal(stack.program, GRAPH_EFFECT_CAMERA_CLIP_W)) {
					file.Line("#include \"VisualEffects/ParticleCamera.hlsli\"");
				}
				file.Line("");
				file.Line("[[vk::binding(0, 2)]] cbuffer sltViewRecord {");
				file.Line("\tShalltearViewData sltPassView;");
				file.Line("};");
				if (form.soft) {
					file.Line("[[vk::binding(1, 2)]] Texture2D sltSceneDepth;");
				}
				file.Line("");
				if (form.Sampled()) {
					file.Line(std::format("[[vk::binding(0, 1)]] Texture2D {};", GRAPH_EFFECT_MAIN_TEXTURE));
					file.Line(std::format("[[vk::binding(1, 1)]] SamplerState {}Sampler;", GRAPH_EFFECT_MAIN_TEXTURE));
					file.Line("");
				}
				for (u32 map = 0; form.lit && map < std::size(GRAPH_EFFECT_LIT_MAP_SLOTS); ++map) {
					if (!form.maps[map]) {
						continue;
					}
					file.Line(std::format("[[vk::binding({}, 1)]] Texture2D {};", mapBinding[map], GRAPH_EFFECT_LIT_MAP_SLOTS[map]));
					file.Line(std::format("[[vk::binding({}, 1)]] SamplerState {}Sampler;", mapBinding[map] + 1, GRAPH_EFFECT_LIT_MAP_SLOTS[map]));
					if (map == 2) {
						file.Line(std::format("#pragma shalltear_default {} normal", GRAPH_EFFECT_LIT_MAP_SLOTS[map]));
					}
					file.Line("");
				}
				DeclareTextures(file, stack.program, 1, slotBinding, [&output](u32, u32, const str& slot, const Guid& asset) {
					output.textures.push_back({slot, asset});
				});
				file.Lines(text.functions, text.functionLineNodes);
				const auto members = [&] {
					file.Line("\tfloat4 positionCS : SV_Position;");
					file.Line("\tfloat4 color : COLOR0;");
					if (uv) {
						file.Line("\tfloat4 uv : TEXCOORD0;");
					}
					if (blended) {
						file.Line("\tfloat blend : TEXCOORD1;");
					}
					if (softLine) {
						file.Line("\tnoperspective float pixelOffset : TEXCOORD2;");
					}
					if (form.threshold != GRAPH_NO_VALUE) {
						file.Line("\tnointerpolation float threshold : TEXCOORD3;");
					}
					if (form.soft) {
						file.Line("\tnointerpolation float softDistance : TEXCOORD4;");
					}
					if (form.lit) {
						file.Line("\tfloat3 positionWS : TEXCOORD5;");
						file.Line("\tfloat3 normalWS : TEXCOORD6;");
						file.Line("\tfloat4 tangentWS : TEXCOORD7;");
						if (form.bending) {
							file.Line("\tfloat2 bent : TEXCOORD8;");
						}
						file.Line("\tnointerpolation float4 material : TEXCOORD9;");
						if (emission) {
							file.Line("\tnointerpolation float3 emission : TEXCOORD10;");
						}
					} else {
						if (depth) {
							file.Line("#ifdef SHALLTEAR_DEPTH_PROGRAM");
							file.Line("\tfloat3 normalWS : TEXCOORD6;");
							file.Line("#endif");
						}
						file.Line("#ifndef SHALLTEAR_DEPTH_PROGRAM");
						file.Line("\tfloat3 positionWS : TEXCOORD5;");
						file.Line("#endif");
					}
				};
				file.Line("struct SltParticlePixel {");
				members();
				file.Line("};");
				file.Line("");
				file.Line("struct SltParticleVertex {");
				members();
				if (point) {
					file.Line("\t[[vk::builtin(\"PointSize\")]] float pointSize : PSIZE;");
				}
				file.Line("};");
				file.Line("");
				if (form.shape == OutputShape::Mesh) {
					file.Line("SltParticleVertex VSMain(uint sltVertexId : SV_VertexID, uint sltInstanceId : SV_InstanceID) {");
					file.Line("\tuint sltIndex = SltParticleOutputInstance(sltInstanceId);");
				} else if (strip) {
					StripVertexOpening(file);
				} else {
					file.Line("SltParticleVertex VSMain(uint sltVertexId : SV_VertexID) {");
					file.Line("\tuint sltIndex = SltParticleOutputIndex(sltVertexId);");
				}
				DeclareParticleId(file, stack.program, system, "SltParticleOutputLoad", {});
				DeclareProperties(file, stack.program);
				DeclareAttributes(file, stack.program, system, "SltParticleOutputLoad", system.strips > 0 ? StripRead::Output : StripRead::None);
				DeclareStripNeighbors(file, stack.program, system);
				file.Lines(text.body, text.lineNodes);
				file.Line("\tSltParticleVertex sltOut = (SltParticleVertex)0;");
				if (!strip) {
					file.Line(std::format("\tif (!({})) {{", read("alive", text)));
					file.Line("\t\tsltOut.positionCS = SLT_PARTICLE_CULLED;");
					file.Line("\t\treturn sltOut;");
					file.Line("\t}");
				}
				file.Line("\tfloat4x4 sltView = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);");
				file.Line("\tfloat4x4 sltProjection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],");
				file.Line("\t\t\t\t\t\t\t\t\t\t\t\t\t sltPassView.projection[2], sltPassView.projection[3]);");
				const auto toClip = [&](std::string_view out, std::string_view at) {
					if (!system.world) {
						file.Line(std::format("\t{0} = mul(SltParticleEffectLocalToWorld(), float4({0}, 1.0)).xyz;", at));
					}
					file.Line(std::format("\t{} = mul(sltProjection, mul(sltView, float4({}, 1.0)));", out, at));
				};
				const auto element = [&](std::string_view offset) {
					return std::format("SltParticleElementPosition({}, {}, {}, {}, {}, ({}) * ({}), {}, {})", read("axisX", text), read("axisY", text),
									   read("axisZ", text), read("angle", text), read("pivot", text), read("size", text), read("scale", text),
									   read("position", text), offset);
				};
				switch (form.shape) {
				case OutputShape::Primitive: {
					file.Line("\tuint sltCorner = SltParticleOutputCorner(sltVertexId);");
					if (form.primitive == PRIMITIVE_TRIANGLE) {
						file.Line("\tfloat3 sltOffset = float3(SltParticleTriangleCorner(sltCorner), 0.0);");
					} else if (form.primitive == PRIMITIVE_QUAD) {
						file.Line("\tfloat3 sltOffset = float3(SltParticleQuadCorner(sltCorner) * 0.5, 0.0);");
					} else {
						file.Line(std::format("\tfloat3 sltOffset = float3(SltParticleOctagonCorner(sltCorner, {}), 0.0);", root(Crop)));
					}
					file.Line(std::format("\tfloat3 sltWorld = {};", element("sltOffset")));
					toClip("sltOut.positionCS", "sltWorld");
					if (uv) {
						file.Line(form.primitive == PRIMITIVE_TRIANGLE ? "\tfloat2 sltUV = sltOffset.xy * 0.866025388240814208984375 + 0.5;"
																	   : "\tfloat2 sltUV = sltOffset.xy + 0.5;");
					}
					break;
				}
				case OutputShape::Mesh:
					file.Line(std::format("\tuint sltMeshIndex = SltParticleMeshIndex({}u, float(sltVertexId));", record));
					file.Line(std::format("\tfloat3 sltOffset = SltParticleMeshVertex({}u, float(sltMeshIndex), 0u).xyz;", record));
					file.Line(std::format("\tfloat3 sltWorld = {};", element("sltOffset")));
					toClip("sltOut.positionCS", "sltWorld");
					if (uv) {
						file.Line(std::format("\tfloat2 sltMeshUV = SltParticleMeshVertex({}u, float(sltMeshIndex), 4u).xy;", record));
						file.Line("\tfloat2 sltUV = float2(sltMeshUV.x, 1.0 - sltMeshUV.y);");
					}
					break;
				case OutputShape::Point:
					file.Line(std::format("\tfloat3 sltWorld = {};", read("position", text)));
					toClip("sltOut.positionCS", "sltWorld");
					file.Line("\tsltOut.pointSize = 1.0;");
					break;
				case OutputShape::Line:
					if (form.targetOffset) {
						file.Line(std::format("\tfloat3 sltStart = {};", element("float3(0.0, 0.0, 0.0)")));
						file.Line(std::format("\tfloat3 sltEnd = {};", element(root(Offset))));
					} else {
						file.Line(std::format("\tfloat3 sltStart = {};", read("position", text)));
						file.Line(std::format("\tfloat3 sltEnd = {};", read("targetPosition", text)));
					}
					file.Line("\tfloat4 sltClipStart;");
					file.Line("\tfloat4 sltClipEnd;");
					toClip("sltClipStart", "sltStart");
					toClip("sltClipEnd", "sltEnd");
					if (softLine) {
						file.Line(std::format("\tsltOut.positionCS = SltParticleLineCorner(sltClipStart, sltClipEnd, SltParticleOutputCorner(sltVertexId), "
											  "sltProjection, sltPassView, {}, sltOut.pixelOffset);",
											  opaque ? "1.0" : "2.0"));
					} else {
						file.Line("\tsltOut.positionCS = SltParticleOutputCorner(sltVertexId) == 0 ? sltClipStart : sltClipEnd;");
					}
					break;
				case OutputShape::Strip:
					StripVertex(file, form, read("size", text), read("scale", text),
								std::format("SltParticleElementPosition({}, {}, {}, {}, {}, sltSize, {}, sltOffset)", read("axisX", text), read("axisY", text),
											read("axisZ", text), read("angle", text), read("pivot", text), read("position", text)),
								uv, form.texCoord != GRAPH_NO_VALUE ? root(TexCoord) : str());
					toClip("sltOut.positionCS", "sltWorld");
					break;
				}
				const bool frame = hasElement && (form.lit || depth);
				if (frame && !form.lit) {
					file.Line("#ifdef SHALLTEAR_DEPTH_PROGRAM");
				}
				if (frame) {
					if (!strip) {
						file.Line(std::format("\tfloat3 sltSize = ({}) * ({});", read("size", text), read("scale", text)));
					}
					file.Line(std::format("\tfloat3x3 sltElement = SltParticleElementMatrix({}, {}, {}, {}, sltSize);", read("axisX", text), read("axisY", text),
										  read("axisZ", text), read("angle", text)));
					if (form.shape != OutputShape::Mesh) {
						file.Line("\tfloat3 sltNormal = SltParticleTransformNormal(sltElement, float3(0.0, 0.0, -1.0)) * (sltSize.x * sltSize.y * sltSize.z < 0.0 ? -1.0 : 1.0);");
						file.Line("\tfloat4 sltTangent = float4(normalize(mul(sltElement, float3(1.0, 0.0, 0.0))), 1.0);");
					} else {
						file.Line(std::format("\tfloat3 sltNormal = SltParticleTransformNormal(sltElement, SltParticleMeshVertex({}u, float(sltMeshIndex), 1u).xyz);", record));
						file.Line(std::format("\tfloat4 sltMeshTangent = SltParticleMeshVertex({}u, float(sltMeshIndex), 2u);", record));
						file.Line("\tfloat4 sltTangent = float4(normalize(mul(sltElement, sltMeshTangent.xyz)), sltMeshTangent.w < 0.0 ? -1.0 : 1.0);");
					}
					if (!system.world) {
						file.Line("\tfloat3x3 sltLocalToWorld = (float3x3)SltParticleEffectLocalToWorld();");
						file.Line("\tsltNormal = SltParticleTransformNormal(sltLocalToWorld, sltNormal);");
						file.Line("\tsltTangent.xyz = normalize(mul(sltLocalToWorld, sltTangent.xyz));");
						file.Line("\tsltTangent.w *= determinant(sltLocalToWorld) < 0.0 ? -1.0 : 1.0;");
					}
					file.Line("\tsltOut.normalWS = sltNormal;");
					if (form.lit) {
						file.Line("\tsltOut.tangentWS = sltTangent;");
					}
				} else if (depth && !form.lit) {
					file.Line("#ifdef SHALLTEAR_DEPTH_PROGRAM");
					file.Line("\tsltOut.normalWS = -normalize(sltPassView.inverseView[2].xyz);");
				}
				if (depth && !form.lit) {
					file.Line("#endif");
				}
				if (form.lit) {
					file.Line("\tsltOut.positionWS = sltWorld;");
					if (form.bending) {
						file.Line(std::format("\tsltOut.bent = sltOffset.xy * ({}) * 1.41421353816986083984375;", root(Bending)));
					}
					file.Line(std::format("\tsltOut.material = float4({}, {}, {}, {});", root(Smoothness), root(Metallic),
										  form.normalScale != GRAPH_NO_VALUE ? root(NormalScale) : str("1.0"),
										  form.emissiveScale != GRAPH_NO_VALUE ? root(EmissiveScale) : str("1.0")));
					if (emission) {
						file.Line(std::format("\tsltOut.emission = {};", form.ColorIsEmissive() ? read("color", text) : root(EmissiveColor)));
					}
				} else {
					file.Line("#ifndef SHALLTEAR_DEPTH_PROGRAM");
					file.Line(form.shape == OutputShape::Line ? "\tsltOut.positionWS = SltParticleWorldFromClip(sltOut.positionCS, sltPassView);"
															  : "\tsltOut.positionWS = sltWorld;");
					file.Line("#endif");
				}
				const str baseColor = form.lit && !form.UsesBaseColor() ? root(BaseColor) : read("color", text);
				file.Line(std::format("\tsltOut.color = float4({}, {});", baseColor, read("alpha", text)));
				if (form.threshold != GRAPH_NO_VALUE) {
					file.Line(std::format("\tsltOut.threshold = {};", root(Threshold)));
				}
				if (form.soft) {
					file.Line(std::format("\tsltOut.softDistance = {};", root(Soft)));
				}
				if (uv) {
					if (form.ScaleAndBias()) {
						file.Line(std::format("\tsltUV = sltUV * ({}) + ({});", text.roots[textureRoot], text.roots[textureRoot + 1]));
					}
					if (form.Flipbook()) {
						file.Line("\tfloat sltBlend;");
						file.Line(std::format("\tsltOut.uv = SltParticleFlipbookUVs({}, {}, {}, sltUV, sltBlend);", text.roots[textureRoot],
											  text.roots[textureRoot + 1], text.roots[textureRoot + 2]));
						if (blended) {
							file.Line("\tsltOut.blend = sltBlend;");
						}
					} else {
						file.Line("\tsltOut.uv = float4(SltParticleTextureUV(sltUV), 0.0, 0.0);");
					}
				}
				file.Line("\treturn sltOut;");
				file.Line("}");
				file.Line("");

				const auto sample = [&](std::string_view slot) {
					return blended ? std::format("lerp({0}.Sample({0}Sampler, sltIn.uv.xy), {0}.Sample({0}Sampler, sltIn.uv.zw), sltIn.blend)", slot)
								   : std::format("{0}.Sample({0}Sampler, sltIn.uv.xy)", slot);
				};
				const auto color = [&] {
					file.Line("\tfloat4 sltColor = sltIn.color;");
					if (form.Sampled()) {
						file.Line(std::format("\tfloat4 sltTexel = {};", sample(GRAPH_EFFECT_MAIN_TEXTURE)));
						if (form.map & 1u) {
							file.Line("\tsltColor.rgb *= sltTexel.rgb;");
						}
						if (form.map & 2u) {
							file.Line("\tsltColor.a *= sltTexel.a;");
						}
					}
				};
				const auto normal = [&] {
					file.Line("\tfloat3 sltNormal = normalize(sltIn.normalWS);");
					file.Line("\tfloat3 sltTangent = normalize(sltIn.tangentWS.xyz);");
					file.Line(form.shape == OutputShape::Primitive ? "\tfloat3 sltBitangent = cross(sltTangent, sltNormal) * sltIn.tangentWS.w;"
																   : "\tfloat3 sltBitangent = cross(sltNormal, sltTangent) * sltIn.tangentWS.w;");
					if (form.bending) {
						file.Line("\tSltParticleBendNormal(sltIn.bent, sltNormal, sltTangent, sltBitangent);");
						file.Line("\tsltBitangent = cross(sltTangent, sltNormal);");
					}
					if (form.doubleSided) {
						file.Line("\tif (!sltFront) {");
						file.Line("\t\tsltNormal = -sltNormal;");
						file.Line("\t\tsltTangent = -sltTangent;");
						file.Line("\t}");
					}
					if (form.maps[2]) {
						file.Line(std::format("\tfloat3 sltMapped = ShalltearUnpackNormalMap({}, 1.0);", sample(GRAPH_EFFECT_LIT_MAP_SLOTS[2])));
						file.Line("\tsltNormal = normalize(lerp(sltNormal, sltTangent * sltMapped.x + sltBitangent * sltMapped.y + sltNormal * sltMapped.z, "
								  "sltIn.material.z));");
					}
				};

				if (depth) {
					file.Line("#if defined(SHALLTEAR_DEPTH_PROGRAM)");
					file.Line("#if defined(SHALLTEAR_DEPTH_VALUE)");
					file.Line(form.lit ? "uint PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target0 {"
									   : "uint PSMain(SltParticlePixel sltIn) : SV_Target0 {");
					if (form.DepthClips()) {
						color();
						file.Line("\tclip(sltColor.a - sltIn.threshold);");
					}
					file.Line("\treturn sltParticleOutput.value;");
					file.Line("}");
					file.Line("#else");
					if (form.DepthClips()) {
						file.Line("#if defined(SHALLTEAR_DEPTH_NO_COLOR)");
						file.Line("void PSMain(SltParticlePixel sltIn) {");
						color();
						file.Line("\tclip(sltColor.a - sltIn.threshold);");
						file.Line("}");
						file.Line("#else");
					}
					file.Line(form.lit ? "float4 PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target0 {"
									   : "float4 PSMain(SltParticlePixel sltIn) : SV_Target0 {");
					if (form.DepthClips()) {
						color();
						file.Line("\tclip(sltColor.a - sltIn.threshold);");
					}
					if (form.lit) {
						normal();
					} else {
						file.Line("\tfloat3 sltNormal = normalize(sltIn.normalWS);");
					}
					file.Line("\treturn float4(sltNormal * 0.5 + 0.5, 1.0);");
					file.Line("}");
					if (form.DepthClips()) {
						file.Line("#endif");
					}
					file.Line("#endif");
					file.Line("#else");
				}
				file.Line(form.lit ? "float4 PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target {"
								   : "float4 PSMain(SltParticlePixel sltIn) : SV_Target {");
				color();
				if (softLine && !opaque) {
					file.Line("\tsltColor.a *= 1.0 - abs(sltIn.pixelOffset);");
				}
				if (form.soft) {
					file.Line("\tfloat sltSceneZ = sltSceneDepth.Load(int3(int2(sltIn.positionCS.xy), 0)).r;");
					file.Line("\tsltColor.a *= SltParticleSoftFade(sltSceneZ, SltParticleViewDepth(sltIn.positionCS.z, sltPassView), sltPassView, "
							  "sltIn.softDistance);");
				}
				if (form.clipping) {
					file.Line("\tclip(sltColor.a - sltIn.threshold);");
				}
				if (form.lit) {
					file.Line("\tShalltearSurface sltSurface = ShalltearDefaultSurface();");
					file.Line("\tsltSurface.baseColor = saturate(sltColor.rgb);");
					file.Line(opaque ? "\tsltSurface.alpha = 1.0;" : "\tsltSurface.alpha = saturate(sltColor.a);");
					file.Line("\tfloat sltSmoothness = sltIn.material.x;");
					file.Line("\tfloat sltMetallic = sltIn.material.y;");
					if (form.maps[1]) {
						file.Line(std::format("\tfloat4 sltMetallicMap = {};", sample(GRAPH_EFFECT_LIT_MAP_SLOTS[1])));
						file.Line("\tsltMetallic *= sltMetallicMap.r;");
					}
					if (form.smoothnessSource == 1) {
						file.Line("\tsltSmoothness *= sltMetallicMap.a;");
					} else if (form.smoothnessSource == 2) {
						file.Line("\tsltSmoothness *= sltTexel.a;");
					}
					file.Line("\tsltSurface.roughness = 1.0 - saturate(sltSmoothness);");
					file.Line("\tsltSurface.metallic = saturate(sltMetallic);");
					if (form.maps[0]) {
						file.Line(std::format("\tsltSurface.occlusion = {}.g;", sample(GRAPH_EFFECT_LIT_MAP_SLOTS[0])));
					}
					if (form.Emits()) {
						file.Line("\tfloat3 sltEmission = float3(1.0, 1.0, 1.0);");
						if (form.maps[3]) {
							file.Line(std::format("\tsltEmission *= {}.rgb * sltIn.material.w;", sample(GRAPH_EFFECT_LIT_MAP_SLOTS[3])));
						}
						if (emission) {
							file.Line("\tsltEmission *= sltIn.emission;");
						}
						file.Line("\tsltSurface.emissive = sltEmission;");
					}
					normal();
					file.Line("\tShalltearSurfaceInput sltInput = SltParticleSurfaceInput(sltIn.positionCS, sltIn.positionWS, sltNormal, "
							  "float4(sltTangent, sltIn.tangentWS.w), sltFront);");
					file.Line(std::format("\tfloat3 sltLit = ShalltearShadeSurface(sltInput, sltSurface, sltNormal, SltParticleReceivesShadows({}, {})) + "
										  "sltSurface.emissive;",
										  form.receiveShadows ? "true" : "false", opaque ? "false" : "true"));
					file.Line("\tsltColor = float4(sltLit, sltSurface.alpha);");
				}
				if (output.blend == RHI::BlendMode::Premultiplied) {
					file.Line(FoggedPixel("float4(sltColor.rgb * sltColor.a, sltColor.a)", output.blend));
				} else if (opaque) {
					file.Line(FoggedPixel("float4(sltColor.rgb, 1.0)", output.blend));
				} else {
					file.Line(FoggedPixel("sltColor", output.blend));
				}
				file.Line("}");
				if (depth) {
					file.Line("#endif");
				}
				output.shader = std::move(file.text);
				output.lineNodes = std::move(file.lines);
				system.outputs.push_back(std::move(output));
			}

			void EmitNodeShaderOutput(const Stack& stack, const GraphContext& context, GraphEffectSystem& system, const OutputForm& form,
									  const GraphEffect& effect) {
				const GraphSurfaceFunctions& graph = form.functions;
				const u32 id = context.node.id;
				GraphEffectOutput output;
				output.context = id;
				output.blend = form.graphBlend;
				output.sort = form.sort;
				output.revertSorting = form.revert;
				output.vertices = form.Vertices();
				output.castShadows = form.castShadows;
				output.strip = form.shape == OutputShape::Strip;
				const bool opaque = output.blend == RHI::BlendMode::Opaque;
				const bool lights = graph.target != GraphSurfaceFunctions::Target::Unlit;
				const bool depth = form.DepthProgram();
				const bool mesh = form.shape == OutputShape::Mesh;
				const bool strip = output.strip;

				u32 graphTextures = 0;
				for (const GraphPropertyInfo& property : graph.properties) {
					if (!GraphProperties::IsTexture(property.kind)) {
						continue;
					}
					++graphTextures;
					for (const GraphInput& input : context.node.inputs) {
						if (!property.hidden && input.port == property.reference && input.fromNode != 0) {
							Report(id, std::format("has its {} fed by a node (an effect takes a texture asset on the port)", property.reference));
							return;
						}
					}
					output.textures.push_back({property.reference, property.hidden ? Guid{} : GraphEffectTarget::HeldTexture(context.node, property.reference)});
				}
				u32 record = 0;
				if (mesh && !MeshRecord(context, effect, output, record)) {
					return;
				}

				const std::span<const std::string_view> reads = OutputReads(form);
				GraphHLSLText text;
				GraphHLSL::Emit(stack.program, GraphStageVertex, text, &graph.helpers);
				const auto read = [&](std::string_view name) -> const str& {
					return text.roots[static_cast<size_t>(std::find(reads.begin(), reads.end(), name) - reads.begin())];
				};
				u32 next = static_cast<u32>(reads.size());
				const std::array<GraphValueId, 12> extras = form.Extras();
				u32 extraRoot[12] = {};
				for (u32 i = 0; i < extras.size(); ++i) {
					if (extras[i] != GRAPH_NO_VALUE) {
						extraRoot[i] = next++;
					}
				}
				constexpr u32 CROP = 0;
				constexpr u32 THRESHOLD = 2;
				constexpr u32 TEX_COORD = 11;
				Vector<u32> propertyRoot{GetMemoryResource(MemTag::Editor)};
				propertyRoot.assign(graph.properties.size(), ~0u);
				for (size_t p = 0; p < graph.properties.size(); ++p) {
					if (form.propertyValues[p] != GRAPH_NO_VALUE) {
						propertyRoot[p] = next++;
					}
				}
				const auto rootType = [&](u32 root) { return stack.program.At(stack.program.roots[root].value).type; };

				struct Handed {
					bool read = false;
					str literal;
					u32 slot = 0;
					u32 offset = 0;
					u32 width = 0;
				};
				Vector<Handed> handed{GetMemoryResource(MemTag::Editor)};
				handed.resize(graph.properties.size());
				Vector<u32> slotWidths{GetMemoryResource(MemTag::Editor)};
				for (size_t p = 0; p < graph.properties.size(); ++p) {
					if (propertyRoot[p] == ~0u || (graph.propertyStages[p] & (GraphStageFragment | GraphStageLighting)) == 0) {
						continue;
					}
					Handed& entry = handed[p];
					entry.read = true;
					if (stack.program.At(stack.program.roots[propertyRoot[p]].value).op == GraphOp::Constant) {
						entry.literal = text.roots[propertyRoot[p]];
						continue;
					}
					entry.width = GraphTypes::Components(rootType(propertyRoot[p]));
					u32 slot = 0;
					while (slot < slotWidths.size() && slotWidths[slot] + entry.width > 4) {
						++slot;
					}
					if (slot == slotWidths.size()) {
						slotWidths.push_back(0);
					}
					entry.slot = slot;
					entry.offset = slotWidths[slot];
					slotWidths[slot] += entry.width;
				}
				const u32 propertySlots = static_cast<u32>(slotWidths.size());
				const u32 interpolatorSlots = graph.interpolators[0] + graph.interpolators[1] + graph.interpolators[2];
				constexpr u32 LOCATIONS = 32;
				const u32 locations = 5 + (graph.fragmentReadsObject ? 3u : 0u) + propertySlots + (form.threshold != GRAPH_NO_VALUE ? 1u : 0u) + interpolatorSlots;
				if (locations > LOCATIONS) {
					Report(id, std::format("passes its px stage {} varyings, more than the {} a device supports ('{}' reads {} slots of ports per px and {} "
										   "of custom interpolators)",
										   locations, LOCATIONS, form.graphName, propertySlots, interpolatorSlots));
					return;
				}

				Writer file;
				Header(file, std::format("the Node Shader {} of Output context {}, shaded by '{}'", form.Name(), id, form.graphName));
				file.Line(std::format("#pragma shalltear_blend {}", RHI::BlendModeName(output.blend)));
				file.Line(std::format("#pragma shalltear_depth {}", RHI::DepthModeName(graph.depth)));
				file.Line(std::format("#pragma shalltear_cull {}", RHI::CullModeName(graph.cull)));
				if (depth) {
					file.Line(form.DepthClips() ? "#pragma shalltear_depth_program clip value" : "#pragma shalltear_depth_program value");
				}
				for (const GraphPropertyInfo& property : graph.properties) {
					if (property.kind == GraphPropertyKind::Texture2D) {
						file.Line(std::format("#pragma shalltear_default {} {}", property.reference, property.fallback));
					}
				}
				file.Line("");
				for (const str& keyword : form.keywords) {
					file.Line("#define " + keyword);
				}
				if (graph.target == GraphSurfaceFunctions::Target::CustomLighting) {
					file.Line("#define SHALLTEAR_CUSTOM_LIGHTING");
				}
				file.Line("#include \"VisualEffects/ParticleOutput.hlsli\"");
				file.Line("#include \"VisualEffects/ParticleGraph.hlsli\"");
				file.Line("#include \"Brdf.hlsli\"");
				file.Line("#ifndef SHALLTEAR_DEPTH_PROGRAM");
				file.Line("#include \"Fog.hlsli\"");
				file.Line("#endif");
				if (ReadsExternal(stack.program, GRAPH_EFFECT_CAMERA_CLIP_W)) {
					file.Line("#include \"VisualEffects/ParticleCamera.hlsli\"");
				}
				file.Line("");
				file.Line("[[vk::binding(0, 2)]] cbuffer sltViewRecord {");
				file.Line("\tShalltearViewData sltPassView;");
				file.Line("};");
				file.Line("");
				file.Lines(GraphProperties::HostDeclarations(graph.properties, 0), {});
				DeclareTextures(file, stack.program, 1, 2 * graphTextures, [&output](u32, u32, const str& slot, const Guid& asset) {
					output.textures.push_back({slot, asset});
				});
				constexpr u32 MATERIAL_TEXTURES = 7;
				if (output.textures.size() > MATERIAL_TEXTURES) {
					Report(id, std::format("samples {} textures, past the {} an output's material binds", output.textures.size(), MATERIAL_TEXTURES));
					return;
				}
				file.Lines(text.functions, text.functionLineNodes);
				Vector<u32> graphLines{GetMemoryResource(MemTag::Editor)};
				for (u32 node : graph.lines) {
					graphLines.push_back(node != 0 ? id : 0u);
				}
				file.Lines(graph.text, graphLines);
				if (lights) {
					file.Line("#ifndef SHALLTEAR_DEPTH_PROGRAM");
					file.Line("#include \"VisualEffects/ParticleLit.hlsli\"");
					file.Line("#endif");
				}
				file.Line("");

				str parameters;
				Vector<str> assignments{GetMemoryResource(MemTag::Editor)};
				for (size_t p = 0; p < graph.properties.size(); ++p) {
					if (propertyRoot[p] == ~0u) {
						continue;
					}
					const GraphPropertyInfo& property = graph.properties[p];
					const str parameter = std::format("sltValue{}", p);
					parameters += (parameters.empty() ? "" : ", ") + str(GraphTypes::HLSLName(rootType(propertyRoot[p]))) + " " + parameter;
					const str member = GraphProperties::MemberName(property);
					if (property.kind == GraphPropertyKind::Integer) {
						assignments.push_back(std::format("\t{} = (int)({});", member, parameter));
					} else if (property.kind == GraphPropertyKind::Boolean) {
						assignments.push_back(std::format("\t{} = ({}) ? 1 : 0;", member, parameter));
					} else {
						assignments.push_back(std::format("\t{} = {};", member, parameter));
					}
				}
				const bool setter = !assignments.empty();
				if (setter) {
					file.Line("void SltGraphProperties(" + parameters + ") {");
					for (const str& line : assignments) {
						file.Line(line);
					}
					file.Line("}");
					file.Line("");
				}
				const auto call = [&](bool vertex) {
					str arguments;
					for (size_t p = 0; p < graph.properties.size(); ++p) {
						if (propertyRoot[p] == ~0u) {
							continue;
						}
						str argument;
						const GraphValueType type = rootType(propertyRoot[p]);
						if (vertex) {
							argument = text.roots[propertyRoot[p]];
						} else if (!handed[p].read) {
							argument = std::format("({})0", GraphTypes::HLSLName(type));
						} else if (!handed[p].literal.empty()) {
							argument = handed[p].literal;
						} else {
							const str components = std::format("sltIn.properties[{}].{}", handed[p].slot, std::string_view("xyzw").substr(handed[p].offset, handed[p].width));
							argument = type == T::Bool ? components + " != 0.0" : components;
						}
						arguments += (arguments.empty() ? "" : ", ") + argument;
					}
					file.Line("\tSltGraphProperties(" + arguments + ");");
				};

				const auto members = [&] {
					file.Line("\tfloat4 positionCS : SV_Position;");
					file.Line("\tfloat3 positionWS : TEXCOORD0;");
					file.Line("\tfloat3 normalWS : TEXCOORD1;");
					file.Line("\tfloat4 tangentWS : TEXCOORD2;");
					file.Line("\tcentroid float2 uv : TEXCOORD3;");
					file.Line("\tcentroid float4 color : COLOR0;");
					if (graph.fragmentReadsObject) {
						file.Line("\tnointerpolation float4 objectToWorld[3] : SLT_OBJECT;");
					}
					if (propertySlots > 0) {
						file.Line(std::format("\tnointerpolation float4 properties[{}] : SLT_PROPERTIES;", propertySlots));
					}
					if (form.threshold != GRAPH_NO_VALUE) {
						file.Line("\tnointerpolation float threshold : SLT_THRESHOLD;");
					}
					if (graph.interpolators[0] > 0) {
						file.Line(std::format("\tfloat4 interpolatorsLinear[{}] : SHALLTEAR_LINEAR;", graph.interpolators[0]));
					}
					if (graph.interpolators[1] > 0) {
						file.Line(std::format("\tnoperspective float4 interpolatorsNoPerspective[{}] : SHALLTEAR_NO_PERSPECTIVE;", graph.interpolators[1]));
					}
					if (graph.interpolators[2] > 0) {
						file.Line(std::format("\tnointerpolation float4 interpolatorsFlat[{}] : SHALLTEAR_FLAT;", graph.interpolators[2]));
					}
				};
				file.Line("struct SltParticlePixel {");
				members();
				file.Line("};");
				file.Line("");
				file.Line("struct SltParticleVertex {");
				members();
				file.Line("};");
				file.Line("");

				if (mesh) {
					file.Line("SltParticleVertex VSMain(uint sltVertexId : SV_VertexID, uint sltInstanceId : SV_InstanceID) {");
					file.Line("\tuint sltIndex = SltParticleOutputInstance(sltInstanceId);");
				} else if (strip) {
					StripVertexOpening(file);
				} else {
					file.Line("SltParticleVertex VSMain(uint sltVertexId : SV_VertexID) {");
					file.Line("\tuint sltIndex = SltParticleOutputIndex(sltVertexId);");
				}
				DeclareParticleId(file, stack.program, system, "SltParticleOutputLoad", {});
				DeclareProperties(file, stack.program);
				DeclareAttributes(file, stack.program, system, "SltParticleOutputLoad", system.strips > 0 ? StripRead::Output : StripRead::None);
				DeclareStripNeighbors(file, stack.program, system);
				file.Lines(text.body, text.lineNodes);
				file.Line("\tSltParticleVertex sltOut = (SltParticleVertex)0;");
				if (!strip) {
					file.Line(std::format("\tif (!({})) {{", read("alive")));
					file.Line("\t\tsltOut.positionCS = SLT_PARTICLE_CULLED;");
					file.Line("\t\treturn sltOut;");
					file.Line("\t}");
				}
				file.Line("\tfloat4x4 sltView = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);");
				file.Line("\tfloat4x4 sltProjection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],");
				file.Line("\t\t\t\t\t\t\t\t\t\t\t\t\t sltPassView.projection[2], sltPassView.projection[3]);");
				file.Line(strip ? std::format("\tfloat3 sltSize = SltParticleStripSize({}, {});", read("size"), read("scale"))
								: std::format("\tfloat3 sltSize = ({}) * ({});", read("size"), read("scale")));
				file.Line(std::format("\tfloat3x3 sltElement = SltParticleElementMatrix({}, {}, {}, {}, sltSize);", read("axisX"), read("axisY"), read("axisZ"),
									  read("angle")));
				file.Line(std::format("\tfloat4x4 sltObjectToWorld = SltParticleObjectToWorld(sltElement, ({}) - mul(sltElement, {}));", read("position"),
									  read("pivot")));
				if (!system.world) {
					file.Line("\tsltObjectToWorld = mul(SltParticleEffectLocalToWorld(), sltObjectToWorld);");
				}
				file.Line("\tfloat4x4 sltWorldToObject = SltParticleAffineInverse(sltObjectToWorld);");
				file.Line("\tfloat sltFlip = sltSize.x * sltSize.y * sltSize.z < 0.0 ? -1.0 : 1.0;");
				if (mesh) {
					file.Line(std::format("\tuint sltMeshIndex = SltParticleMeshIndex({}u, float(sltVertexId));", record));
					file.Line(std::format("\tShalltearVertexInput sltVertex = SltParticleGraphVertexInput(SltParticleMeshVertex({0}u, float(sltMeshIndex), 0u).xyz, "
										  "SltParticleMeshVertex({0}u, float(sltMeshIndex), 1u).xyz * sltFlip, SltParticleMeshVertex({0}u, float(sltMeshIndex), 2u), "
										  "SltParticleMeshVertex({0}u, float(sltMeshIndex), 4u).xy, SltParticleMeshVertex({0}u, float(sltMeshIndex), 3u), sltMeshIndex, "
										  "sltObjectToWorld, sltWorldToObject);",
										  record));
				} else {
					file.Line("\tuint sltCorner = SltParticleOutputCorner(sltVertexId);");
					if (strip) {
						file.Line("\tfloat2 sltOffset = float2(0.0, sltQuad.y * 0.5);");
						StripUV(file, form, form.texCoord != GRAPH_NO_VALUE ? std::string_view(text.roots[extraRoot[TEX_COORD]]) : std::string_view());
					} else if (form.primitive == PRIMITIVE_TRIANGLE) {
						file.Line("\tfloat2 sltOffset = SltParticleTriangleCorner(sltCorner);");
						file.Line("\tfloat2 sltUV = sltOffset * 0.866025388240814208984375 + 0.5;");
					} else if (form.primitive == PRIMITIVE_QUAD) {
						file.Line("\tfloat2 sltOffset = SltParticleQuadCorner(sltCorner) * 0.5;");
						file.Line("\tfloat2 sltUV = sltOffset + 0.5;");
					} else {
						file.Line(std::format("\tfloat2 sltOffset = SltParticleOctagonCorner(sltCorner, {});", text.roots[extraRoot[CROP]]));
						file.Line("\tfloat2 sltUV = sltOffset + 0.5;");
					}
					file.Line("\tShalltearVertexInput sltVertex = SltParticleGraphVertexInput(float3(sltOffset, 0.0), float3(0.0, 0.0, -1.0) * sltFlip, "
							  "float4(1.0, 0.0, 0.0, -1.0), SltParticleTextureUV(sltUV), float4(1.0, 1.0, 1.0, 1.0), sltVertexId, sltObjectToWorld, "
							  "sltWorldToObject);");
				}
				if (setter) {
					call(true);
				}
				if (graph.vertexFunction) {
					file.Line("\tShalltearVertexOutput sltDisplaced = ShalltearDefaultVertexOutput(sltVertex);");
					file.Line("\tShalltearVertexFunction(sltVertex, sltDisplaced);");
					file.Line("\tsltVertex.positionOS = sltDisplaced.positionOS;");
					file.Line("\tsltVertex.normalOS = sltDisplaced.normalOS;");
					file.Line("\tsltVertex.tangentOS.xyz = sltDisplaced.tangentOS;");
					file.Line("\tSltParticleGraphPlace(sltVertex);");
					constexpr std::string_view ARRAYS[] = {"interpolatorsLinear", "interpolatorsNoPerspective", "interpolatorsFlat"};
					u32 first = 0;
					for (u32 mode = 0; mode < 3; ++mode) {
						for (u32 slot = 0; slot < graph.interpolators[mode]; ++slot) {
							file.Line(std::format("\tsltOut.{}[{}] = sltDisplaced.interpolators[{}];", ARRAYS[mode], slot, first + slot));
						}
						first += graph.interpolators[mode];
					}
				}
				file.Line("\tsltOut.positionCS = mul(sltProjection, mul(sltView, float4(sltVertex.positionWS, 1.0)));");
				file.Line("\tsltOut.positionWS = sltVertex.positionWS;");
				file.Line("\tsltOut.normalWS = sltVertex.normalWS;");
				file.Line("\tsltOut.tangentWS = sltVertex.tangentWS;");
				file.Line("\tsltOut.uv = sltVertex.uv;");
				file.Line("\tsltOut.color = sltVertex.color;");
				if (graph.fragmentReadsObject) {
					file.Line("\tsltOut.objectToWorld[0] = sltObjectToWorld[0];");
					file.Line("\tsltOut.objectToWorld[1] = sltObjectToWorld[1];");
					file.Line("\tsltOut.objectToWorld[2] = sltObjectToWorld[2];");
				}
				for (size_t p = 0; p < graph.properties.size(); ++p) {
					const Handed& entry = handed[p];
					if (!entry.read || !entry.literal.empty()) {
						continue;
					}
					const str value = text.roots[propertyRoot[p]];
					const std::string_view components = std::string_view("xyzw").substr(entry.offset, entry.width);
					file.Line(std::format("\tsltOut.properties[{}].{} = {};", entry.slot, components,
										  rootType(propertyRoot[p]) == T::Bool ? "(float)(" + value + ")" : value));
				}
				if (form.threshold != GRAPH_NO_VALUE) {
					file.Line(std::format("\tsltOut.threshold = {};", text.roots[extraRoot[THRESHOLD]]));
				}
				file.Line("\treturn sltOut;");
				file.Line("}");
				file.Line("");

				file.Line("ShalltearSurface SltGraphSurface(SltParticlePixel sltIn, bool sltFront, out ShalltearSurfaceInput sltInput) {");
				if (setter) {
					call(false);
				}
				file.Line(graph.fragmentReadsObject
							  ? "\tfloat4x4 sltObjectToWorld = float4x4(sltIn.objectToWorld[0], sltIn.objectToWorld[1], sltIn.objectToWorld[2], float4(0.0, 0.0, 0.0, 1.0));"
							  : "\tfloat4x4 sltObjectToWorld = float4x4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);");
				file.Line("\tsltInput = SltParticleGraphSurfaceInput(sltIn.positionCS, sltIn.positionWS, sltIn.normalWS, sltIn.tangentWS, sltIn.uv, sltIn.color, "
						  "sltFront, sltObjectToWorld);");
				{
					constexpr std::string_view ARRAYS[] = {"interpolatorsLinear", "interpolatorsNoPerspective", "interpolatorsFlat"};
					u32 first = 0;
					for (u32 mode = 0; mode < 3; ++mode) {
						for (u32 slot = 0; slot < graph.interpolators[mode]; ++slot) {
							file.Line(std::format("\tsltInput.interpolators[{}] = sltIn.{}[{}];", first + slot, ARRAYS[mode], slot));
						}
						first += graph.interpolators[mode];
					}
				}
				file.Line("\tShalltearSurface sltSurface = ShalltearDefaultSurface();");
				file.Line("\tShalltearSurfaceFunction(sltInput, sltSurface);");
				file.Line("\treturn sltSurface;");
				file.Line("}");
				file.Line("");
				if (depth) {
					file.Line("#if defined(SHALLTEAR_DEPTH_PROGRAM)");
					file.Line("#if defined(SHALLTEAR_DEPTH_VALUE)");
					file.Line("uint PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target0 {");
					if (form.DepthClips()) {
						file.Line("\tShalltearSurfaceInput sltInput;");
						file.Line("\tShalltearSurface sltSurface = SltGraphSurface(sltIn, sltFront, sltInput);");
						file.Line("\tclip(sltSurface.alpha - sltIn.threshold);");
					}
					file.Line("\treturn sltParticleOutput.value;");
					file.Line("}");
					file.Line("#else");
					if (form.DepthClips()) {
						file.Line("#if defined(SHALLTEAR_DEPTH_NO_COLOR)");
						file.Line("void PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) {");
						file.Line("\tShalltearSurfaceInput sltInput;");
						file.Line("\tShalltearSurface sltSurface = SltGraphSurface(sltIn, sltFront, sltInput);");
						file.Line("\tclip(sltSurface.alpha - sltIn.threshold);");
						file.Line("}");
						file.Line("#else");
					}
					file.Line("float4 PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target0 {");
					file.Line("\tShalltearSurfaceInput sltInput;");
					file.Line("\tShalltearSurface sltSurface = SltGraphSurface(sltIn, sltFront, sltInput);");
					if (form.DepthClips()) {
						file.Line("\tclip(sltSurface.alpha - sltIn.threshold);");
					}
					file.Line("\treturn float4(ShalltearTransformSurfaceNormal(sltInput, sltSurface.normal) * 0.5 + 0.5, 1.0);");
					file.Line("}");
					if (form.DepthClips()) {
						file.Line("#endif");
					}
					file.Line("#endif");
					file.Line("#else");
				}
				file.Line("float4 PSMain(SltParticlePixel sltIn, bool sltFront : SV_IsFrontFace) : SV_Target {");
				file.Line("\tShalltearSurfaceInput sltInput;");
				file.Line("\tShalltearSurface sltSurface = SltGraphSurface(sltIn, sltFront, sltInput);");
				if (form.clipping) {
					file.Line("\tclip(sltSurface.alpha - sltIn.threshold);");
				}
				if (lights) {
					file.Line("\tfloat3 sltNormal = ShalltearTransformSurfaceNormal(sltInput, sltSurface.normal);");
					file.Line(std::format("\tfloat3 sltColor = ShalltearShadeSurface(sltInput, sltSurface, sltNormal, SltParticleReceivesShadows({}, {})) + "
										  "sltSurface.emissive;",
										  form.receiveShadows ? "true" : "false", opaque ? "false" : "true"));
				} else {
					file.Line("\tfloat3 sltColor = sltSurface.baseColor + sltSurface.emissive;");
				}
				if (output.blend == RHI::BlendMode::Premultiplied) {
					file.Line(FoggedPixel("float4(sltColor * sltSurface.alpha, sltSurface.alpha)", output.blend));
				} else if (opaque) {
					file.Line(FoggedPixel("float4(sltColor, 1.0)", output.blend));
				} else {
					file.Line(FoggedPixel("float4(sltColor, sltSurface.alpha)", output.blend));
				}
				file.Line("}");
				if (depth) {
					file.Line("#endif");
				}
				output.shader = std::move(file.text);
				output.lineNodes = std::move(file.lines);
				system.outputs.push_back(std::move(output));
			}

			void EmitSortKernel(const Stack& stack, const GraphContext& context, GraphEffectSystem& system, const OutputForm& form) {
				if (Failed() || form.sort == GraphEffectSort::None || system.outputs.empty()) {
					return;
				}
				GraphHLSLText text;
				GraphHLSL::Emit(stack.program, GraphStageVertex, text);
				Writer file;
				Header(file, std::format("the sort key kernel of Output context {}", context.node.id));
				file.Line("#pragma shalltear_kernel");
				file.Line("#include \"VisualEffects/ParticleSort.hlsli\"");
				file.Line("");
				file.Lines(text.functions, text.functionLineNodes);
				file.Line("[numthreads(SLT_PARTICLE_SORT_THREADS, 1, 1)]");
				file.Line("void CSMain(uint3 sltId : SV_DispatchThreadID) {");
				file.Line("\tSltParticleEnter(sltParticle.firstSlot);");
				file.Line("\tuint sltStride = SltParticleSortStride();");
				file.Line("\tfor (uint sltSlot = sltId.x; sltSlot < sltParticle.sortCount; sltSlot += sltStride) {");
				if (form.shape == OutputShape::Strip) {
					file.Line("\t\tuint sltIndex = sltSlot < sltParticle.capacity && SltParticleStripBegins(sltSlot) ? sltSlot : sltParticle.capacity;");
				} else {
					file.Line("\t\tuint sltIndex = sltSlot < SltParticleAliveInCount() ? SltParticleAliveIn(sltSlot) : sltParticle.capacity;");
				}
				file.Line("\t\tif (sltIndex >= sltParticle.capacity) {");
				file.Line("\t\t\tSltParticleSortPad(sltSlot);");
				file.Line("\t\t\tcontinue;");
				file.Line("\t\t}");
				DeclareParticleId(file, stack.program, system, "SltParticleLoad", {});
				DeclareProperties(file, stack.program);
				DeclareAttributes(file, stack.program, system, "SltParticleLoad", system.strips > 0 ? StripRead::Kernel : StripRead::None);
				file.Lines(text.body, text.lineNodes);
				const str& root = text.roots[0];
				const str world = system.world ? root : std::format("mul(SltParticleEffectLocalToWorld(), float4({}, 1.0)).xyz", root);
				str key;
				switch (form.sort) {
				case GraphEffectSort::DistanceToCamera:
					key = std::format("SltParticleSortDistance({})", world);
					break;
				case GraphEffectSort::CameraDepth:
					key = std::format("SltParticleSortDepth({})", world);
					break;
				case GraphEffectSort::Custom:
					key = std::format("-({})", root);
					break;
				default:
					key = root;
					break;
				}
				file.Line(std::format("\t\tSltParticleSortStore(sltSlot, sltIndex, {});", key));
				file.Line("\t}");
				file.Line("}");
				GraphEffectOutput& output = system.outputs.back();
				output.sortKernel = std::move(file.text);
				output.sortLines = std::move(file.lines);
			}

			const GraphDocument& _document;
			const GraphDocument& _root;
			std::string_view _sourceName;
			GraphDefinitions& _definitions;
			Vector<GraphDiagnostic>& _diagnostics;
			GraphEffectCompileOptions _options;
			bool _inner = false;
			Vector<u32> _within{GetMemoryResource(MemTag::Editor)};
			u32 _report = 0;
			str _where;
			str _label;
			Vector<ChainLink> _systemChain{GetMemoryResource(MemTag::Editor)};
			HashMap<str, SlotFeed> _slots{GetMemoryResource(MemTag::Editor)};
			HashMap<u32, u32> _spawnerOf{GetMemoryResource(MemTag::Editor)};
			Vector<const GraphContext*> _contexts{GetMemoryResource(MemTag::Editor)};
			const Vector<GraphPropertyInfo>* _properties = nullptr;
			const Vector<GraphEffectField>* _propertyFields = nullptr;
			Vector<GraphBinding> _portTextures{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> _portTextureAssets{GetMemoryResource(MemTag::Editor)};
			Vector<GraphBinding> _cpuBindings{GetMemoryResource(MemTag::Editor)};
			Vector<GraphBinding> _portMeshes{GetMemoryResource(MemTag::Editor)};
			u32 _meshesOffset = 0;
			u32 _cameraOffset = 0;
			HashMap<u32, Vector<u32>> _wiredEvents{GetMemoryResource(MemTag::Editor)};
			HashMap<u32, u32> _eventSystem{GetMemoryResource(MemTag::Editor)};
			HashMap<u32, u32> _feedingEvent{GetMemoryResource(MemTag::Editor)};
			HashMap<u32, u32> _parentOf{GetMemoryResource(MemTag::Editor)};
			HashMap<u32, Vector<u32>> _wiredOutputEvents{GetMemoryResource(MemTag::Editor)};
			Vector<str> _accumulatorNames{GetMemoryResource(MemTag::Editor)};
			Vector<GraphEffectAttributeInfo> _accumulators{GetMemoryResource(MemTag::Editor)};
			Vector<GraphBinding> _accumulatorBindings{GetMemoryResource(MemTag::Editor)};
			Vector<SystemBuild> _builds{GetMemoryResource(MemTag::Editor)};
			bool _failed = false;
		};

	} // anonymous namespace

	namespace GraphEffectSplice {

		bool Append(GraphProgram& into, const GraphProgram& block, const HashMap<str, GraphValueId>& current, Vector<GraphValueId>& roots) {
			constexpr u32 NEW = ~0u;
			Vector<u32> callees{GetMemoryResource(MemTag::Editor)};
			for (const GraphCall& call : block.calls) {
				u32 index = NEW;
				for (u32 i = 0; i < into.calls.size(); ++i) {
					const GraphCall& held = into.calls[i];
					if (held.function != call.function) {
						continue;
					}
					if (held.definition != call.definition || held.include != call.include) {
						return false;
					}
					index = i;
				}
				callees.push_back(index);
			}
			for (size_t i = 0; i < callees.size(); ++i) {
				if (callees[i] == NEW) {
					callees[i] = static_cast<u32>(into.calls.size());
					into.calls.push_back(block.calls[i]);
				}
			}
			Vector<GraphValueId> mapped{GetMemoryResource(MemTag::Editor)};
			mapped.resize(block.values.size(), GRAPH_NO_VALUE);
			for (GraphValueId id = 0; id < block.values.size(); ++id) {
				GraphValue value = block.values[id];
				if (value.op == GraphOp::External) {
					const GraphExternal& external = block.externals[value.payload];
					if (auto written = current.find(external.name); written != current.end()) {
						mapped[id] = written->second;
						continue;
					}
					u32 index = static_cast<u32>(into.externals.size());
					for (u32 i = 0; i < into.externals.size(); ++i) {
						const GraphExternal& held = into.externals[i];
						if (held.name == external.name && held.expression == external.expression && held.sampler == external.sampler) {
							index = i;
						}
					}
					if (index == into.externals.size()) {
						into.externals.push_back(external);
					}
					value.payload = index;
				} else if (value.op == GraphOp::Constant) {
					const std::span<const f32> components = block.ConstantOf(id);
					value.payload = static_cast<u32>(into.constants.size());
					into.constants.insert(into.constants.end(), components.begin(), components.end());
				} else if (value.op == GraphOp::Call) {
					value.payload = callees[value.payload];
				}
				for (u32 operand = 0; operand < value.operandCount; ++operand) {
					value.operands[operand] = mapped[value.operands[operand]];
				}
				mapped[id] = static_cast<GraphValueId>(into.values.size());
				into.values.push_back(value);
				into.labels.push_back(id < block.labels.size() ? block.labels[id] : str());
			}
			roots.clear();
			for (const GraphRoot& root : block.roots) {
				roots.push_back(root.value == GRAPH_NO_VALUE ? GRAPH_NO_VALUE : mapped[root.value]);
			}
			return true;
		}

	} // namespace GraphEffectSplice

	namespace GraphEffectAssembly {

		namespace {

			std::string_view Mnemonic(GraphOp op) {
				switch (op) {
				case GraphOp::Negate:
					return "negate";
				case GraphOp::Absolute:
					return "abs";
				case GraphOp::Sign:
					return "sign";
				case GraphOp::Floor:
					return "floor";
				case GraphOp::Ceiling:
					return "ceil";
				case GraphOp::Round:
					return "round";
				case GraphOp::Truncate:
					return "trunc";
				case GraphOp::Fraction:
					return "frac";
				case GraphOp::Saturate:
					return "saturate";
				case GraphOp::SquareRoot:
					return "sqrt";
				case GraphOp::ReciprocalSquareRoot:
					return "rsqrt";
				case GraphOp::Reciprocal:
					return "rcp";
				case GraphOp::Exponential:
					return "exp";
				case GraphOp::Exponential2:
					return "exp2";
				case GraphOp::Logarithm:
					return "log";
				case GraphOp::Logarithm2:
					return "log2";
				case GraphOp::Logarithm10:
					return "log10";
				case GraphOp::Sine:
					return "sin";
				case GraphOp::Cosine:
					return "cos";
				case GraphOp::Tangent:
					return "tan";
				case GraphOp::Arcsine:
					return "asin";
				case GraphOp::Arccosine:
					return "acos";
				case GraphOp::Arctangent:
					return "atan";
				case GraphOp::HyperbolicSine:
					return "sinh";
				case GraphOp::HyperbolicCosine:
					return "cosh";
				case GraphOp::HyperbolicTangent:
					return "tanh";
				case GraphOp::Add:
					return "add";
				case GraphOp::Subtract:
					return "sub";
				case GraphOp::Multiply:
					return "mul";
				case GraphOp::Divide:
					return "div";
				case GraphOp::Power:
					return "pow";
				case GraphOp::Minimum:
					return "min";
				case GraphOp::Maximum:
					return "max";
				case GraphOp::Modulo:
					return "fmod";
				case GraphOp::Arctangent2:
					return "atan2";
				case GraphOp::Step:
					return "step";
				case GraphOp::Lerp:
					return "lerp";
				case GraphOp::Clamp:
					return "clamp";
				case GraphOp::Smoothstep:
					return "smoothstep";
				case GraphOp::Dot:
					return "dot";
				case GraphOp::Cross:
					return "cross";
				case GraphOp::Length:
					return "length";
				case GraphOp::Distance:
					return "distance";
				case GraphOp::Normalize:
					return "normalize";
				case GraphOp::Reflect:
					return "reflect";
				case GraphOp::Swizzle:
					return "swizzle";
				case GraphOp::Construct:
					return "construct";
				case GraphOp::Less:
					return "less";
				case GraphOp::LessEqual:
					return "lessequal";
				case GraphOp::Equal:
					return "equal";
				case GraphOp::Select:
					return "select";
				case GraphOp::LogicalAnd:
					return "and";
				case GraphOp::LogicalOr:
					return "or";
				case GraphOp::LogicalNot:
					return "not";
				case GraphOp::All:
					return "all";
				case GraphOp::Any:
					return "any";
				case GraphOp::IsNaN:
					return "isnan";
				case GraphOp::IsInfinite:
					return "isinf";
				case GraphOp::Random:
					return "random";
				case GraphOp::ValueNoise:
					return "valuenoise";
				case GraphOp::GradientNoise:
					return "gradientnoise";
				case GraphOp::Voronoi:
					return "voronoi";
				case GraphOp::FractalNoise:
					return "fractalnoise";
				case GraphOp::CurlNoise:
					return "curlnoise";
				default:
					return {};
				}
			}

			bool CarriesPayload(GraphOp op) {
				return op == GraphOp::Swizzle || op == GraphOp::Random || op == GraphOp::ValueNoise || op == GraphOp::GradientNoise || op == GraphOp::Voronoi ||
					   op == GraphOp::FractalNoise || op == GraphOp::CurlNoise;
			}

		} // anonymous namespace

		bool Emit(const GraphProgram& program, str& out, Vector<GraphDiagnostic>& diagnostics, std::string_view system) {
			const str gpuOnly = system.empty() ? str("only a particle context does (a Spawn's values and a system's bounds are the CPU's)")
											   : "only a system simulated on the GPU does (" + str(system) + ")";
			const str cpuSays = system.empty() ? str() : " (" + str(system) + ")";
			str text;
			Vector<i64> lines{GetMemoryResource(MemTag::Editor)};
			lines.resize(program.values.size(), -1);
			HashMap<str, i64> inputs{GetMemoryResource(MemTag::Editor)};
			i64 next = 0;
			bool ok = true;
			auto refuse = [&](const GraphValue& value, str message) {
				ok = false;
				for (const GraphDiagnostic& said : diagnostics) {
					if (said.node == value.node && said.message == message) {
						return;
					}
				}
				diagnostics.push_back({value.node, std::move(message)});
			};
			HashMap<GraphValueId, str> meshes{GetMemoryResource(MemTag::Editor)};
			HashMap<str, u32> declared{GetMemoryResource(MemTag::Editor)};
			for (GraphValueId id = 0; id < program.values.size(); ++id) {
				const GraphValue& value = program.values[id];
				if (value.op == GraphOp::BoolToFloat) {
					lines[id] = lines[value.operands[0]];
					continue;
				}
				if (value.op == GraphOp::External && value.type == GraphValueType::Mesh) {
					meshes[id] = program.externals[value.payload].name;
					continue;
				}
				if (value.op == GraphOp::MeshCount) {
					const auto mesh = meshes.find(value.operands[0]);
					if (mesh == meshes.end()) {
						refuse(value, "counts a mesh no port holds");
						continue;
					}
					const str name = mesh->second + str(value.payload == 0 ? GRAPH_EFFECT_MESH_VERTEX_COUNT_INPUT : GRAPH_EFFECT_MESH_INDEX_COUNT_INPUT);
					if (auto found = inputs.find(name); found != inputs.end()) {
						lines[id] = found->second;
						continue;
					}
					text += std::format("input 1 {}\n", name);
					inputs[name] = next;
					lines[id] = next++;
					continue;
				}
				if (value.op == GraphOp::MeshIndex || value.op == GraphOp::MeshVertex) {
					const auto mesh = meshes.find(value.operands[0]);
					if (mesh == meshes.end()) {
						refuse(value, "samples a mesh no port holds");
						continue;
					}
					if (lines[value.operands[1]] < 0) {
						continue;
					}
					auto number = declared.find(mesh->second);
					if (number == declared.end()) {
						text += std::format("mesh {}\n", mesh->second);
						number = declared.emplace(mesh->second, static_cast<u32>(declared.size())).first;
					}
					const bool vertex = value.op == GraphOp::MeshVertex;
					const u32 payload = vertex ? (number->second << GRAPH_EFFECT_MESH_CHANNEL_BITS) | value.payload : number->second;
					text += std::format("{} {} {} {}\n", vertex ? "meshvertex" : "meshindex", GraphTypes::Components(value.type), payload,
										lines[value.operands[1]]);
					lines[id] = next++;
					continue;
				}
				if (value.op == GraphOp::Call || value.op == GraphOp::Argument || value.op == GraphOp::CallResult) {
					refuse(value, "runs custom HLSL, which " + gpuOnly);
					continue;
				}
				const u32 width = GraphTypes::Components(value.type);
				const bool held = value.type == GraphValueType::Bool || (GraphTypes::VectorWidth(value.type) != 0);
				if (!held || width == 0 || width > 4) {
					refuse(value, "computes a " + str(GraphTypes::DisplayName(value.type)) + " (an effect's CPU program can't hold it)" + cpuSays);
					continue;
				}
				bool missing = false;
				for (u32 operand = 0; operand < value.operandCount; ++operand) {
					missing = missing || lines[value.operands[operand]] < 0;
				}
				if (value.op == GraphOp::External) {
					const str& name = program.externals[value.payload].name;
					if (auto found = inputs.find(name); found != inputs.end()) {
						lines[id] = found->second;
						continue;
					}
					text += std::format("input {} {}\n", width, name);
					inputs[name] = next;
					lines[id] = next++;
					continue;
				}
				if (value.op == GraphOp::Constant) {
					text += std::format("const {}", width);
					for (f32 component : program.ConstantOf(id)) {
						u32 bits = 0;
						std::memcpy(&bits, &component, sizeof(bits));
						text += std::format(" 0x{:08X}", bits);
					}
					text += '\n';
					lines[id] = next++;
					continue;
				}
				const std::string_view mnemonic = Mnemonic(value.op);
				if (mnemonic.empty()) {
					refuse(value, "uses " + str(GraphOps::Name(value.op)) + " (an effect's CPU program can't run it)" + cpuSays);
					continue;
				}
				if (missing) {
					continue;
				}
				text += std::format("{} {} {}", mnemonic, width, CarriesPayload(value.op) ? value.payload : 0u);
				for (u32 operand = 0; operand < value.operandCount; ++operand) {
					text += std::format(" {}", lines[value.operands[operand]]);
				}
				text += '\n';
				lines[id] = next++;
			}
			for (const GraphRoot& root : program.roots) {
				if (root.value == GRAPH_NO_VALUE || lines[root.value] < 0) {
					ok = false;
					continue;
				}
				text += std::format("root {}\n", lines[root.value]);
			}
			if (ok) {
				out = std::move(text);
			}
			return ok;
		}

	} // namespace GraphEffectAssembly

	bool IsEffectDocumentType(std::string_view type) {
		return type == GRAPH_EFFECT_DOCUMENT_TYPE || type == GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE || type == GRAPH_EFFECT_BLOCK_SUBGRAPH_DOCUMENT_TYPE;
	}

	namespace GraphEffectTarget {

		namespace {

			void NameSystems(GraphEffect& effect) {
				HashSet<str> taken{GetMemoryResource(MemTag::Editor)};
				for (GraphEffectSystem& system : effect.systems) {
					std::string_view wished = system.name;
					if (wished.ends_with(')')) {
						const size_t open = wished.rfind(" (");
						const std::string_view digits = open == std::string_view::npos ? std::string_view() : wished.substr(open + 2, wished.size() - open - 3);
						if (open != std::string_view::npos && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
							wished = wished.substr(0, open);
						}
					}
					const str base(wished.empty() ? GRAPH_EFFECT_DEFAULT_SYSTEM_NAME : wished);
					str name = base;
					for (u32 index = 1; taken.contains(name); ++index) {
						name = std::format("{} ({})", base, index);
					}
					taken.insert(name);
					system.name = std::move(name);
				}
			}

		} // anonymous namespace

		GraphEffectTiming Timing(const GraphDocument& document) {
			GraphEffectTiming timing;
			for (const GraphSetting& setting : document.settings) {
				LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
				const JSONValue value = JSONParse(setting.value, arena);
				if (value.IsBool()) {
					timing.fixedDeltaTime = setting.key == GRAPH_EFFECT_FIXED_DELTA_TIME ? value.AsBool() : timing.fixedDeltaTime;
					timing.exactFixedTime = setting.key == GRAPH_EFFECT_EXACT_FIXED_TIME ? value.AsBool() : timing.exactFixedTime;
					timing.ignoreTimeScale = setting.key == GRAPH_EFFECT_IGNORE_TIME_SCALE ? value.AsBool() : timing.ignoreTimeScale;
				} else if (value.IsNumber() && setting.key == GRAPH_EFFECT_PREWARM_STEP_COUNT) {
					const f64 steps = value.AsNumber();
					timing.prewarmStepCount = steps >= 1.0 ? static_cast<u32>(std::min(std::floor(steps), static_cast<f64>(GRAPH_EFFECT_PREWARM_MAX_STEPS))) : 0u;
				} else if (value.IsNumber() && setting.key == GRAPH_EFFECT_PREWARM_DELTA_TIME) {
					timing.prewarmDeltaTime = static_cast<f32>(value.AsNumber());
				} else if (value.IsNumber() && setting.key == GRAPH_EFFECT_CULLING_FLAGS) {
					const f64 flags = value.AsNumber();
					const bool known = flags >= 0.0 && flags <= static_cast<f64>(GRAPH_EFFECT_CULL_DEFAULT) && flags == std::floor(flags);
					timing.cullingFlags = known ? static_cast<u32>(flags) : GRAPH_EFFECT_CULL_DEFAULT;
				}
			}
			const bool prewarms = timing.prewarmStepCount > 0 && std::isfinite(timing.prewarmDeltaTime) && timing.prewarmDeltaTime > 0.0f;
			timing.prewarmDeltaTime = prewarms ? std::max(timing.prewarmDeltaTime, GRAPH_EFFECT_PREWARM_MIN_DELTA_TIME) : 0.0f;
			timing.prewarmStepCount = prewarms ? timing.prewarmStepCount : 0u;
			if (HasAutomaticBounds(document)) {
				timing.cullingFlags = 0;
			}
			return timing;
		}

		bool HasAutomaticBounds(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == GRAPH_EFFECT_INITIALIZE && GraphEffectRules::BoundsModeOf(context.node) == GRAPH_EFFECT_BOUNDS_AUTOMATIC) {
					return true;
				}
			}
			return false;
		}

		GraphEffectInstancing Instancing(const GraphDocument& document) {
			GraphEffectInstancing instancing;
			for (const GraphSetting& setting : document.settings) {
				if (setting.key != GRAPH_EFFECT_INSTANCING_MODE && setting.key != GRAPH_EFFECT_INSTANCING_CAPACITY) {
					continue;
				}
				LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
				const JSONValue value = JSONParse(setting.value, arena);
				if (!value.IsNumber()) {
					continue;
				}
				const f64 number = value.AsNumber();
				if (setting.key == GRAPH_EFFECT_INSTANCING_MODE) {
					const bool known = number == GRAPH_EFFECT_INSTANCING_DISABLED || number == GRAPH_EFFECT_INSTANCING_AUTOMATIC || number == GRAPH_EFFECT_INSTANCING_CUSTOM;
					instancing.mode = known ? static_cast<i32>(number) : GRAPH_EFFECT_INSTANCING_AUTOMATIC;
				} else {
					instancing.capacity = number >= 1.0 ? static_cast<u32>(std::min(std::floor(number), static_cast<f64>(GRAPH_EFFECT_INSTANCING_MAX_CAPACITY))) : 1u;
				}
			}
			for (const GraphContext& context : document.contexts) {
				instancing.refused |= context.node.def == GRAPH_EFFECT_GPU_EVENT ? GRAPH_EFFECT_INSTANCING_GPU_EVENT : 0u;
			}
			return instancing;
		}

		bool Compile(const GraphDocument& document, std::string_view sourceName, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics,
					 GraphDefinitions* definitions, const GraphEffectCompileOptions& options) {
			GraphDefinitions own;
			EffectCompile compile(document, sourceName, definitions ? *definitions : own, diagnostics, options);
			GraphEffect built;
			if (!compile.Run(built)) {
				return false;
			}
			NameSystems(built);
			built.timing = Timing(document);
			for (const GraphEffectSystem& system : built.systems) {
				built.timing.cullingFlags = system.boundsMode == GRAPH_EFFECT_BOUNDS_AUTOMATIC ? 0u : built.timing.cullingFlags;
			}
			built.instancing = Instancing(document);
			for (const GraphEffectSystem& system : built.systems) {
				const bool events = system.parent != GRAPH_EFFECT_NO_BINDING || !system.initializeEvents.empty() || !system.updateEvents.empty();
				built.instancing.refused |= events ? GRAPH_EFFECT_INSTANCING_GPU_EVENT : 0u;
			}
			out = std::move(built);
			return true;
		}

		bool CheckSubgraph(const GraphDocument& document, const Guid& asset, Vector<GraphDiagnostic>& diagnostics, GraphDefinitions* definitions) {
			GraphDefinitions own;
			GraphDefinitions& resolver = definitions ? *definitions : own;
			const size_t before = diagnostics.size();
			GraphSubgraphKind kind = GraphSubgraphKind::Shader;
			if (!GraphSubgraphs::KindOf(document.type, kind) || kind == GraphSubgraphKind::Shader || kind == GraphSubgraphKind::EffectSystem) {
				diagnostics.push_back({0, "is a '" + document.type + "' document (not an operator or block subgraph)"});
				return false;
			}
			Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
			GraphEffectKernel kernel = GraphEffectKernel::Update;
			if (kind == GraphSubgraphKind::EffectOperator) {
				for (const GraphProperty& property : document.properties) {
					for (const GraphNode& node : GraphSubgraphs::IsOutput(property) ? std::span<const GraphNode>(document.nodes) : std::span<const GraphNode>()) {
						const GraphSetting* setting = nullptr;
						for (const GraphSetting& held : node.settings) {
							setting = held.key == GRAPH_PROPERTY_SETTING ? &held : setting;
						}
						if (node.def == GRAPH_PROPERTY_NODE && setting && std::strtoul(setting->value.c_str(), nullptr, 10) == property.id) {
							roots.push_back({node.id, str(GRAPH_SUBGRAPH_OUTPUT_PORT), GraphStageVertex, true});
						}
					}
				}
			} else {
				const GraphContext* stack = nullptr;
				for (const GraphContext& context : document.contexts) {
					stack = context.node.def == GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT ? &context : stack;
				}
				if (!stack) {
					diagnostics.push_back({0, "has no Block Subgraph context (it holds the blocks)"});
					return false;
				}
				Vector<std::string_view> kinds{GetMemoryResource(MemTag::Editor)};
				GraphEffectRules::StandsFor(stack->node, kinds);
				if (kinds.empty()) {
					diagnostics.push_back({stack->node.id, "has Suitable Contexts that aren't one of its options"});
					return false;
				}
				kernel = KindOf(kinds[0]) == ContextKind::Initialize ? GraphEffectKernel::Initialize
						 : KindOf(kinds[0]) == ContextKind::Output	 ? GraphEffectKernel::Output
																	 : GraphEffectKernel::Update;
				for (const GraphNode& block : stack->blocks) {
					if (block.disabled) {
						continue;
					}
					str error;
					const GraphNodeDefinition* definition = resolver.Find(document, block, &error);
					if (!definition) {
						diagnostics.push_back({block.id, error});
						continue;
					}
					if (block.def == GRAPH_EFFECT_TRIGGER_EVENT) {
						diagnostics.push_back({block.id, "is a Trigger Event. No GPU Event of a graph using this subgraph can take its Evt"});
						continue;
					}
					const auto refused = std::find_if(kinds.begin(), kinds.end(), [&block](std::string_view held) { return !GraphEffectRules::HoldsBlock(held, block); });
					if (refused != kinds.end()) {
						const GraphNodeDefinition* context = GraphNodeLibrary::Find(*refused);
						diagnostics.push_back({block.id, "is a " + str(definition->name) + " block, its Suitable Contexts list " +
															 str(context ? context->name : *refused) + " (can't hold one)"});
						continue;
					}
					const bool spawn = definition->outputs.empty();
					for (const GraphPortDefinition& port : spawn ? definition->inputs : definition->outputs) {
						roots.push_back({block.id, str(port.name), GraphStageVertex, spawn});
					}
				}
			}
			Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
			KernelBindings(kernel, bindings);
			PortTextureBindings(document, resolver, bindings);
			PortMeshBindings(document, resolver, bindings, FRAME_HEADER_BYTES);
			CameraDepthBindings(FRAME_HEADER_BYTES, kernel == GraphEffectKernel::Output ? "only an Initialize or an Update reads the camera's depth" : "",
								bindings);
			{
				Vector<GraphDiagnostic> said{GetMemoryResource(MemTag::Editor)};
				EffectCompile warnings(document, "", resolver, said, {});
				warnings.WarnOperators();
				for (const GraphContext& context : document.contexts) {
					if (kind == GraphSubgraphKind::EffectBlock && context.node.def == GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
						warnings.WarnStack(context, bindings);
					}
				}
				for (GraphDiagnostic& diagnostic : said) {
					if (diagnostic.severity == GraphDiagnosticSeverity::Warning) {
						diagnostics.push_back(std::move(diagnostic));
					}
				}
			}
			if (roots.empty()) {
				return !HasErrorSince(diagnostics, before);
			}
			GraphCompileRequest request;
			request.roots = roots;
			request.bindings = bindings;
			request.definitions = &resolver;
			request.asset = asset;
			request.space = GraphSpace::Local;
			GraphProgram program;
			GraphCompiler::Compile(document, request, program, diagnostics);
			return !HasErrorSince(diagnostics, before);
		}

		std::span<const GraphBinding> Bindings() { return EFFECT_BINDINGS; }

		void PortTextureBindings(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphBinding>& out, std::string_view unavailable) {
			ForEachPortTexture(document, definitions, [&](std::span<const u32> within, const GraphNode& node, u32 index, const GraphPortDefinition& port, GraphValueType type) {
				GraphBinding binding{GraphCompiler::PortTextureBinding(node.id, port.name, within), PortTextureName(within, node.id, index), type};
				binding.sampler = binding.expression + "Sampler";
				binding.unavailable = str(unavailable);
				out.push_back(std::move(binding));
			});
		}

		Guid HeldTexture(const GraphNode& node, std::string_view port) {
			for (const GraphInput& input : node.inputs) {
				if (input.port != port || input.value.empty()) {
					continue;
				}
				LinearAllocator arena(input.value.size() + 64, MemTag::Editor);
				const JSONValue value = JSONParse(input.value, arena);
				return value.IsString() ? Guid::FromString(value.AsString()) : Guid{};
			}
			return {};
		}

		void PortMeshBindings(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphBinding>& out, u32 first) {
			u32 offset = first;
			ForEachPortMesh(document, definitions, [&](std::span<const u32> within, const GraphNode& node, const GraphPortDefinition& port) {
				out.push_back({GraphCompiler::PortMeshBinding(node.id, port.name, within), std::format("{}u", offset), T::Mesh});
				offset += GRAPH_EFFECT_MESH_RECORD_BYTES;
			});
		}

		Guid HeldMesh(const GraphNode& node, std::string_view port) {
			for (const GraphInput& input : node.inputs) {
				if (input.port == port && !input.value.empty()) {
					return HeldTexture(node, port);
				}
			}
			return AssetDatabase::LookupGuid(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
		}

		void KernelBindings(GraphEffectKernel kernel, Vector<GraphBinding>& out, bool world, bool strip) {
			out.assign(std::begin(EFFECT_BINDINGS), std::end(EFFECT_BINDINGS));
			for (u32 row = 0; row < 3; ++row) {
				out.push_back({str(GRAPH_FOREIGN_SPACE_ROWS[row]),
							   std::format("{}({}u)", world ? "SltParticleEffectLocalToWorldRow" : "SltParticleEffectWorldToLocalRow", row), T::Float4});
			}
			const std::string_view camera[] = {GRAPH_EFFECT_CAMERA_RIGHT, GRAPH_EFFECT_CAMERA_UP, GRAPH_EFFECT_CAMERA_FORWARD, GRAPH_EFFECT_CAMERA_POSITION};
			for (u32 column = 0; column < 4; ++column) {
				const str read = std::format("sltPassView.inverseView[{}].xyz", column);
				GraphBinding binding{str(camera[column]), world ? read : std::format("SltParticleEffectWorldToLocal{}({})", column == 3 ? "Point" : "Vector", read),
									 T::Float3};
				out.push_back(std::move(binding));
			}
			out.push_back({str(GRAPH_EFFECT_CAMERA_ORTHOGRAPHIC), "(sltPassView.projectionParams.z > 0.5)", T::Bool});
			out.push_back({str(GRAPH_EFFECT_CAMERA_CLIP_W),
						   world ? str("SltParticleCameraClipWRow()") : str("mul(SltParticleCameraClipWRow(), SltParticleEffectLocalToWorld())"),
						   T::Float4});
			out.push_back({str(GRAPH_EFFECT_SHADOW_PASS), "SltParticleShadowPass()", T::Bool});
			if (kernel != GraphEffectKernel::Output) {
				for (size_t i = out.size() - 7; i < out.size(); ++i) {
					out[i].unavailable = "only an Output context draws for a camera";
				}
			}
			for (const std::string_view name : {GRAPH_EFFECT_STRIP_PREVIOUS, GRAPH_EFFECT_STRIP_NEXT}) {
				GraphBinding neighbor{str(name), name == GRAPH_EFFECT_STRIP_PREVIOUS ? "sltStripPrevious" : "sltStripNext", T::Float4};
				if (kernel != GraphEffectKernel::Output || !strip) {
					neighbor.unavailable = "only a strip output has a strip's neighbors";
				}
				out.push_back(std::move(neighbor));
			}
			GraphBinding slot{str(GRAPH_EFFECT_MAIN_TEXTURE_BINDING), str(GRAPH_EFFECT_MAIN_TEXTURE), T::Texture2D};
			slot.sampler = str(GRAPH_EFFECT_MAIN_TEXTURE) + "Sampler";
			if (kernel != GraphEffectKernel::Output) {
				slot.unavailable = "only an Output context samples its Main Texture";
			}
			out.push_back(std::move(slot));
			for (u32 map = 0; map < std::size(GRAPH_EFFECT_LIT_MAP_BINDINGS); ++map) {
				GraphBinding mapSlot{str(GRAPH_EFFECT_LIT_MAP_BINDINGS[map]), str(GRAPH_EFFECT_LIT_MAP_SLOTS[map]), T::Texture2D};
				mapSlot.sampler = str(GRAPH_EFFECT_LIT_MAP_SLOTS[map]) + "Sampler";
				mapSlot.unavailable = "a lit output's map belongs to its shading (no block reads it)";
				out.push_back(std::move(mapSlot));
			}
			constexpr T GRAPH_TEXTURE_TYPES[] = {T::Texture2D, T::Texture2DArray, T::Texture3D, T::TextureCube};
			for (u32 kind = 0; kind < std::size(GRAPH_EFFECT_NODE_SHADER_TEXTURE_BINDINGS); ++kind) {
				GraphBinding graphSlot{str(GRAPH_EFFECT_NODE_SHADER_TEXTURE_BINDINGS[kind]), "sltGraphTexture", GRAPH_TEXTURE_TYPES[kind]};
				graphSlot.sampler = "sltGraphTextureSampler";
				graphSlot.unavailable = "a node shader's texture belongs to its shading (no block reads it)";
				out.push_back(std::move(graphSlot));
			}
			out.push_back({str(GRAPH_EFFECT_FLIPBOOK_SIZE), "float2(1.0, 1.0)", T::Float2});
			out.push_back({str(GRAPH_EFFECT_FLIPBOOK_BLENDING), "false", T::Bool});
			GraphBinding reap{str(GRAPH_EFFECT_IMPLICIT_REAP), "sltImplicitReap", T::Bool};
			if (kernel == GraphEffectKernel::Output || kernel == GraphEffectKernel::Sort) {
				reap.unavailable = "only an Initialize or an Update triggers events";
			}
			out.push_back(std::move(reap));
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				out.push_back({GraphEffectAttributes::CurrentBinding(attribute.name), AttributeLocal(attribute.name), attribute.type});
				GraphBinding read{GraphEffectAttributes::SourceBinding(attribute.name), SourceLocal(attribute.name), attribute.type};
				if (kernel != GraphEffectKernel::Initialize) {
					read.unavailable = "only an Initialize context reads the source of what spawned it";
				}
				out.push_back(std::move(read));
			}
		}

	} // namespace GraphEffectTarget

	namespace GraphEffectRules {

		bool MayFeed(std::string_view from, std::string_view to, std::string_view slot) {
			const ContextKind target = KindOf(to);
			switch (KindOf(from)) {
			case ContextKind::Event:
				return (target == ContextKind::Spawn && (slot == GRAPH_EFFECT_START || slot == GRAPH_EFFECT_STOP)) ||
					   ((target == ContextKind::Initialize || target == ContextKind::OutputEvent) && slot == GRAPH_EFFECT_IN) ||
					   target == ContextKind::Subgraph;
			case ContextKind::Spawn:
				return ((target == ContextKind::Initialize || target == ContextKind::OutputEvent) && slot == GRAPH_EFFECT_IN) ||
					   target == ContextKind::Subgraph;
			case ContextKind::GPUEvent:
				return target == ContextKind::Initialize && slot == GRAPH_EFFECT_IN;
			case ContextKind::Initialize:
				return (target == ContextKind::Update || target == ContextKind::Output) && slot == GRAPH_EFFECT_IN;
			case ContextKind::Update:
				return target == ContextKind::Output && slot == GRAPH_EFFECT_IN;
			default:
				return false;
			}
		}

		bool TakesOneSource(std::string_view context) {
			const ContextKind kind = KindOf(context);
			return kind == ContextKind::Update || kind == ContextKind::Output;
		}

		bool Holds(std::string_view context, std::string_view block) {
			if (block == GRAPH_EFFECT_SUBGRAPH_BLOCK) {
				const ContextKind kind = KindOf(context);
				return kind == ContextKind::Spawn || kind == ContextKind::Initialize || kind == ContextKind::Update || kind == ContextKind::Output;
			}
			if (block == GRAPH_EFFECT_POSITION_DEPTH) {
				return KindOf(context) == ContextKind::Initialize;
			}
			switch (KindOf(context)) {
			case ContextKind::Spawn:
				return block == GRAPH_EFFECT_CONSTANT_RATE || block == GRAPH_EFFECT_BURST || block == GRAPH_EFFECT_SET_SPAWN_EVENT ||
					   block == GRAPH_EFFECT_INCREMENT_STRIP_INDEX;
			case ContextKind::Update:
				if (block == GRAPH_EFFECT_FORCE || block == GRAPH_EFFECT_GRAVITY || block == GRAPH_EFFECT_LINEAR_DRAG || block == GRAPH_EFFECT_TURBULENCE ||
					block == GRAPH_EFFECT_CONFORM_TO_SPHERE || block == GRAPH_EFFECT_FLIPBOOK_PLAYER || block == GRAPH_EFFECT_VECTOR_FIELD_FORCE ||
					block == GRAPH_EFFECT_CONFORM_TO_SDF) {
					return true;
				}
				[[fallthrough]];
			case ContextKind::Initialize:
				return block == GRAPH_EFFECT_SET_ATTRIBUTE || block == GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE || block == GRAPH_EFFECT_POSITION_SHAPE ||
					   block == GRAPH_EFFECT_POSITION_MESH || block == GRAPH_EFFECT_POSITION_SEQUENTIAL || block == GRAPH_EFFECT_COLLISION_SHAPE ||
					   block == GRAPH_EFFECT_COLLISION_DEPTH || block == GRAPH_EFFECT_TRIGGER_EVENT || block == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK;
			case ContextKind::Output:
				return block == GRAPH_EFFECT_SET_ATTRIBUTE || block == GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE || block == GRAPH_EFFECT_POSITION_SHAPE ||
					   block == GRAPH_EFFECT_POSITION_MESH || block == GRAPH_EFFECT_POSITION_SEQUENTIAL || block == GRAPH_EFFECT_COLLISION_SHAPE ||
					   block == GRAPH_EFFECT_ORIENT || block == GRAPH_EFFECT_CAMERA_FADE || block == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK;
			default:
				return false;
			}
		}

		bool HoldsBlock(std::string_view context, const GraphNode& block) {
			if (!Holds(context, block.def)) {
				return false;
			}
			if (block.def == GRAPH_EFFECT_COLLISION_SHAPE) {
				const u32 behavior = Settings(block).Choice("Behavior", GraphEffectBlocks::BEHAVIORS, GraphEffectBlocks::BehaviorCollision);
				const ContextKind kind = KindOf(context);
				return behavior != GraphEffectBlocks::BehaviorCollision || kind == ContextKind::Initialize || kind == ContextKind::Update;
			}
			return true;
		}

		bool Suits(u32 suitable, std::string_view context) {
			constexpr u8 SPAWN = 1;
			constexpr u8 INITIALIZE = 2;
			constexpr u8 UPDATE = 4;
			constexpr u8 OUTPUT = 8;
			constexpr u8 MASKS[] = {SPAWN, INITIALIZE, UPDATE, OUTPUT, INITIALIZE | UPDATE, INITIALIZE | UPDATE | OUTPUT, UPDATE | OUTPUT};
			static_assert(std::size(MASKS) == std::size(GRAPH_EFFECT_SUITABLE_CONTEXTS), "a mask per option");
			if (suitable >= std::size(MASKS)) {
				return false;
			}
			switch (KindOf(context)) {
			case ContextKind::Spawn:
				return (MASKS[suitable] & SPAWN) != 0;
			case ContextKind::Initialize:
				return (MASKS[suitable] & INITIALIZE) != 0;
			case ContextKind::Update:
				return (MASKS[suitable] & UPDATE) != 0;
			case ContextKind::Output:
				return (MASKS[suitable] & OUTPUT) != 0;
			default:
				return false;
			}
		}

		u32 SuitableOf(const GraphNode& blockSubgraph) {
			return Settings(blockSubgraph).Choice(GRAPH_EFFECT_SUITABLE_CONTEXTS_SETTING, GRAPH_EFFECT_SUITABLE_CONTEXTS, GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS);
		}

		void StandsFor(const GraphNode& context, Vector<std::string_view>& out) {
			out.clear();
			if (context.def != GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
				return;
			}
			const u32 suitable = SuitableOf(context);
			for (const std::string_view kind : {GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_OUTPUT}) {
				if (Suits(suitable, kind)) {
					out.push_back(kind);
				}
			}
		}

		bool HoldsIn(const GraphNode& context, const GraphNode& block) {
			if (context.def != GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
				return HoldsBlock(context.def, block);
			}
			Vector<std::string_view> kinds{GetMemoryResource(MemTag::Editor)};
			StandsFor(context, kinds);
			return !kinds.empty() && std::all_of(kinds.begin(), kinds.end(), [&block](std::string_view kind) { return HoldsBlock(kind, block); });
		}

		bool IsContext(std::string_view def) { return KindOf(def) != ContextKind::Other; }

		bool IsOutput(std::string_view def) { return KindOf(def) == ContextKind::Output; }

		bool DrawsStrips(std::string_view def) { return IsStripOutput(def); }

		bool MakesStrips(const GraphNode& initialize) {
			return initialize.def == GRAPH_EFFECT_INITIALIZE && Settings(initialize).Choice("Data Type", GRAPH_EFFECT_DATA_TYPES, 0) == 1;
		}

		u32 BoundsModeOf(const GraphNode& initialize) {
			return Settings(initialize).Choice(GRAPH_EFFECT_BOUNDS_SETTING_MODE, GRAPH_EFFECT_BOUNDS_MODES, GRAPH_EFFECT_BOUNDS_RECORDED);
		}

		bool ComputesBounds(const GraphNode& initialize) {
			return std::any_of(initialize.inputs.begin(), initialize.inputs.end(), [](const GraphInput& input) {
				return (input.port == GRAPH_EFFECT_BOUNDS_CENTER || input.port == GRAPH_EFFECT_BOUNDS_SIZE) && input.fromNode != 0;
			});
		}

		bool IsEffectOnly(std::string_view def) {
			const std::span<const GraphNodeDefinition> own = GraphEffectDefinitions();
			return std::any_of(own.begin(), own.end(), [def](const GraphNodeDefinition& definition) { return definition.id == def; });
		}

		bool IgnoresError(const GraphNode& node, std::string_view key) {
			Settings settings(node);
			const JSONValue* ignored = settings.Find(GRAPH_IGNORED_ERRORS_SETTING);
			for (size_t i = 0; ignored && ignored->IsArray() && i < ignored->Size(); ++i) {
				if ((*ignored)[i].IsString() && (*ignored)[i].AsString() == key) {
					return true;
				}
			}
			return false;
		}

		bool HasIgnoredErrors(const GraphNode& node) {
			Settings settings(node);
			const JSONValue* ignored = settings.Find(GRAPH_IGNORED_ERRORS_SETTING);
			return ignored && ignored->IsArray() && ignored->Size() > 0;
		}

	} // namespace GraphEffectRules

} // namespace Shalltear
