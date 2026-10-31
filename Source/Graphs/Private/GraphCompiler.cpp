#include "GraphCompiler.h"

#include "GraphCurves.h"
#include "GraphDefinitions.h"
#include "GraphNodeLibrary.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <array>
#include <deque>
#include <memory_resource>
#include <unordered_map>
#include <unordered_set>

namespace Shalltear {

	namespace {

		enum class NodeState : u8 {
			Unvisited,
			Building,
			Built,
			Failed
		};

		struct NodeEntry {
			const GraphNode* node = nullptr;
			const GraphNodeDefinition* definition = nullptr;
			str missing;
			NodeState state = NodeState::Unvisited;
			Vector<GraphValueId> outputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphValueId> inputs{GetMemoryResource(MemTag::Editor)};
		};

		const GraphInput* FindDocumentInput(const GraphNode& node, std::string_view port) {
			for (const GraphInput& input : node.inputs) {
				if (input.port == port) {
					return &input;
				}
			}
			return nullptr;
		}

		const GraphSetting* FindDocumentSetting(const GraphNode& node, std::string_view key) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key == key) {
					return &setting;
				}
			}
			return nullptr;
		}

		std::string_view PortTypeName(GraphPortType type) {
			switch (type) {
			case GraphPortType::Float:
				return "Float";
			case GraphPortType::Float2:
				return "Vector 2";
			case GraphPortType::Float3:
				return "Vector 3";
			case GraphPortType::Float4:
				return "Vector 4";
			case GraphPortType::Float2x2:
				return "Matrix 2";
			case GraphPortType::Float3x3:
				return "Matrix 3";
			case GraphPortType::Float4x4:
				return "Matrix 4";
			case GraphPortType::DynamicVector:
				return "Dynamic Vector";
			case GraphPortType::DynamicMatrix:
				return "Dynamic Matrix";
			case GraphPortType::Dynamic:
				return "Dynamic";
			case GraphPortType::Any:
				return "Any";
			case GraphPortType::Boolean:
				return "Boolean";
			case GraphPortType::Texture2D:
				return "Texture 2D";
			case GraphPortType::Texture2DArray:
				return "Texture 2D Array";
			case GraphPortType::Texture3D:
				return "Texture 3D";
			case GraphPortType::Cubemap:
				return "Cubemap";
			case GraphPortType::Gradient:
				return "Gradient";
			case GraphPortType::Curve:
				return "AnimationCurve";
			case GraphPortType::Mesh:
				return "Mesh";
			case GraphPortType::GPUEvent:
				return "GPUEvent";
			case GraphPortType::ConnectionState:
				return "Property";
			}
			return "?";
		}

		GraphValueType FixedType(GraphPortType type) {
			switch (type) {
			case GraphPortType::Float:
				return GraphValueType::Float;
			case GraphPortType::Float2:
				return GraphValueType::Float2;
			case GraphPortType::Float3:
				return GraphValueType::Float3;
			case GraphPortType::Float4:
				return GraphValueType::Float4;
			case GraphPortType::Float2x2:
				return GraphValueType::Float2x2;
			case GraphPortType::Float3x3:
				return GraphValueType::Float3x3;
			case GraphPortType::Float4x4:
				return GraphValueType::Float4x4;
			case GraphPortType::Boolean:
			case GraphPortType::ConnectionState:
				return GraphValueType::Bool;
			case GraphPortType::Texture2D:
				return GraphValueType::Texture2D;
			case GraphPortType::Texture2DArray:
				return GraphValueType::Texture2DArray;
			case GraphPortType::Texture3D:
				return GraphValueType::Texture3D;
			case GraphPortType::Cubemap:
				return GraphValueType::TextureCube;
			case GraphPortType::Gradient:
				return GraphValueType::Gradient;
			case GraphPortType::Curve:
				return GraphValueType::Curve;
			case GraphPortType::Mesh:
				return GraphValueType::Mesh;
			case GraphPortType::GPUEvent:
				return GraphValueType::Float;
			default:
				return GraphValueType::None;
			}
		}

		bool FitsPortType(GraphValueType value, GraphPortType port) {
			switch (port) {
			case GraphPortType::DynamicVector:
				return GraphTypes::VectorWidth(value) != 0;
			case GraphPortType::DynamicMatrix:
				return GraphTypes::MatrixDimension(value) != 0;
			case GraphPortType::Dynamic:
				return GraphTypes::VectorWidth(value) != 0 || GraphTypes::MatrixDimension(value) != 0;
			case GraphPortType::Any:
				return value != GraphValueType::None;
			default:
				return value == FixedType(port);
			}
		}

		u32 ResolveVectorWidth(std::span<const u32> connectedWidths) {
			u32 narrowest = 0;
			bool any = false;
			for (u32 width : connectedWidths) {
				any = true;
				if (width > 1 && (narrowest == 0 || width < narrowest)) {
					narrowest = width;
				}
			}
			return any && narrowest != 0 ? narrowest : 1;
		}

		u32 ResolveMatrixDimension(std::span<const u32> connectedDimensions) {
			u32 narrowest = 0;
			for (u32 dimension : connectedDimensions) {
				if (narrowest == 0 || dimension < narrowest) {
					narrowest = dimension;
				}
			}
			return narrowest != 0 ? narrowest : 2;
		}

		constexpr u32 MAX_SUBGRAPH_DEPTH = 32;

		struct ChainLink {
			Guid asset;
			str name;
		};

		class Compilation {
		public:
			Compilation(const GraphDocument& document, const GraphCompileRequest& request, GraphDefinitions& definitions,
						GraphProgram& program, Vector<GraphDiagnostic>& diagnostics)
				: _document(document), _request(request), _definitions(definitions), _program(program), _builder(program),
				  _diagnostics(diagnostics), _nodes(GetMemoryResource(MemTag::Editor)) {}

			bool Run() {
				if (_request.asset.IsValid()) {
					_chain.push_back({_request.asset, GraphSubgraphs::NameOf(_request.asset)});
				}
				IndexDocument();
				for (const GraphCompileRoot& root : _request.roots) {
					GraphValueId value = BuildRoot(root);
					_program.roots.push_back({value, root.stage});
				}
				return !_failed;
			}
		private:
			bool RunInline(const GraphSubgraph& subgraph, Vector<GraphValueId>& out) {
				IndexDocument();
				for (size_t i = 0; i < subgraph.outputBlocks.size(); ++i) {
					if (subgraph.outputBlocks[i] == 0) {
						const GraphPortDefinition& port = subgraph.outputs[i];
						const GraphValueType type = FixedType(port.type);
						out.push_back(_builder.Constant(type, {port.defaultValue, GraphTypes::Components(type)}));
						continue;
					}
					GraphCompileRoot root;
					root.node = subgraph.outputBlocks[i];
					root.port = str(GRAPH_SUBGRAPH_OUTPUT_PORT);
					root.input = true;
					out.push_back(BuildRoot(root));
				}
				return !_failed;
			}

			u32 Attribution(u32 node) const { return _attributeTo != 0 ? _attributeTo : node; }

			str Describe(u32 node) {
				auto found = _nodes.find(node);
				if (found == _nodes.end()) {
					return "node " + std::to_string(node);
				}
				const NodeEntry& entry = found->second;
				const str what = entry.definition ? str(entry.definition->name) : entry.node->def;
				return "node " + std::to_string(node) + " (" + what + ")";
			}
			void Report(u32 node, str message) {
				_failed = true;
				for (const GraphDiagnostic& diagnostic : _diagnostics) {
					if (diagnostic.node == node && diagnostic.message == message) {
						return;
					}
				}
				_diagnostics.push_back({node, std::move(message)});
			}

			void IndexDocument() {
				auto add = [this](const GraphNode& node) {
					NodeEntry& entry = _nodes[node.id];
					entry.node = &node;
					entry.definition = _definitions.Find(_document, node, &entry.missing);
				};
				for (const GraphNode& node : _document.nodes) {
					add(node);
				}
				for (const GraphContext& context : _document.contexts) {
					add(context.node);
					for (const GraphNode& block : context.blocks) {
						add(block);
					}
				}
			}

			GraphValueId BuildRoot(const GraphCompileRoot& root) {
				if (!root.within.empty()) {
					return BuildWithin(root);
				}
				auto found = _nodes.find(root.node);
				if (found == _nodes.end()) {
					Report(0, "a root uses node " + std::to_string(root.node) + " (the graph doesn't have it)");
					return GRAPH_NO_VALUE;
				}
				Build(root.node);
				NodeEntry& entry = found->second;
				if (entry.state != NodeState::Built) {
					return GRAPH_NO_VALUE;
				}
				if (root.input) {
					i32 input = GraphNodeLibrary::FindInput(*entry.definition, root.port);
					if (input < 0) {
						Report(root.node, "has no input named '" + root.port + "'");
						return GRAPH_NO_VALUE;
					}
					return entry.inputs[static_cast<size_t>(input)];
				}
				i32 output = GraphNodeLibrary::FindOutput(*entry.definition, root.port);
				if (output < 0) {
					Report(root.node, "has no output named '" + root.port + "'");
					return GRAPH_NO_VALUE;
				}
				return entry.outputs[static_cast<size_t>(output)];
			}

			void Build(u32 id) {
				struct Frame {
					u32 node;
					u32 nextInput;
					u32 nextSetting = 0;
				};
				Vector<Frame> stack{GetMemoryResource(MemTag::Editor)};
				stack.push_back({id, 0});
				while (!stack.empty()) {
					Frame& frame = stack.back();
					NodeEntry& entry = _nodes[frame.node];
					if (entry.state == NodeState::Built || entry.state == NodeState::Failed) {
						stack.pop_back();
						continue;
					}
					if (entry.state == NodeState::Unvisited) {
						if (entry.definition == nullptr) {
							Report(frame.node, entry.missing);
							entry.state = NodeState::Failed;
							stack.pop_back();
							continue;
						}
						entry.state = NodeState::Building;
					}

					bool descended = false;
					std::span<const GraphPortDefinition> ports = entry.definition->inputs;
					while (frame.nextInput < ports.size() && !descended) {
						const GraphInput* input = FindDocumentInput(*entry.node, ports[frame.nextInput].name);
						const bool active = GraphNodeLibrary::InputActive(*entry.definition, frame.nextInput, *entry.node);
						++frame.nextInput;
						if (input == nullptr || input->fromNode == 0 || !active) {
							continue;
						}
						auto source = _nodes.find(input->fromNode);
						if (source == _nodes.end()) {
							continue;
						}
						if (source->second.state == NodeState::Building) {
							ReportCycle(stack, input->fromNode);
							continue;
						}
						if (source->second.state == NodeState::Unvisited) {
							stack.push_back({input->fromNode, 0});
							descended = true;
						}
					}
					std::span<const GraphSettingDefinition> settings = entry.definition->settings;
					while (!descended && frame.nextSetting < settings.size()) {
						const u32 block = ReroutedBlock(*entry.node, settings[frame.nextSetting]);
						++frame.nextSetting;
						auto target = block != 0 ? _nodes.find(block) : _nodes.end();
						if (target == _nodes.end()) {
							continue;
						}
						if (target->second.state == NodeState::Building) {
							ReportCycle(stack, block);
							continue;
						}
						if (target->second.state == NodeState::Unvisited) {
							stack.push_back({block, 0});
							descended = true;
						}
					}
					if (descended) {
						continue;
					}

					u32 node = stack.back().node;
					stack.pop_back();
					NodeEntry& built = _nodes[node];
					built.state = BuildNode(node, built) ? NodeState::Built : NodeState::Failed;
				}
			}

			str BlockName(const GraphNode& node, const GraphSettingDefinition& setting) const {
				const GraphSetting* stored = FindDocumentSetting(node, setting.key);
				if (stored == nullptr) {
					return str(setting.defaultText);
				}
				LinearAllocator arena(stored->value.size() + 64, MemTag::Editor);
				JSONValue json = JSONParse(stored->value, arena);
				return json.IsString() ? str(json.AsString()) : str();
			}

			u32 FindBlockNamed(std::string_view name, std::span<const std::string_view> kinds) const {
				for (const GraphContext& context : _document.contexts) {
					for (const GraphNode& block : context.blocks) {
						if (std::find(kinds.begin(), kinds.end(), std::string_view(block.def)) == kinds.end()) {
							continue;
						}
						const GraphSetting* stored = FindDocumentSetting(block, "Name");
						if (stored == nullptr) {
							continue;
						}
						LinearAllocator arena(stored->value.size() + 64, MemTag::Editor);
						JSONValue json = JSONParse(stored->value, arena);
						if (json.IsString() && json.AsString() == name) {
							return block.id;
						}
					}
				}
				return 0;
			}

			const GraphBinding* FindBinding(std::string_view name) const {
				for (const GraphBinding& binding : _request.bindings) {
					if (binding.name == name) {
						return &binding;
					}
				}
				return nullptr;
			}

			u32 ReroutedBlock(const GraphNode& node, const GraphSettingDefinition& setting) const {
				if (setting.kind != GraphSettingKind::Block) {
					return 0;
				}
				const str name = BlockName(node, setting);
				return FindBinding("Block/" + name) != nullptr ? 0 : FindBlockNamed(name, setting.options);
			}

			template <typename Frames>
			void ReportCycle(const Frames& stack, u32 closing) {
				size_t start = 0;
				for (size_t i = 0; i < stack.size(); ++i) {
					if (stack[i].node == closing) {
						start = i;
					}
				}
				str chain;
				for (size_t i = start; i < stack.size(); ++i) {
					chain += "node " + std::to_string(stack[i].node) + " -> ";
				}
				chain += "node " + std::to_string(closing);
				for (size_t i = start; i < stack.size(); ++i) {
					Report(stack[i].node, "is part of a cycle: " + chain);
				}
			}

			struct ResolvedInput {
				GraphValueId source = GRAPH_NO_VALUE;
				GraphValueType target = GraphValueType::None;
			};

			bool BuildNode(u32 id, NodeEntry& entry) {
				const GraphNodeDefinition& definition = *entry.definition;
				_builder.SetNode(Attribution(id));
				_builder.SetIdentity(GraphCompiler::Identity(id, _path));
				_builder.ClearError();
				if (definition.subgraphOnly && _document.type != GRAPH_SUBGRAPH_DOCUMENT_TYPE) {
					Report(id, "belongs in a subgraph (it tests an input of the graph using the subgraph)");
					return false;
				}

				std::span<const GraphPortDefinition> ports = definition.inputs;
				Vector<ResolvedInput> resolved{GetMemoryResource(MemTag::Editor)};
				resolved.resize(ports.size());

				Vector<GraphSettingValue> settings{GetMemoryResource(MemTag::Editor)};
				Vector<u32> settingIndices{GetMemoryResource(MemTag::Editor)};
				for (const GraphSettingDefinition& setting : definition.settings) {
					GraphSettingValue value;
					if (!SettingValue(id, *entry.node, setting, value)) {
						return false;
					}
					settings.push_back(value);
					settingIndices.push_back(value.index);
				}

				for (size_t i = 0; i < ports.size(); ++i) {
					const GraphInput* input = FindDocumentInput(*entry.node, ports[i].name);
					if (input == nullptr || input->fromNode == 0 || !GraphNodeLibrary::InputActive(definition, static_cast<u32>(i), settingIndices)) {
						continue;
					}
					if (ports[i].type == GraphPortType::ConnectionState) {
						continue;
					}
					auto source = _nodes.find(input->fromNode);
					if (source == _nodes.end()) {
						Report(id, "input '" + str(GraphPortLabel(ports[i])) + "' reads node " + std::to_string(input->fromNode) +
									   " (the graph doesn't have it)");
						return false;
					}
					if (source->second.state != NodeState::Built) {
						return false;
					}
					i32 output = GraphNodeLibrary::FindOutput(*source->second.definition, input->fromPort);
					if (output < 0) {
						Report(id, "input '" + str(GraphPortLabel(ports[i])) + "' reads '" + input->fromPort + "' from node " +
									   std::to_string(input->fromNode) + " (no such output)");
						return false;
					}
					resolved[i].source = source->second.outputs[static_cast<size_t>(output)];
				}

				if (!ResolveTypes(id, ports, resolved)) {
					return false;
				}

				Vector<GraphValueId> inputs{GetMemoryResource(MemTag::Editor)};
				for (size_t i = 0; i < ports.size(); ++i) {
					GraphValueId value = InputValue(id, *entry.node, ports[i], resolved[i]);
					if (value == GRAPH_NO_VALUE) {
						if (_builder.Failed()) {
							Report(id, "input '" + str(GraphPortLabel(ports[i])) + "': " + _builder.Error());
						}
						return false;
					}
					inputs.push_back(value);
				}

				entry.outputs.assign(definition.outputs.size(), GRAPH_NO_VALUE);
				GraphBodyContext context{_builder, inputs, settings, entry.outputs, {}};
				context.bindings = _request.bindings;
				context.definition = &definition;
				for (size_t i = 0; i < ports.size() && i < 32; ++i) {
					context.connected |= resolved[i].source != GRAPH_NO_VALUE ? 1u << i : 0u;
				}

				if (_request.space != GraphSpace::None) {
					for (size_t i = 0; i < ports.size() && i < 32; ++i) {
						if (ports[i].space == GraphPortSpace::None) {
							continue;
						}
						const GraphInput* stored = FindDocumentInput(*entry.node, ports[i].name);
						const GraphSpace space = stored && stored->space == GRAPH_SPACE_WORLD ? GraphSpace::World : GraphSpace::Local;
						if (space == _request.space) {
							continue;
						}
						context.foreign |= 1u << i;
						inputs[i] = context.FromForeign(inputs[i], ports[i].space);
						if (inputs[i] == GRAPH_NO_VALUE) {
							Report(id, "input '" + str(GraphPortLabel(ports[i])) + "' is in " + (space == GraphSpace::World ? "World" : "Local") +
										   " space and cannot be converted: " + (context.error.empty() ? _builder.Error() : context.error));
							return false;
						}
					}
				}

				entry.inputs.assign(inputs.begin(), inputs.end());
				if (definition.subgraph != nullptr) {
					return InlineSubgraph(id, entry, *definition.subgraph);
				}
				if (definition.body) {
					definition.body(context);
				}
				if (!context.error.empty()) {
					Report(id, context.error);
					return false;
				}
				if (_builder.Failed()) {
					Report(id, "can't be compiled: " + _builder.Error());
					return false;
				}
				for (size_t i = 0; i < definition.outputs.size(); ++i) {
					GraphValueId value = entry.outputs[i];
					const GraphPortDefinition& port = definition.outputs[i];
					if (value == GRAPH_NO_VALUE || !FitsPortType(_builder.TypeOf(value), port.type)) {
						Report(id, "definition '" + str(definition.id) + "' produced no " + str(PortTypeName(port.type)) + " for output '" +
									   str(port.name) + "'");
						return false;
					}
					Label(value, port);
				}
				return true;
			}

			void Label(GraphValueId value, const GraphPortDefinition& port) {
				if (_attributeTo != 0 || !_program.labels[value].empty()) {
					return;
				}
				const std::string_view label = GraphPortLabel(port);
				const bool numeric = !label.empty() && label[0] >= '0' && label[0] <= '9';
				_program.labels[value] = numeric ? "_" + str(label) : str(label);
			}

			bool Enter(u32 id, const NodeEntry& entry, const GraphSubgraph& subgraph, Compilation& inner) {
				for (size_t i = 0; i < _chain.size(); ++i) {
					if (_chain[i].asset != subgraph.asset) {
						continue;
					}
					str chain;
					for (size_t j = i; j < _chain.size(); ++j) {
						chain += "'" + _chain[j].name + "' -> ";
					}
					chain += "'" + subgraph.name + "'";
					Report(id, "uses subgraph '" + subgraph.name + "', which uses itself: " + chain);
					return false;
				}
				if (_chain.size() >= MAX_SUBGRAPH_DEPTH) {
					Report(id, "uses subgraph '" + subgraph.name + "' (nests subgraphs more than " + std::to_string(MAX_SUBGRAPH_DEPTH) +
								   " deep)");
					return false;
				}
				inner._attributeTo = Attribution(id);
				inner._chain = _chain;
				inner._chain.push_back({subgraph.asset, subgraph.name});
				inner._path = _path;
				inner._path.push_back(id);
				for (size_t i = 0; i < entry.inputs.size() && i < subgraph.inputProperties.size(); ++i) {
					inner._bound[subgraph.inputProperties[i]] = entry.inputs[i];
					const GraphInput* input = FindDocumentInput(*entry.node, subgraph.inputs[i].name);
					if (input != nullptr && input->fromNode != 0) {
						inner._wired.insert(subgraph.inputProperties[i]);
					}
				}
				return true;
			}

			void Adopt(u32 id, const GraphSubgraph& subgraph, Compilation& inner, std::span<const GraphDiagnostic> found) {
				for (const GraphDiagnostic& diagnostic : found) {
					const str where = diagnostic.node != 0 ? ", " + inner.Describe(diagnostic.node) + " " : ": ";
					Report(id, "in subgraph '" + subgraph.name + "'" + where + diagnostic.message);
				}
			}

			bool InlineSubgraph(u32 id, NodeEntry& entry, const GraphSubgraph& subgraph) {
				Vector<GraphDiagnostic> found{GetMemoryResource(MemTag::Editor)};
				Compilation inner(subgraph.document, _request, _definitions, _program, found);
				if (!Enter(id, entry, subgraph, inner)) {
					return false;
				}
				Vector<GraphValueId> values{GetMemoryResource(MemTag::Editor)};
				const bool built = inner.RunInline(subgraph, values);
				Adopt(id, subgraph, inner, found);
				if (!built || values.size() != entry.outputs.size()) {
					return false;
				}
				for (size_t i = 0; i < values.size(); ++i) {
					if (values[i] == GRAPH_NO_VALUE) {
						return false;
					}
					entry.outputs[i] = values[i];
					Label(values[i], subgraph.outputs[i]);
				}
				return true;
			}

			GraphValueId BuildWithin(const GraphCompileRoot& root) {
				const u32 use = root.within[0];
				auto found = _nodes.find(use);
				if (found == _nodes.end()) {
					Report(0, "a root is in node " + std::to_string(use) + " (the graph doesn't have it)");
					return GRAPH_NO_VALUE;
				}
				Build(use);
				NodeEntry& entry = found->second;
				if (entry.state != NodeState::Built) {
					return GRAPH_NO_VALUE;
				}
				const GraphSubgraph* subgraph = entry.definition->subgraph;
				if (subgraph == nullptr || (subgraph->kind != GraphSubgraphKind::EffectBlock && subgraph->kind != GraphSubgraphKind::EffectSystem)) {
					Report(use, "is no block or system subgraph a node may sit in");
					return GRAPH_NO_VALUE;
				}
				Vector<GraphDiagnostic> said{GetMemoryResource(MemTag::Editor)};
				Compilation inner(subgraph->document, _request, _definitions, _program, said);
				if (!Enter(use, entry, *subgraph, inner)) {
					return GRAPH_NO_VALUE;
				}
				inner.IndexDocument();
				GraphCompileRoot below = root;
				below.within = root.within.subspan(1);
				const GraphValueId value = inner.BuildRoot(below);
				Adopt(use, *subgraph, inner, said);
				return inner._failed ? GRAPH_NO_VALUE : value;
			}

			bool ResolveTypes(u32 id, std::span<const GraphPortDefinition> ports, std::span<ResolvedInput> resolved) {
				Vector<u32> vectorWidths{GetMemoryResource(MemTag::Editor)};
				Vector<u32> matrixDimensions{GetMemoryResource(MemTag::Editor)};
				Vector<u32> dynamicVectorWidths{GetMemoryResource(MemTag::Editor)};
				u32 dynamicMatrices = 0;
				u32 dynamicVectors = 0;
				u32 mixedDimension = 0;
				for (size_t i = 0; i < ports.size(); ++i) {
					if (resolved[i].source == GRAPH_NO_VALUE) {
						continue;
					}
					GraphValueType type = _builder.TypeOf(resolved[i].source);
					u32 width = type == GraphValueType::Bool ? 1 : GraphTypes::VectorWidth(type);
					u32 dimension = GraphTypes::MatrixDimension(type);
					switch (ports[i].type) {
					case GraphPortType::DynamicVector:
						if (width == 0) {
							return RefuseConnection(id, ports[i], type);
						}
						vectorWidths.push_back(width);
						break;
					case GraphPortType::DynamicMatrix:
						if (dimension == 0) {
							return RefuseConnection(id, ports[i], type);
						}
						matrixDimensions.push_back(dimension);
						break;
					case GraphPortType::Dynamic:
						if (dimension != 0) {
							++dynamicMatrices;
							mixedDimension = dimension;
						} else if (width != 0) {
							++dynamicVectors;
							dynamicVectorWidths.push_back(width);
						} else {
							return RefuseConnection(id, ports[i], type);
						}
						break;
					default:
						break;
					}
				}

				u32 vectorWidth = ResolveVectorWidth(vectorWidths);
				u32 matrixDimension = ResolveMatrixDimension(matrixDimensions);

				enum class DynamicMode : u8 {
					Vector,
					Matrix,
					Mixed
				};
				DynamicMode mode = DynamicMode::Vector;
				if (dynamicMatrices == 2) {
					mode = DynamicMode::Matrix;
				} else if (dynamicVectors == 2) {
					mode = DynamicMode::Vector;
				} else if (dynamicMatrices == 1) {
					mode = DynamicMode::Mixed;
				}
				Vector<u32> dynamicDimensions{GetMemoryResource(MemTag::Editor)};
				for (size_t i = 0; i < ports.size(); ++i) {
					if (ports[i].type == GraphPortType::Dynamic && resolved[i].source != GRAPH_NO_VALUE) {
						u32 dimension = GraphTypes::MatrixDimension(_builder.TypeOf(resolved[i].source));
						if (dimension != 0) {
							dynamicDimensions.push_back(dimension);
						}
					}
				}

				for (size_t i = 0; i < ports.size(); ++i) {
					GraphValueType target = GraphValueType::None;
					switch (ports[i].type) {
					case GraphPortType::DynamicVector:
						target = GraphTypes::Vector(vectorWidth);
						break;
					case GraphPortType::DynamicMatrix:
						target = GraphTypes::Matrix(matrixDimension);
						break;
					case GraphPortType::Dynamic:
						if (mode == DynamicMode::Matrix) {
							target = GraphTypes::Matrix(ResolveMatrixDimension(dynamicDimensions));
						} else if (mode == DynamicMode::Mixed) {
							GraphValueType sourceType = _builder.TypeOf(resolved[i].source);
							target = resolved[i].source != GRAPH_NO_VALUE && GraphTypes::MatrixDimension(sourceType) != 0
										 ? sourceType
										 : GraphTypes::Vector(mixedDimension);
						} else {
							target = GraphTypes::Vector(ResolveVectorWidth(dynamicVectorWidths));
						}
						break;
					case GraphPortType::Any:
						target = resolved[i].source != GRAPH_NO_VALUE ? _builder.TypeOf(resolved[i].source) : GraphValueType::Float;
						break;
					default:
						target = FixedType(ports[i].type);
						break;
					}
					resolved[i].target = target;
					if (resolved[i].source != GRAPH_NO_VALUE && !GraphProgramBuilder::CanConvert(_builder.TypeOf(resolved[i].source), target)) {
						return RefuseConnection(id, ports[i], _builder.TypeOf(resolved[i].source));
					}
				}
				return true;
			}

			bool RefuseConnection(u32 id, const GraphPortDefinition& port, GraphValueType type) {
				Report(id, "input '" + str(GraphPortLabel(port)) + "' is a " + str(PortTypeName(port.type)) + " and cannot take a " +
							   str(GraphTypes::DisplayName(type)));
				return false;
			}

			GraphValueId InputValue(u32 id, const GraphNode& node, const GraphPortDefinition& port, const ResolvedInput& resolved) {
				if (port.type == GraphPortType::ConnectionState) {
					return ConnectionState(id, node, port);
				}
				if (resolved.source != GRAPH_NO_VALUE) {
					return _builder.Convert(resolved.source, resolved.target);
				}

				if (!port.binding.empty()) {
					for (const GraphBinding& binding : _request.bindings) {
						if (binding.name == port.binding) {
							GraphValueId value = binding.Read(_builder);
							return _builder.Convert(value, resolved.target);
						}
					}
					Report(id, "input '" + str(GraphPortLabel(port)) + "' reads " + str(port.binding) + " when unconnected, and this target does not provide it");
					return GRAPH_NO_VALUE;
				}

				if (resolved.target == GraphValueType::Gradient) {
					std::array<f32, GRAPH_GRADIENT_COMPONENTS> gradient{};
					GraphGradients::Default(gradient);
					const GraphInput* input = FindDocumentInput(node, port.name);
					const std::string_view stored = input != nullptr && !input->value.empty() ? std::string_view(input->value) : port.defaultJson;
					str error;
					if (!stored.empty() && !GraphGradients::Read(stored, gradient, error)) {
						Report(id, "input '" + str(GraphPortLabel(port)) + "' " + error);
						return GRAPH_NO_VALUE;
					}
					return _builder.Constant(GraphValueType::Gradient, gradient);
				}
				if (resolved.target == GraphValueType::Curve) {
					const GraphInput* input = FindDocumentInput(node, port.name);
					std::string_view stored = input != nullptr && !input->value.empty() ? std::string_view(input->value) : port.defaultJson;
					stored = stored.empty() ? GraphCurves::DEFAULT_JSON : stored;
					AnimationCurve curve(GetMemoryResource(MemTag::Editor));
					std::array<f32, GRAPH_CURVE_COMPONENTS> packed{};
					str error;
					if (!GraphCurves::Parse(stored, curve, error) || !GraphCurves::Pack(curve, packed, error)) {
						Report(id, "input '" + str(GraphPortLabel(port)) + "' " + error);
						return GRAPH_NO_VALUE;
					}
					return _builder.Constant(GraphValueType::Curve, packed);
				}

				if (GraphTypes::IsTexture(resolved.target)) {
					const str held = GraphCompiler::PortTextureBinding(id, port.name, _path);
					for (const GraphBinding& binding : _request.bindings) {
						if (binding.name == held && binding.type == resolved.target) {
							return binding.Read(_builder);
						}
					}
					Report(id, "input '" + str(GraphPortLabel(port)) + "' needs a texture: connect a " + str(GraphTypes::DisplayName(resolved.target)) + " property");
					return GRAPH_NO_VALUE;
				}
				if (resolved.target == GraphValueType::Mesh) {
					const str held = GraphCompiler::PortMeshBinding(id, port.name, _path);
					for (const GraphBinding& binding : _request.bindings) {
						if (binding.name == held && binding.type == GraphValueType::Mesh) {
							return binding.Read(_builder);
						}
					}
					Report(id, "input '" + str(GraphPortLabel(port)) + "' needs a mesh (only an effect's port holds one)");
					return GRAPH_NO_VALUE;
				}
				std::array<f32, 16> components{};
				if (!ConstantValue(id, node, port, resolved.target, components)) {
					return GRAPH_NO_VALUE;
				}
				return _builder.Constant(resolved.target, {components.data(), GraphTypes::Components(resolved.target)});
			}

			GraphValueId ConnectionState(u32 id, const GraphNode& node, const GraphPortDefinition& port) {
				const GraphInput* input = FindDocumentInput(node, port.name);
				u32 from = input != nullptr ? input->fromNode : 0;
				for (u32 hops = 0; from != 0 && hops < 64; ++hops) {
					auto source = _nodes.find(from);
					if (source == _nodes.end() || source->second.node->def != GraphDocumentOps::REDIRECT_DEF) {
						break;
					}
					const GraphInput* through = FindDocumentInput(*source->second.node, GraphDocumentOps::REDIRECT_IN);
					from = through != nullptr ? through->fromNode : 0;
				}
				f32 state = 0.0f;
				if (from != 0) {
					auto source = _nodes.find(from);
					const GraphSetting* stored = source != _nodes.end() && source->second.node->def == GRAPH_PROPERTY_NODE
													 ? FindDocumentSetting(*source->second.node, GRAPH_PROPERTY_SETTING)
													 : nullptr;
					if (stored == nullptr) {
						Report(id, "input '" + str(GraphPortLabel(port)) + "' reads node " + std::to_string(from) + " (not a property)");
						return GRAPH_NO_VALUE;
					}
					LinearAllocator arena(stored->value.size() + 64, MemTag::Editor);
					const JSONValue json = JSONParse(stored->value, arena);
					const u32 property = json.IsNumber() ? static_cast<u32>(json.AsNumber()) : 0;
					const GraphProperty* held = nullptr;
					for (const GraphProperty& candidate : _document.properties) {
						held = candidate.id == property && property != 0 ? &candidate : held;
					}
					const str reads = "input '" + str(GraphPortLabel(port)) + "' reads ";
					if (held == nullptr) {
						Report(id, reads + "a property the blackboard doesn't have");
						return GRAPH_NO_VALUE;
					}
					GraphPropertyInfo info;
					str error;
					if (!GraphProperties::Parse(*held, info, error)) {
						Report(id, reads + "property '" + held->name + "', which " + error);
						return GRAPH_NO_VALUE;
					}
					if (!info.customBinding) {
						Report(id, reads + "property '" + held->name + "' (its Use Custom Binding is off)");
						return GRAPH_NO_VALUE;
					}
					state = _wired.contains(property) ? 1.0f : 0.0f;
				}
				return _builder.Constant(GraphValueType::Bool, {&state, 1});
			}

			bool ConstantValue(u32 id, const GraphNode& node, const GraphPortDefinition& port, GraphValueType type, std::array<f32, 16>& out) {
				u32 dimension = GraphTypes::MatrixDimension(type);
				std::array<f32, 16> full{};
				for (u32 row = 0; row < 4; ++row) {
					for (u32 column = 0; column < 4; ++column) {
						f32 value = port.defaultValue[column];
						if (port.type == GraphPortType::DynamicMatrix || GraphTypes::MatrixDimension(FixedType(port.type)) != 0) {
							value = row == column ? 1.0f : 0.0f;
						}
						full[row * 4 + column] = value;
					}
				}

				const GraphInput* input = FindDocumentInput(node, port.name);
				if (input != nullptr && !input->value.empty()) {
					LinearAllocator arena(input->value.size() + 64, MemTag::Editor);
					JSONValue json = JSONParse(input->value, arena);
					if (json.IsNumber()) {
						full[0] = static_cast<f32>(json.AsNumber());
					} else if (json.IsBool()) {
						full[0] = json.AsBool() ? 1.0f : 0.0f;
					} else if (json.IsArray() && json.Size() <= 16) {
						for (size_t i = 0; i < json.Size(); ++i) {
							if (!json[i].IsNumber()) {
								Report(id, "input '" + str(GraphPortLabel(port)) + "' holds a constant that is not a list of numbers");
								return false;
							}
							full[i] = static_cast<f32>(json[i].AsNumber());
						}
					} else {
						Report(id, "input '" + str(GraphPortLabel(port)) + "' holds a constant that is not a list of numbers");
						return false;
					}
				}

				if (dimension != 0) {
					for (u32 row = 0; row < dimension; ++row) {
						for (u32 column = 0; column < dimension; ++column) {
							out[row * dimension + column] = full[row * 4 + column];
						}
					}
				} else {
					for (u32 i = 0; i < GraphTypes::Components(type); ++i) {
						out[i] = full[i];
					}
				}
				return true;
			}

			bool SettingValue(u32 id, const GraphNode& node, const GraphSettingDefinition& setting, GraphSettingValue& out) {
				out.index = setting.defaultIndex;
				out.text = setting.defaultText;
				for (u32 i = 0; i < 4; ++i) {
					out.numbers[i] = setting.defaultValue[i];
				}
				const GraphSetting* stored = FindDocumentSetting(node, setting.key);
				if (stored == nullptr && setting.kind != GraphSettingKind::Property && setting.kind != GraphSettingKind::Block) {
					return true;
				}
				LinearAllocator arena((stored != nullptr ? stored->value.size() : 0) + 64, MemTag::Editor);
				JSONValue json = stored != nullptr ? JSONParse(stored->value, arena) : JSONValue{};
				switch (setting.kind) {
				case GraphSettingKind::Choice: {
					std::string_view name = json.AsString();
					for (u32 i = 0; i < setting.options.size(); ++i) {
						if (setting.options[i] == name) {
							out.index = i;
							return true;
						}
					}
					if (setting.optionsMove) {
						return true;
					}
					Report(id, "setting '" + str(setting.key) + "' is '" + str(name) + "' (not one of its options)");
					return false;
				}
				case GraphSettingKind::Toggle:
					if (!json.IsBool()) {
						Report(id, "setting '" + str(setting.key) + "' isn't true or false");
						return false;
					}
					out.index = json.AsBool() ? 1 : 0;
					return true;
				case GraphSettingKind::Text:
					if (!json.IsString()) {
						Report(id, "setting '" + str(setting.key) + "' isn't text");
						return false;
					}
					_texts.push_back(str(json.AsString()));
					out.text = _texts.back();
					return true;
				case GraphSettingKind::Number:
					if (!json.IsNumber()) {
						Report(id, "setting '" + str(setting.key) + "' isn't a number");
						return false;
					}
					out.numbers[0] = static_cast<f32>(json.AsNumber());
					return true;
				case GraphSettingKind::Gradient: {
					_gradients.emplace_back();
					str error;
					if (!GraphGradients::Read(json, _gradients.back(), error)) {
						Report(id, "setting '" + str(setting.key) + "' " + error);
						return false;
					}
					out.data = _gradients.back();
					return true;
				}
				case GraphSettingKind::Color:
				case GraphSettingKind::Vector:
					if (!json.IsArray() || json.Size() > 4) {
						Report(id, "setting '" + str(setting.key) + "' isn't a list of 1-4 numbers");
						return false;
					}
					for (size_t i = 0; i < json.Size(); ++i) {
						if (!json[i].IsNumber()) {
							Report(id, "setting '" + str(setting.key) + "' isn't a list of 1-4 numbers");
							return false;
						}
						out.numbers[i] = static_cast<f32>(json[i].AsNumber());
					}
					return true;
				case GraphSettingKind::Asset:
					if (!json.IsString() && !json.IsNull()) {
						Report(id, "setting '" + str(setting.key) + "' isn't an asset");
						return false;
					}
					_texts.push_back(json.IsString() ? str(json.AsString()) : str());
					out.text = _texts.back();
					return true;
				case GraphSettingKind::Property: {
					u32 property = json.IsNumber() ? static_cast<u32>(json.AsNumber()) : 0;
					if (auto bound = _bound.find(property); bound != _bound.end() && property != 0) {
						out.value = bound->second;
						out.type = _builder.TypeOf(bound->second);
						for (const GraphProperty& candidate : _document.properties) {
							if (candidate.id == property) {
								_texts.push_back(candidate.reference);
								out.text = _texts.back();
							}
						}
						return true;
					}
					for (const GraphProperty& candidate : _document.properties) {
						if (candidate.id != property || property == 0) {
							continue;
						}
						GraphPropertyInfo info;
						str error;
						if (!GraphProperties::Parse(candidate, info, error)) {
							Report(id, "reads property '" + candidate.name + "', which " + error);
							return false;
						}
						out.type = GraphProperties::ValueType(info.kind);
						_texts.push_back(info.reference);
						out.text = _texts.back();
						_texts.push_back(GraphProperties::ReadExpression(info));
						out.expression = _texts.back();
						if (GraphProperties::IsTexture(info.kind)) {
							_texts.push_back(GraphProperties::SamplerName(info));
							out.sampler = _texts.back();
						}
						return true;
					}
					Report(id, "reads a property the blackboard doesn't have");
					return false;
				}
				case GraphSettingKind::Block: {
					const str name = BlockName(node, setting);
					_texts.push_back(name);
					out.text = _texts.back();
					if (const GraphBinding* binding = FindBinding("Block/" + name)) {
						out.value = binding->Read(_builder);
						out.type = binding->type;
						return true;
					}
					auto block = _nodes.find(FindBlockNamed(name, setting.options));
					if (block == _nodes.end() || block->second.definition == nullptr || block->second.definition->inputs.empty()) {
						Report(id, "reads '" + name + "' (no block on the stack has that name)");
						return false;
					}
					if (block->second.state != NodeState::Built) {
						return false;
					}
					out.value = block->second.inputs[0];
					out.type = _builder.TypeOf(out.value);
					return true;
				}
				}
				return false;
			}

			const GraphDocument& _document;
			const GraphCompileRequest& _request;
			GraphDefinitions& _definitions;
			GraphProgram& _program;
			GraphProgramBuilder _builder;
			Vector<GraphDiagnostic>& _diagnostics;
			std::pmr::unordered_map<u32, NodeEntry> _nodes;
			u32 _attributeTo = 0;
			Vector<ChainLink> _chain{GetMemoryResource(MemTag::Editor)};
			Vector<u32> _path{GetMemoryResource(MemTag::Editor)};
			std::pmr::unordered_map<u32, GraphValueId> _bound{GetMemoryResource(MemTag::Editor)};
			std::pmr::unordered_set<u32> _wired{GetMemoryResource(MemTag::Editor)};
			std::pmr::deque<str> _texts{GetMemoryResource(MemTag::Editor)};
			std::pmr::deque<std::array<f32, GRAPH_GRADIENT_COMPONENTS>> _gradients{GetMemoryResource(MemTag::Editor)};
			bool _failed = false;
		};

	} // anonymous namespace

	GraphValueId GraphBinding::Read(GraphProgramBuilder& builder) const {
		if (GraphTypes::IsTexture(type)) {
			return builder.Texture(type, name, expression, sampler, stages, unavailable);
		}
		return builder.External(type, name, expression, stages, unavailable);
	}

	namespace GraphCompiler {

		bool Compile(const GraphDocument& document, const GraphCompileRequest& request, GraphProgram& out, Vector<GraphDiagnostic>& diagnostics) {
			out = GraphProgram{};
			size_t firstDiagnostic = diagnostics.size();
			GraphDefinitions local;
			GraphDefinitions& definitions = request.definitions ? *request.definitions : local;
			Compilation compilation(document, request, definitions, out, diagnostics);
			bool built = compilation.Run();
			if (built) {
				built = GraphPasses::AssignStages(out, diagnostics);
			}
			if (!built) {
				out = GraphProgram{};
				return false;
			}
			GraphPasses::Fold(out);
			GraphPasses::Deduplicate(out);
			GraphPasses::Eliminate(out);
			return diagnostics.size() == firstDiagnostic;
		}

		namespace {
			str PortBinding(std::string_view kind, u32 node, std::string_view port, std::span<const u32> within) {
				str name(kind);
				for (const u32 use : within) {
					name += "/" + std::to_string(use);
				}
				return name + "/" + std::to_string(node) + "/" + str(port);
			}
		} // anonymous namespace

		str PortTextureBinding(u32 node, std::string_view port, std::span<const u32> within) { return PortBinding("PortTexture", node, port, within); }

		str PortMeshBinding(u32 node, std::string_view port, std::span<const u32> within) { return PortBinding("PortMesh", node, port, within); }

		u32 Identity(u32 node, std::span<const u32> within) {
			if (within.empty()) {
				return node;
			}
			u32 hash = 2166136261u;
			const auto mix = [&hash](u32 value) {
				for (u32 shift = 0; shift < 32; shift += 8) {
					hash = (hash ^ ((value >> shift) & 0xffu)) * 16777619u;
				}
			};
			for (const u32 use : within) {
				mix(use);
			}
			mix(node);
			return hash | (1u << 28);
		}

	} // namespace GraphCompiler

} // namespace Shalltear
