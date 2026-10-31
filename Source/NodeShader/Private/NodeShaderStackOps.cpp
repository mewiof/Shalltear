#include "NodeShaderStackOps.h"

#include "GraphDefinitions.h"
#include "GraphNodeLibrary.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		constexpr std::string_view STACK_ORDER[] = {"Block/BaseColor", "Block/NormalTS", "Block/NormalWS", "Block/NormalOS",
													"Block/Metallic", "Block/Roughness", "Block/Reflectance", "Block/Emission",
													"Block/Occlusion", "Block/Alpha"};

		GraphContext* FindContext(GraphDocument& document, u32 id) {
			for (GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					return &context;
				}
			}
			return nullptr;
		}

		str NodeText(const GraphNode& node, std::string_view key) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key == key) {
					LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
					JSONValue json = JSONParse(setting.value, arena);
					return json.IsString() ? str(json.AsString()) : str();
				}
			}
			return {};
		}

		GraphNode* FindInterpolator(GraphDocument& document, u32 id) {
			for (GraphContext& context : document.contexts) {
				for (GraphNode& block : context.blocks) {
					if (block.id == id && block.def == GraphSurface::INTERPOLATOR_BLOCK) {
						return &block;
					}
				}
			}
			return nullptr;
		}

		bool InterpolatorNamed(const GraphDocument& document, std::string_view name, u32 except) {
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& block : context.blocks) {
					if (block.def == GraphSurface::INTERPOLATOR_BLOCK && block.id != except && NodeText(block, "Name") == name) {
						return true;
					}
				}
			}
			return false;
		}

		bool IsInterpolatorOption(std::string_view key, std::string_view option) {
			const GraphNodeDefinition* definition = GraphNodeLibrary::Find(GraphSurface::INTERPOLATOR_BLOCK);
			if (!definition) {
				return false;
			}
			for (const GraphSettingDefinition& setting : definition->settings) {
				if (setting.key == key) {
					return std::find(setting.options.begin(), setting.options.end(), option) != setting.options.end();
				}
			}
			return false;
		}

	} // anonymous namespace

	namespace NodeShaderStackOps {

		u32 FragmentContext(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == "Context/Fragment") {
					return context.node.id;
				}
			}
			return 0;
		}

		u32 VertexContext(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == "Context/Vertex") {
					return context.node.id;
				}
			}
			return 0;
		}

		bool EnsureVertexStack(GraphDocument& document) {
			if (VertexContext(document) != 0) {
				return false;
			}
			GraphContext vertex;
			vertex.node.id = document.nextId++;
			vertex.node.def = "Context/Vertex";
			const GraphContext* fragment = FindContext(document, FragmentContext(document));
			vertex.node.x = fragment ? fragment->node.x : 300;
			vertex.node.y = (fragment ? fragment->node.y : 0) - 160;
			for (std::string_view def : GraphSurface::VertexBlocks()) {
				GraphNode block;
				block.id = document.nextId++;
				block.def = str(def);
				vertex.blocks.push_back(std::move(block));
			}
			document.contexts.insert(document.contexts.begin(), std::move(vertex));
			return true;
		}

		u32 LightingContext(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == "Context/Lighting") {
					return context.node.id;
				}
			}
			return 0;
		}

		bool EnsureLightingStack(GraphDocument& document) {
			if (LightingContext(document) != 0) {
				return false;
			}
			GraphContext lighting;
			lighting.node.id = document.nextId++;
			lighting.node.def = "Context/Lighting";
			const GraphContext* fragment = FindContext(document, FragmentContext(document));
			lighting.node.x = (fragment ? fragment->node.x : 300) + 280;
			lighting.node.y = fragment ? fragment->node.y : 0;
			GraphNode radiance;
			radiance.id = document.nextId++;
			radiance.def = str(GraphSurface::RADIANCE_BLOCK_ID);
			lighting.blocks.push_back(std::move(radiance));
			document.contexts.push_back(std::move(lighting));
			return true;
		}

		bool IsBlockActive(const GraphDocument& document, std::string_view def) { return GraphSurface::IsBlockActive(document, def); }

		void MissingBlocks(const GraphDocument& document, u32 context, Vector<std::string_view>& out) {
			out.clear();
			const GraphContext* stack = FindContext(const_cast<GraphDocument&>(document), context);
			if (!stack) {
				return;
			}
			const bool vertex = stack->node.def == "Context/Vertex";
			if (stack->node.def == "Context/Lighting") {
				if (stack->blocks.empty()) {
					out.push_back(GraphSurface::RADIANCE_BLOCK_ID);
				}
				return;
			}
			if (stack->node.def == GRAPH_SUBGRAPH_OUTPUT_CONTEXT) {
				return;
			}
			if (!vertex && stack->node.def != "Context/Fragment") {
				return;
			}
			const std::span<const std::string_view> order = vertex ? GraphSurface::VertexBlocks() : std::span<const std::string_view>(STACK_ORDER);
			for (std::string_view def : order) {
				const bool held = std::any_of(stack->blocks.begin(), stack->blocks.end(), [&](const GraphNode& block) { return block.def == def; });
				if (!held) {
					out.push_back(def);
				}
			}
			if (vertex) {
				out.push_back(GraphSurface::INTERPOLATOR_BLOCK);
			}
		}

		u32 AddBlock(GraphDocument& document, u32 context, std::string_view def) {
			GraphContext* stack = FindContext(document, context);
			if (!stack) {
				return 0;
			}
			const bool vertex = stack->node.def == "Context/Vertex";
			if (stack->node.def == "Context/Lighting") {
				if (def != GraphSurface::RADIANCE_BLOCK_ID || !stack->blocks.empty()) {
					return 0;
				}
				GraphNode radiance;
				radiance.id = document.nextId++;
				radiance.def = str(def);
				stack->blocks.push_back(std::move(radiance));
				return stack->blocks.back().id;
			}
			if (vertex && def == GraphSurface::INTERPOLATOR_BLOCK) {
				str name = "CustomInterpolator";
				for (u32 n = 1; InterpolatorNamed(document, name, 0); ++n) {
					name = "CustomInterpolator" + std::to_string(n);
				}
				GraphNode block;
				block.id = document.nextId++;
				block.def = str(def);
				block.settings.push_back({str("Name"), NodeShaderEditOps::Quote(name)});
				const u32 id = block.id;
				stack->blocks.push_back(std::move(block));
				return id;
			}
			const std::span<const std::string_view> order = vertex ? GraphSurface::VertexBlocks() : std::span<const std::string_view>(STACK_ORDER);
			auto rank = [&](std::string_view of) { return static_cast<size_t>(std::find(order.begin(), order.end(), of) - order.begin()); };
			if ((!vertex && stack->node.def != "Context/Fragment") || rank(def) >= order.size()) {
				return 0;
			}
			if (std::any_of(stack->blocks.begin(), stack->blocks.end(), [&](const GraphNode& block) { return block.def == def; })) {
				return 0;
			}
			GraphNode block;
			block.id = document.nextId++;
			block.def = str(def);
			auto at = std::find_if(stack->blocks.begin(), stack->blocks.end(), [&](const GraphNode& held) { return rank(held.def) > rank(def); });
			const u32 id = block.id;
			stack->blocks.insert(at, std::move(block));
			return id;
		}

		bool RemoveBlock(GraphDocument& document, u32 block) {
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& held : context.blocks) {
					if (held.id == block) {
						const u32 ids[] = {block};
						return GraphDocumentOps::Delete(document, ids) > 0;
					}
				}
			}
			return false;
		}

		void Interpolators(const GraphDocument& document, Vector<u32>& out) {
			out.clear();
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& block : context.blocks) {
					if (block.def == GraphSurface::INTERPOLATOR_BLOCK) {
						out.push_back(block.id);
					}
				}
			}
		}

		u32 InterpolatorOf(const GraphDocument& document, u32 node) {
			const GraphNode* held = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), node);
			if (!held || held->def != INTERPOLATOR_NODE) {
				return 0;
			}
			const str name = NodeText(*held, "Interpolator");
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& block : context.blocks) {
					if (block.def == GraphSurface::INTERPOLATOR_BLOCK && NodeText(block, "Name") == name) {
						return block.id;
					}
				}
			}
			return 0;
		}

		u32 AddInterpolatorNode(GraphDocument& document, u32 block, Vec2 position) {
			const GraphNode* interpolator = FindInterpolator(document, block);
			if (!interpolator) {
				return 0;
			}
			const str name = NodeText(*interpolator, "Name");
			const str type = NodeText(*interpolator, "Type");
			const u32 id = NodeShaderEditOps::AddNode(document, INTERPOLATOR_NODE, position);
			if (id != 0) {
				NodeShaderEditOps::SetNodeSetting(document, id, "Interpolator", NodeShaderEditOps::Quote(name));
				if (!type.empty()) {
					NodeShaderEditOps::SetNodeSetting(document, id, "Type", NodeShaderEditOps::Quote(type));
				}
			}
			return id;
		}

		bool RenameInterpolator(GraphDocument& document, u32 block, std::string_view name) {
			GraphNode* interpolator = FindInterpolator(document, block);
			if (!interpolator || name.empty() || InterpolatorNamed(document, name, block)) {
				return false;
			}
			const str old = NodeText(*interpolator, "Name");
			if (old == name) {
				return false;
			}
			NodeShaderEditOps::SetNodeSetting(document, block, "Name", NodeShaderEditOps::Quote(name));
			for (GraphNode& node : document.nodes) {
				if (node.def == INTERPOLATOR_NODE && NodeText(node, "Interpolator") == old) {
					NodeShaderEditOps::SetNodeSetting(document, node.id, "Interpolator", NodeShaderEditOps::Quote(name));
				}
			}
			return true;
		}

		bool SetInterpolatorType(GraphDocument& document, u32 block, std::string_view type) {
			GraphNode* interpolator = FindInterpolator(document, block);
			if (!interpolator || !IsInterpolatorOption("Type", type)) {
				return false;
			}
			const str name = NodeText(*interpolator, "Name");
			NodeShaderEditOps::SetNodeSetting(document, block, "Type", NodeShaderEditOps::Quote(type));
			for (GraphNode& node : document.nodes) {
				if (node.def == INTERPOLATOR_NODE && NodeText(node, "Interpolator") == name) {
					NodeShaderEditOps::SetNodeSetting(document, node.id, "Type", NodeShaderEditOps::Quote(type));
				}
			}
			return true;
		}

		bool SetInterpolatorMode(GraphDocument& document, u32 block, std::string_view mode) {
			return FindInterpolator(document, block) && IsInterpolatorOption("Interpolation", mode) &&
				   NodeShaderEditOps::SetNodeSetting(document, block, "Interpolation", NodeShaderEditOps::Quote(mode));
		}

		void UpgradeOnOpen(GraphDocument& document) {
			if (document.type == GraphSurface::DOCUMENT_TYPE && GraphSurface::HasVertexStage(document)) {
				EnsureVertexStack(document);
			}
		}

	} // namespace NodeShaderStackOps

} // namespace Shalltear
