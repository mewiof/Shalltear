#include "EffectGraphEditOps.h"

#include "GraphCompiler.h"
#include "GraphCurves.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphEffectBlocks.h"
#include "GraphNodeLibrary.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>

namespace Shalltear {

	namespace {

		constexpr std::string_view CONTEXTS[] = {GRAPH_EFFECT_EVENT, GRAPH_EFFECT_GPU_EVENT, GRAPH_EFFECT_OUTPUT_EVENT, GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_INITIALIZE,
												 GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_OUTPUT_POINT,
												 GRAPH_EFFECT_OUTPUT_LINE,
												 GRAPH_EFFECT_OUTPUT_LIT, GRAPH_EFFECT_OUTPUT_LIT_MESH, GRAPH_EFFECT_OUTPUT_NODE_SHADER,
												 GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH, GRAPH_EFFECT_OUTPUT_STRIP, GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP};

		constexpr std::string_view PROPERTY_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4", "Color", "Boolean"};

		constexpr std::string_view COMPOSITION_SYNONYMS[] = {"overwrite write", "increase plus sum", "scale times product", "lerp mix"};

		const GraphContext* FindContext(const GraphDocument& document, u32 id) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					return &context;
				}
			}
			return nullptr;
		}

		const GraphNode* FindBlock(const GraphDocument& document, u32 id, const GraphContext** holder = nullptr) {
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& block : context.blocks) {
					if (block.id == id) {
						if (holder) {
							*holder = &context;
						}
						return &block;
					}
				}
			}
			return nullptr;
		}

		str VariantName(std::string_view composition, std::string_view attribute, bool source, std::string_view random = {}) {
			str name = str(composition) + " " + str(attribute);
			name[composition.size() + 1] = static_cast<char>(std::toupper(static_cast<unsigned char>(attribute[0])));
			if (source) {
				return name + " from Source";
			}
			return random.empty() ? name : name + " Random (" + str(random) + ")";
		}

		GraphSetting Chosen(std::string_view key, std::string_view option) {
			return GraphSetting{str(key), NodeShaderEditOps::Quote(option)};
		}

		void SortSettings(Vector<GraphSetting>& settings) {
			std::sort(settings.begin(), settings.end(), [](const GraphSetting& a, const GraphSetting& b) { return a.key < b.key; });
		}

	} // anonymous namespace

	namespace EffectGraphEditOps {

		std::span<const std::string_view> Contexts() { return CONTEXTS; }

		void ContextChoices(Vector<BlockChoice>& out) {
			out.clear();
			for (std::string_view def : CONTEXTS) {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
				if (!definition) {
					continue;
				}
				BlockChoice choice;
				choice.name = str(definition->name);
				choice.category = "Context";
				choice.synonyms = str(definition->synonyms);
				choice.def = str(def);
				out.push_back(std::move(choice));
				if (def == GRAPH_EFFECT_INITIALIZE) {
					BlockChoice strips;
					strips.name = "Initialize Particle Strip";
					strips.category = "Context";
					strips.synonyms = "strip trail ribbon";
					strips.def = str(def);
					strips.settings.push_back(Chosen("Data Type", GRAPH_EFFECT_DATA_TYPES[1]));
					out.push_back(std::move(strips));
				}
			}
		}

		u32 AddContext(GraphDocument& document, std::string_view def, Vec2 position) {
			if (!GraphEffectRules::IsContext(def) || !GraphNodeLibrary::Find(def)) {
				return 0;
			}
			GraphContext context;
			context.node.id = document.nextId++;
			context.node.def = str(def);
			document.contexts.push_back(std::move(context));
			GraphDocumentOps::MoveNode(document, document.contexts.back().node.id, position);
			return document.contexts.back().node.id;
		}

		u32 AddContext(GraphDocument& document, const BlockChoice& choice, Vec2 position) {
			const u32 created = AddContext(document, choice.def, position);
			for (GraphContext& context : document.contexts) {
				if (created != 0 && context.node.id == created) {
					context.node.settings.assign(choice.settings.begin(), choice.settings.end());
					SortSettings(context.node.settings);
				}
			}
			return created;
		}

		std::span<const std::string_view> FlowInputs(const GraphDocument& document, u32 context, GraphDefinitions* definitions) {
			const GraphContext* held = FindContext(document, context);
			if (held && held->node.def == GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
				const GraphNodeDefinition* built = definitions ? definitions->Find(document, held->node) : nullptr;
				return built ? built->flowInputs : std::span<const std::string_view>{};
			}
			const GraphNodeDefinition* definition = held ? GraphNodeLibrary::Find(held->node.def) : nullptr;
			return definition ? definition->flowInputs : std::span<const std::string_view>{};
		}

		bool FlowOutput(const GraphDocument& document, u32 context) {
			const GraphContext* held = FindContext(document, context);
			const GraphNodeDefinition* definition = held ? GraphNodeLibrary::Find(held->node.def) : nullptr;
			return definition && definition->flowOutput;
		}

		bool CanLink(const GraphDocument& document, u32 from, u32 to, std::string_view slot, str* why, GraphDefinitions* definitions) {
			auto refuse = [&](str message) {
				if (why) {
					*why = std::move(message);
				}
				return false;
			};
			const GraphContext* source = FindContext(document, from);
			const GraphContext* target = FindContext(document, to);
			if (!source || !target || from == to) {
				return refuse("only one context may be linked to another");
			}
			const GraphNodeDefinition* sourceDefinition = GraphNodeLibrary::Find(source->node.def);
			const GraphNodeDefinition* targetDefinition = GraphNodeLibrary::Find(target->node.def);
			if (!sourceDefinition || !targetDefinition) {
				return refuse("a context with a missing definition links to nothing");
			}
			const auto slots = FlowInputs(document, to, definitions);
			if (!sourceDefinition->flowOutput || std::find(slots.begin(), slots.end(), slot) == slots.end()) {
				return refuse(str(targetDefinition->name) + " has no '" + str(slot) + "' for " + str(sourceDefinition->name) + " to link to");
			}
			if (!GraphEffectRules::MayFeed(source->node.def, target->node.def, slot)) {
				return refuse(str(sourceDefinition->name) + " cannot feed " + str(targetDefinition->name));
			}
			if (std::any_of(target->flows.begin(), target->flows.end(), [&](const GraphFlow& flow) { return flow.from == from && flow.slot == slot; })) {
				return refuse("the 2 are linked already");
			}
			if (GraphEffectRules::DrawsStrips(target->node.def)) {
				const GraphContext* initialize = source;
				if (source->node.def == GRAPH_EFFECT_UPDATE) {
					initialize = nullptr;
					for (const GraphFlow& flow : source->flows) {
						const GraphContext* feeding = FindContext(document, flow.from);
						initialize = feeding && feeding->node.def == GRAPH_EFFECT_INITIALIZE ? feeding : initialize;
					}
				}
				if (initialize && !GraphEffectRules::MakesStrips(initialize->node)) {
					return refuse(str(targetDefinition->name) + " draws strips + this system makes particles (its Initialize's Data Type is Particle)");
				}
			}
			if (target->node.def == GRAPH_EFFECT_INITIALIZE) {
				const bool gpu = source->node.def == GRAPH_EFFECT_GPU_EVENT;
				for (const GraphFlow& flow : target->flows) {
					const GraphContext* feeding = FindContext(document, flow.from);
					const bool fedByGPU = feeding && feeding->node.def == GRAPH_EFFECT_GPU_EVENT;
					if (feeding && (gpu || fedByGPU)) {
						return refuse(gpu && !fedByGPU ? "a Spawn or an Event feeds this Initialize already (a GPU Event can't feed one beside them)"
													   : "a GPU Event feeds this Initialize already (it spawns from that one alone)");
					}
				}
			}
			if (target->node.def == GRAPH_EFFECT_UPDATE && source->node.def == GRAPH_EFFECT_INITIALIZE) {
				for (const GraphContext& other : document.contexts) {
					if (other.node.id == to || other.node.def != GRAPH_EFFECT_UPDATE) {
						continue;
					}
					if (std::any_of(other.flows.begin(), other.flows.end(), [&](const GraphFlow& flow) { return flow.from == from; })) {
						return refuse("this Initialize already leads to an Update (a system has 1)");
					}
				}
			}
			return true;
		}

		bool Link(GraphDocument& document, u32 from, u32 to, std::string_view slot, GraphDefinitions* definitions) {
			if (!CanLink(document, from, to, slot, nullptr, definitions)) {
				return false;
			}
			for (GraphContext& context : document.contexts) {
				if (context.node.id == to && GraphEffectRules::TakesOneSource(context.node.def)) {
					context.flows.clear();
				}
			}
			return GraphDocumentOps::Link(document, from, to, slot);
		}

		bool CanUseSystemSubgraph(const GraphDocument& document, const Guid& asset, GraphDefinitions& definitions, const Guid& editing) {
			if (document.type != GRAPH_EFFECT_DOCUMENT_TYPE) {
				return false;
			}
			const GraphSubgraph* subgraph = asset.IsValid() && asset != editing ? definitions.Subgraph(asset) : nullptr;
			return subgraph && subgraph->kind == GraphSubgraphKind::EffectSystem &&
				   !(editing.IsValid() && GraphSubgraphs::Uses(definitions, subgraph->document, editing));
		}

		u32 AddSystemSubgraph(GraphDocument& document, const Guid& asset, Vec2 position, GraphDefinitions& definitions, const Guid& editing) {
			if (!CanUseSystemSubgraph(document, asset, definitions, editing)) {
				return 0;
			}
			const u32 created = AddContext(document, GRAPH_EFFECT_SYSTEM_SUBGRAPH, position);
			for (GraphContext& context : document.contexts) {
				if (created != 0 && context.node.id == created) {
					context.node.settings.push_back({str(GRAPH_SUBGRAPH_ASSET_SETTING), NodeShaderEditOps::Quote(asset.ToString())});
				}
			}
			return created;
		}

		static void ChoicesFor(std::string_view def, Vector<BlockChoice>& out);

		void BlockChoices(const GraphDocument& document, u32 context, Vector<BlockChoice>& out) {
			out.clear();
			const GraphContext* held = FindContext(document, context);
			if (!held) {
				return;
			}
			Vector<std::string_view> kinds{GetMemoryResource(MemTag::Editor)};
			GraphEffectRules::StandsFor(held->node, kinds);
			if (kinds.empty()) {
				ChoicesFor(held->node.def, out);
				return;
			}
			ChoicesFor(kinds[0], out);
			for (size_t k = 1; k < kinds.size(); ++k) {
				Vector<BlockChoice> other{GetMemoryResource(MemTag::Editor)};
				ChoicesFor(kinds[k], other);
				std::erase_if(out, [&other](const BlockChoice& choice) {
					return std::none_of(other.begin(), other.end(), [&choice](const BlockChoice& offered) { return offered.def == choice.def && offered.name == choice.name; });
				});
			}
		}

		static void ChoicesFor(std::string_view def, Vector<BlockChoice>& out) {
			out.clear();
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_CONSTANT_RATE)) {
				const GraphNodeDefinition* rate = GraphNodeLibrary::Find(GRAPH_EFFECT_CONSTANT_RATE);
				BlockChoice constant;
				constant.name = str(rate->name);
				constant.category = "Spawn";
				constant.synonyms = str(rate->synonyms);
				constant.def = str(GRAPH_EFFECT_CONSTANT_RATE);
				out.push_back(std::move(constant));
				for (std::string_view repeat : {std::string_view("Single"), std::string_view("Periodic")}) {
					BlockChoice burst;
					burst.name = str(repeat) + " Burst";
					burst.category = "Spawn";
					burst.synonyms = "emission burst";
					burst.def = str(GRAPH_EFFECT_BURST);
					burst.settings.push_back(Chosen("Repeat", repeat));
					out.push_back(std::move(burst));
				}
				const GraphNodeDefinition* increment = GraphNodeLibrary::Find(GRAPH_EFFECT_INCREMENT_STRIP_INDEX);
				BlockChoice strip;
				strip.name = str(increment->name);
				strip.category = "Spawn/Custom";
				strip.synonyms = str(increment->synonyms);
				strip.def = str(GRAPH_EFFECT_INCREMENT_STRIP_INDEX);
				out.push_back(std::move(strip));
				for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
					if (attribute.readOnly) {
						continue;
					}
					BlockChoice set;
					set.name = "Set SpawnEvent " + VariantName("Set", attribute.name, false).substr(4);
					set.category = "Attribute";
					set.synonyms = "spawn event attribute " + str(attribute.name);
					set.def = str(GRAPH_EFFECT_SET_SPAWN_EVENT);
					set.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, attribute.name));
					out.push_back(std::move(set));
				}
			}
			if (!GraphEffectRules::Holds(def, GRAPH_EFFECT_SET_ATTRIBUTE)) {
				return;
			}
			const bool initialize = def == GRAPH_EFFECT_INITIALIZE;
			constexpr std::string_view RANDOM_NAMES[] = {"", "Uniform", "Per Component"};
			constexpr std::string_view RANDOM_OPTIONS[] = {"Off", "Uniform", "PerComponent"};
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				if (attribute.readOnly) {
					continue;
				}
				for (u32 composition = 0; composition < std::size(GRAPH_EFFECT_COMPOSITIONS); ++composition) {
					if (attribute.type == GraphValueType::Bool && composition != 0) {
						continue;
					}
					for (u32 random = 0; random < std::size(RANDOM_NAMES); ++random) {
						if (random != 0 && (attribute.type == GraphValueType::Bool || (random == 2 && attribute.type != GraphValueType::Float3))) {
							continue;
						}
						BlockChoice choice;
						choice.name = VariantName(GRAPH_EFFECT_COMPOSITIONS[composition], attribute.name, false, RANDOM_NAMES[random]);
						choice.category = "Attribute/" + str(GRAPH_EFFECT_COMPOSITIONS[composition]);
						choice.synonyms = str(COMPOSITION_SYNONYMS[composition]) + " attribute " + str(attribute.name) + (random != 0 ? " random range" : "");
						choice.def = str(GRAPH_EFFECT_SET_ATTRIBUTE);
						choice.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, attribute.name));
						if (composition != 0) {
							choice.settings.push_back(Chosen("Composition", GRAPH_EFFECT_COMPOSITIONS[composition]));
						}
						if (random != 0) {
							choice.settings.push_back(Chosen("Random", RANDOM_OPTIONS[random]));
						}
						SortSettings(choice.settings);
						out.push_back(std::move(choice));
					}
				}
				if (initialize) {
					BlockChoice inherit;
					inherit.name = VariantName("Set", attribute.name, true);
					inherit.category = "Attribute/Set from Source";
					inherit.synonyms = "inherit source attribute " + str(attribute.name);
					inherit.def = str(GRAPH_EFFECT_SET_ATTRIBUTE);
					inherit.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, attribute.name));
					inherit.settings.push_back(Chosen("Source", "Source"));
					SortSettings(inherit.settings);
					out.push_back(std::move(inherit));
				}
			}
			if (!GraphEffectRules::Holds(def, GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE)) {
				return;
			}
			const auto fromCurve = [&out](const GraphEffectAttributeInfo& attribute, u32 composition, u32 mode) {
				BlockChoice choice;
				choice.name = VariantName(GRAPH_EFFECT_COMPOSITIONS[composition], attribute.name, false) + " " + str(GRAPH_EFFECT_SAMPLE_MODE_NAMES[mode]);
				choice.category = "Attribute from Curve/" + str(GRAPH_EFFECT_COMPOSITIONS[composition]);
				choice.synonyms = str(COMPOSITION_SYNONYMS[composition]) + " curve " + str(attribute.name) + (attribute.name == "color" ? " gradient" : "");
				choice.def = str(GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE);
				choice.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, attribute.name));
				if (composition != 0) {
					choice.settings.push_back(Chosen("Composition", GRAPH_EFFECT_COMPOSITIONS[composition]));
				}
				if (mode != 0) {
					choice.settings.push_back(Chosen("Sample Mode", GRAPH_EFFECT_SAMPLE_MODES[mode]));
				}
				SortSettings(choice.settings);
				out.push_back(std::move(choice));
			};
			constexpr u32 OVER_LIFE = 0;
			constexpr u32 BY_SPEED = 1;
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				if (attribute.readOnly || attribute.type == GraphValueType::Bool) {
					continue;
				}
				const bool age = attribute.name == "age";
				const u32 first = age ? BY_SPEED : OVER_LIFE;
				fromCurve(attribute, 0, first);
				for (u32 composition = 0; composition < std::size(GRAPH_EFFECT_COMPOSITIONS); ++composition) {
					for (u32 mode = 0; mode < 3; ++mode) {
						if ((composition == 0 && mode == first) || (age && mode == OVER_LIFE) || (attribute.name == "velocity" && mode == BY_SPEED)) {
							continue;
						}
						fromCurve(attribute, composition, mode);
					}
				}
			}

			namespace B = GraphEffectBlocks;
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_POSITION_SHAPE)) {
				for (u32 shape = 0; shape < std::size(B::POSITION_SHAPES); ++shape) {
					BlockChoice choice;
					choice.name = "Set Position Shape " + str(B::POSITION_SHAPE_NAMES[shape]);
					choice.category = "Position Shape";
					choice.synonyms = "spawn emit position shape " + str(B::POSITION_SHAPE_NAMES[shape]);
					choice.def = str(GRAPH_EFFECT_POSITION_SHAPE);
					choice.settings.push_back(Chosen("Shape", B::POSITION_SHAPES[shape]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_POSITION_MESH)) {
				const GraphNodeDefinition* mesh = GraphNodeLibrary::Find(GRAPH_EFFECT_POSITION_MESH);
				BlockChoice choice;
				choice.name = str(mesh->name);
				choice.category = "Position Shape";
				choice.synonyms = str(mesh->synonyms);
				choice.def = str(GRAPH_EFFECT_POSITION_MESH);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_POSITION_DEPTH)) {
				const GraphNodeDefinition* depth = GraphNodeLibrary::Find(GRAPH_EFFECT_POSITION_DEPTH);
				BlockChoice choice;
				choice.name = str(depth->name);
				choice.category = "Position Shape";
				choice.synonyms = str(depth->synonyms);
				choice.def = str(GRAPH_EFFECT_POSITION_DEPTH);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_POSITION_SEQUENTIAL)) {
				for (const u32 shape : {u32(B::SequentialCircle), u32(B::SequentialLine), u32(B::SequentialThreeDimensional)}) {
					BlockChoice choice;
					choice.name = "Set Position Sequential " + str(B::SEQUENTIAL_SHAPES[shape]);
					choice.category = "Position Shape/Sequential";
					choice.synonyms = "spawn emit sequence sequential order " + str(B::SEQUENTIAL_SHAPES[shape]);
					choice.def = str(GRAPH_EFFECT_POSITION_SEQUENTIAL);
					choice.settings.push_back(Chosen("Composition Position", GRAPH_EFFECT_COMPOSITIONS[0]));
					choice.settings.push_back(Chosen("Shape", B::SEQUENTIAL_SHAPES[shape]));
					SortSettings(choice.settings);
					out.push_back(std::move(choice));
				}
			}
			constexpr std::string_view FORCES[] = {GRAPH_EFFECT_FORCE, GRAPH_EFFECT_GRAVITY, GRAPH_EFFECT_LINEAR_DRAG, GRAPH_EFFECT_TURBULENCE,
												   GRAPH_EFFECT_CONFORM_TO_SPHERE};
			for (std::string_view force : FORCES) {
				if (!GraphEffectRules::Holds(def, force)) {
					continue;
				}
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(force);
				BlockChoice choice;
				choice.name = str(definition->name);
				choice.category = "Force";
				choice.synonyms = str(definition->synonyms);
				choice.def = str(force);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_FLIPBOOK_PLAYER)) {
				const GraphNodeDefinition* player = GraphNodeLibrary::Find(GRAPH_EFFECT_FLIPBOOK_PLAYER);
				BlockChoice choice;
				choice.name = str(player->name);
				choice.category = "FlipBook";
				choice.synonyms = str(player->synonyms);
				choice.def = str(GRAPH_EFFECT_FLIPBOOK_PLAYER);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_ORIENT)) {
				for (const u32 mode : {u32(B::FaceCameraPlane), u32(B::AlongVelocity), u32(B::OrientAdvanced), u32(B::FaceCameraPosition), u32(B::LookAtPosition),
									   u32(B::LookAtLine), u32(B::FixedAxis), u32(B::CustomZ), u32(B::CustomY)}) {
					BlockChoice choice;
					choice.name = "Orient: " + str(B::ORIENT_MODE_NAMES[mode]);
					choice.category = "Orientation";
					choice.synonyms = "orientation billboard facing rotate " + str(B::ORIENT_MODE_NAMES[mode]);
					choice.def = str(GRAPH_EFFECT_ORIENT);
					choice.settings.push_back(Chosen("Mode", B::ORIENT_MODES[mode]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_CAMERA_FADE)) {
				const GraphNodeDefinition* fade = GraphNodeLibrary::Find(GRAPH_EFFECT_CAMERA_FADE);
				BlockChoice choice;
				choice.name = str(fade->name);
				choice.category = "Output";
				choice.synonyms = str(fade->synonyms);
				choice.def = str(GRAPH_EFFECT_CAMERA_FADE);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_COLLISION_SHAPE)) {
				struct Family {
					u32 behavior;
					u32 first;
					std::string_view category;
					std::string_view synonyms;
				};
				const Family families[] = {
					{B::BehaviorCollision, B::CollidePlane, "Collision", "collide bounce floor wall "},
					{B::BehaviorKill, B::CollidePlane, "Collision/Kill", "kill destroy volume "},
					{B::BehaviorNone, B::CollideBox, "Collision/Trigger", "trigger detect event "},
				};
				for (const Family& family : families) {
					GraphNode probe;
					probe.def = str(GRAPH_EFFECT_COLLISION_SHAPE);
					probe.settings.push_back(Chosen("Behavior", B::BEHAVIORS[family.behavior]));
					if (!GraphEffectRules::HoldsBlock(def, probe)) {
						continue;
					}
					Vector<u32> shapes{GetMemoryResource(MemTag::Editor)};
					shapes.push_back(family.first);
					for (const u32 shape : {u32(B::CollideSphere), u32(B::CollideBox), u32(B::CollideCone), u32(B::CollidePlane)}) {
						if (shape != family.first) {
							shapes.push_back(shape);
						}
					}
					for (const u32 shape : shapes) {
						BlockChoice choice;
						choice.name = str(B::BEHAVIOR_NAMES[family.behavior]) + " Shape " + str(B::COLLISION_SHAPE_NAMES[shape]);
						choice.category = str(family.category);
						choice.synonyms = str(family.synonyms) + str(B::COLLISION_SHAPE_NAMES[shape]);
						choice.def = str(GRAPH_EFFECT_COLLISION_SHAPE);
						choice.settings.push_back(Chosen("Behavior", B::BEHAVIORS[family.behavior]));
						choice.settings.push_back(Chosen("Shape", B::COLLISION_SHAPES[shape]));
						SortSettings(choice.settings);
						out.push_back(std::move(choice));
					}
				}
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_COLLISION_DEPTH)) {
				struct Family {
					u32 behavior;
					std::string_view category;
					std::string_view synonyms;
				};
				const Family families[] = {
					{B::BehaviorCollision, "Collision", "collide bounce floor wall depth buffer camera screen"},
					{B::BehaviorKill, "Collision/Kill", "kill destroy depth buffer camera screen"},
					{B::BehaviorNone, "Collision/Trigger", "trigger detect event depth buffer camera screen"},
				};
				for (const Family& family : families) {
					BlockChoice choice;
					choice.name = str(B::BEHAVIOR_NAMES[family.behavior]) + " Depth Buffer";
					choice.category = str(family.category);
					choice.synonyms = str(family.synonyms);
					choice.def = str(GRAPH_EFFECT_COLLISION_DEPTH);
					choice.settings.push_back(Chosen("Behavior", B::BEHAVIORS[family.behavior]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_TRIGGER_EVENT)) {
				for (u32 mode = 0; mode < std::size(GRAPH_EFFECT_TRIGGER_MODES); ++mode) {
					BlockChoice choice;
					choice.name = "Trigger Event " + str(B::TRIGGER_MODE_NAMES[mode]);
					choice.category = "GPUEvent";
					choice.synonyms = "gpu event child spawn sub emitter " + str(B::TRIGGER_MODE_NAMES[mode]);
					choice.def = str(GRAPH_EFFECT_TRIGGER_EVENT);
					choice.settings.push_back(Chosen("Mode", GRAPH_EFFECT_TRIGGER_MODES[mode]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK)) {
				const GraphNodeDefinition* custom = GraphNodeLibrary::Find(GRAPH_EFFECT_CUSTOM_HLSL_BLOCK);
				BlockChoice choice;
				choice.name = str(custom->name);
				choice.category = "HLSL";
				choice.synonyms = str(custom->synonyms);
				choice.def = str(GRAPH_EFFECT_CUSTOM_HLSL_BLOCK);
				out.push_back(std::move(choice));
			}
		}

		static bool Takes(const GraphDocument& document, const GraphContext& context, const GraphNode& block, GraphDefinitions* definitions) {
			if (!GraphEffectRules::HoldsIn(context.node, block)) {
				return false;
			}
			if (block.def != GRAPH_EFFECT_SUBGRAPH_BLOCK || !definitions) {
				return true;
			}
			const GraphNodeDefinition* definition = definitions->Find(document, block);
			if (!definition || !definition->subgraph) {
				return true;
			}
			Vector<std::string_view> kinds{GetMemoryResource(MemTag::Editor)};
			GraphEffectRules::StandsFor(context.node, kinds);
			if (kinds.empty()) {
				kinds.push_back(context.node.def);
			}
			const u32 suitable = definition->subgraph->suitable;
			return std::all_of(kinds.begin(), kinds.end(), [suitable](std::string_view kind) { return GraphEffectRules::Suits(suitable, kind); });
		}

		u32 AddBlock(GraphDocument& document, u32 context, u32 index, const BlockChoice& choice, GraphDefinitions* definitions) {
			for (GraphContext& held : document.contexts) {
				if (held.node.id != context) {
					continue;
				}
				GraphNode block;
				block.def = choice.def;
				block.settings.assign(choice.settings.begin(), choice.settings.end());
				SortSettings(block.settings);
				if (!GraphNodeLibrary::Find(choice.def) || !Takes(document, held, block, definitions)) {
					return 0;
				}
				block.id = document.nextId++;
				const u32 at = std::min<u32>(index, static_cast<u32>(held.blocks.size()));
				held.blocks.insert(held.blocks.begin() + at, std::move(block));
				return held.blocks[at].id;
			}
			return 0;
		}

		bool CanMoveBlocks(const GraphDocument& document, std::span<const u32> blocks, u32 context, GraphDefinitions* definitions) {
			const GraphContext* target = FindContext(document, context);
			if (!target || blocks.empty()) {
				return false;
			}
			for (u32 id : blocks) {
				const GraphNode* block = FindBlock(document, id);
				if (!block || !Takes(document, *target, *block, definitions)) {
					return false;
				}
			}
			return true;
		}

		bool MoveBlocks(GraphDocument& document, std::span<const u32> blocks, u32 context, u32 index, GraphDefinitions* definitions) {
			return CanMoveBlocks(document, blocks, context, definitions) && GraphDocumentOps::MoveBlocks(document, blocks, context, index);
		}

		bool IsOperator(const GraphNodeDefinition& definition) {
			if (GraphEffectRules::IsContext(definition.id) || definition.category == "Context" || definition.category == "Block" ||
				definition.id == GRAPH_PROPERTY_NODE || definition.id == GraphDocumentOps::REDIRECT_DEF || definition.subgraphOnly ||
				definition.id == GRAPH_CUSTOM_FUNCTION_NODE || definition.id == GRAPH_SUBGRAPH_NODE || definition.outputs.empty()) {
				return false;
			}
			GraphDocument probe;
			probe.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
			GraphNode node;
			node.id = 1;
			node.def = str(definition.id);
			probe.nodes.push_back(std::move(node));
			probe.nextId = 2;
			const GraphCompileRoot root{1, str(definition.outputs[0].name), GraphStageVertex, false};
			Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
			GraphEffectTarget::KernelBindings(GraphEffectKernel::Update, bindings);
			GraphDefinitions definitions;
			if (GraphEffectRules::IsEffectOnly(definition.id)) {
				GraphEffectTarget::PortTextureBindings(probe, definitions, bindings);
				GraphEffectTarget::PortMeshBindings(probe, definitions, bindings, GraphEffectTarget::FRAME_HEADER_BYTES);
			}
			GraphCompileRequest request;
			request.roots = std::span<const GraphCompileRoot>(&root, 1);
			request.bindings = bindings;
			GraphProgram program;
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			return GraphCompiler::Compile(probe, request, program, diagnostics);
		}

		void OperatorChoices(Vector<BlockChoice>& out) {
			out.clear();
			const GraphNodeDefinition* get = GraphNodeLibrary::Find(GRAPH_EFFECT_GET_ATTRIBUTE);
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				BlockChoice choice;
				choice.name = "Get Attribute: " + str(attribute.name);
				choice.category = str(get->category);
				choice.synonyms = str(get->synonyms) + " " + str(attribute.name);
				choice.def = str(GRAPH_EFFECT_GET_ATTRIBUTE);
				choice.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, attribute.name));
				out.push_back(std::move(choice));
			}
			const GraphNodeDefinition* random = GraphNodeLibrary::Find(GRAPH_EFFECT_RANDOM_NUMBER);
			for (std::string_view type : {std::string_view("Float"), std::string_view("Vector2"), std::string_view("Vector3"), std::string_view("Vector4")}) {
				BlockChoice choice;
				choice.name = type == "Float" ? str("Random Number") : "Random Number (" + str(type) + ")";
				choice.category = str(random->category);
				choice.synonyms = str(random->synonyms);
				choice.def = str(GRAPH_EFFECT_RANDOM_NUMBER);
				if (type != "Float") {
					choice.settings.push_back(Chosen("Type", type));
				}
				out.push_back(std::move(choice));
			}
			const auto noise = [&out](std::string_view def, u32 type, std::string_view dimension, u32 width) {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
				BlockChoice choice;
				choice.name = std::format("{} {} {}D", GRAPH_EFFECT_NOISE_TYPES[type], def == GRAPH_EFFECT_CURL_NOISE ? "Curl Noise" : "Noise", width);
				choice.category = str(definition->category);
				choice.synonyms = str(definition->synonyms);
				choice.def = str(def);
				choice.settings.push_back(Chosen("Dimensions", dimension));
				choice.settings.push_back(Chosen("Type", GRAPH_EFFECT_NOISE_TYPES[type]));
				out.push_back(std::move(choice));
			};
			for (const bool curl : {false, true}) {
				const std::string_view def = curl ? GRAPH_EFFECT_CURL_NOISE : GRAPH_EFFECT_NOISE;
				noise(def, 1, "Three", 3);
				for (u32 type = 0; type < std::size(GRAPH_EFFECT_NOISE_TYPES); ++type) {
					for (u32 width = curl ? 2 : 1; width <= 3; ++width) {
						if ((type == 1 && width == 3) || (curl && type == 2)) {
							continue;
						}
						noise(def, type, GRAPH_EFFECT_NOISE_DIMENSIONS[width - 1], width);
					}
				}
			}
		}

		bool IsOfferedAsVariants(std::string_view definition) {
			return definition == GRAPH_EFFECT_GET_ATTRIBUTE || definition == GRAPH_EFFECT_RANDOM_NUMBER || definition == GRAPH_EFFECT_NOISE ||
				   definition == GRAPH_EFFECT_CURL_NOISE;
		}

		u32 AddOperator(GraphDocument& document, const BlockChoice& choice, Vec2 graph) {
			if (choice.def == GRAPH_SUBGRAPH_NODE) {
				for (const GraphSetting& setting : choice.settings) {
					if (setting.key == GRAPH_SUBGRAPH_ASSET_SETTING && setting.value.size() > 2) {
						return NodeShaderEditOps::AddSubgraphNode(document, Guid::FromString(std::string_view(setting.value).substr(1, setting.value.size() - 2)), graph);
					}
				}
				return 0;
			}
			if (!IsOfferedAsVariants(choice.def)) {
				return 0;
			}
			const u32 created = NodeShaderEditOps::AddNode(document, choice.def, graph);
			GraphNode* node = created != 0 ? GraphDocumentOps::FindNode(document, created) : nullptr;
			if (!node) {
				return 0;
			}
			node->settings.assign(choice.settings.begin(), choice.settings.end());
			SortSettings(node->settings);
			return created;
		}

		std::span<const std::string_view> PropertyTypes() { return PROPERTY_TYPES; }

		u32 AddOutputProperty(GraphDocument& document, std::string_view type) {
			if (document.type != GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE) {
				return 0;
			}
			const u32 created = NodeShaderEditOps::AddProperty(document, type);
			if (created != 0) {
				NodeShaderEditOps::SetPropertySetting(document, created, GRAPH_PROPERTY_OUTPUT_SETTING, "true");
			}
			return created;
		}

		static void LeaveBlockContexts(const GraphDocument& document, std::span<const u32> ids, Vector<u32>& out) {
			out.clear();
			for (const u32 id : ids) {
				const GraphContext* context = FindContext(document, id);
				if (!context || context->node.def != GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
					out.push_back(id);
				}
			}
		}

		u32 Delete(GraphDocument& document, std::span<const u32> ids) {
			Vector<u32> kept{GetMemoryResource(MemTag::Editor)};
			LeaveBlockContexts(document, ids, kept);
			return GraphDocumentOps::Delete(document, kept);
		}

		constexpr std::string_view BLOCKS_ONLY_SETTING = "Blocks Only";
		constexpr std::string_view BLOCK_PASTE_ERROR = "Select one block or one context to paste the blocks into.";

		static bool BlocksOnly(const GraphDocument& fragment) {
			return std::any_of(fragment.settings.begin(), fragment.settings.end(),
							   [](const GraphSetting& setting) { return setting.key == BLOCKS_ONLY_SETTING && setting.value == "true"; });
		}

		str Copy(const GraphDocument& document, std::span<const u32> ids) {
			Vector<const GraphNode*> blocks{GetMemoryResource(MemTag::Editor)};
			bool beside = false;
			for (const u32 id : ids) {
				if (const GraphNode* block = FindBlock(document, id)) {
					blocks.push_back(block);
				}
				beside = beside || FindContext(document, id) ||
						 std::any_of(document.nodes.begin(), document.nodes.end(), [id](const GraphNode& node) { return node.id == id; });
			}
			if (!blocks.empty() && !beside) {
				GraphDocument fragment;
				fragment.type = document.type;
				fragment.settings.push_back({str(BLOCKS_ONLY_SETTING), "true"});
				u32 highest = 0;
				for (const GraphNode* block : blocks) {
					GraphNode copied = *block;
					for (GraphInput& input : copied.inputs) {
						input.fromNode = 0;
						input.fromPort.clear();
					}
					highest = std::max(highest, copied.id);
					fragment.nodes.push_back(std::move(copied));
				}
				fragment.nextId = highest + 1;
				return GraphDocumentOps::Write(fragment);
			}
			Vector<u32> kept{GetMemoryResource(MemTag::Editor)};
			LeaveBlockContexts(document, ids, kept);
			return NodeShaderEditOps::CopyWithProperties(document, kept);
		}

		bool IsBlockFragment(std::string_view fragment) {
			GraphDocument read;
			str error;
			return GraphDocumentOps::Read(fragment, read, error) && read.type == GRAPH_EFFECT_DOCUMENT_TYPE && BlocksOnly(read);
		}

		bool PasteBlocks(GraphDocument& document, std::string_view text, std::span<const u32> selection, Vector<u32>& pasted, str& error,
						 GraphDefinitions* definitions) {
			pasted.clear();
			GraphDocument fragment;
			if (!GraphDocumentOps::Read(text, fragment, error)) {
				return false;
			}
			if (fragment.type != document.type || !BlocksOnly(fragment)) {
				error = "what is pasted isn't a copy of blocks alone";
				return false;
			}
			GraphContext* target = nullptr;
			GraphContext* only = nullptr;
			size_t after = 0;
			u32 contexts = 0;
			for (const u32 id : selection) {
				for (GraphContext& context : document.contexts) {
					if (context.node.id == id) {
						only = &context;
						++contexts;
					}
					for (size_t i = 0; i < context.blocks.size(); ++i) {
						if (context.blocks[i].id == id && (!target || i + 1 > after)) {
							target = &context;
							after = i + 1;
						}
					}
				}
			}
			if (!target && contexts == 1) {
				target = only;
				after = only->blocks.size();
			}
			if (!target) {
				error = str(BLOCK_PASTE_ERROR);
				return false;
			}
			for (GraphNode& block : fragment.nodes) {
				if (!Takes(document, *target, block, definitions)) {
					continue;
				}
				block.id = document.nextId++;
				block.group = 0;
				pasted.push_back(block.id);
				target->blocks.insert(target->blocks.begin() + static_cast<ptrdiff_t>(after++), std::move(block));
			}
			if (pasted.empty()) {
				error = "the context doesn't take any of the copied blocks";
				return false;
			}
			return true;
		}

		void SubgraphChoices(const GraphDocument& document, const Guid& editing, u32 context, GraphDefinitions& definitions, Vector<BlockChoice>& out) {
			out.clear();
			const GraphContext* held = context != 0 ? FindContext(document, context) : nullptr;
			if (context != 0 && !held) {
				return;
			}
			const GraphSubgraphKind kind = context != 0 ? GraphSubgraphKind::EffectBlock : GraphSubgraphKind::EffectOperator;
			Vector<Guid> found{GetMemoryResource(MemTag::Editor)};
			GraphSubgraphs::ProjectFiles(GraphSubgraphs::Extension(kind), found);
			for (const Guid& asset : found) {
				const GraphSubgraph* subgraph = asset == editing ? nullptr : definitions.Subgraph(asset);
				if (!subgraph || subgraph->kind != kind || (editing.IsValid() && GraphSubgraphs::Uses(definitions, subgraph->document, editing))) {
					continue;
				}
				BlockChoice choice;
				choice.name = subgraph->name;
				choice.category = subgraph->category;
				choice.synonyms = "subgraph";
				choice.def = str(kind == GraphSubgraphKind::EffectBlock ? GRAPH_EFFECT_SUBGRAPH_BLOCK : GRAPH_SUBGRAPH_NODE);
				choice.settings.push_back({str(GRAPH_SUBGRAPH_ASSET_SETTING), NodeShaderEditOps::Quote(asset.ToString())});
				if (held) {
					GraphNode probe;
					probe.def = choice.def;
					probe.settings = choice.settings;
					if (!Takes(document, *held, probe, &definitions)) {
						continue;
					}
				}
				out.push_back(std::move(choice));
			}
		}

		str OperatorSubgraphTemplate() {
			namespace Shared = NodeShaderEditOps;
			GraphDocument document;
			document.type = str(GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE);
			document.settings.push_back({str(GRAPH_SUBGRAPH_CATEGORY_SETTING), Shared::Quote(GRAPH_EFFECT_OPERATOR_SUBGRAPH_CATEGORY)});
			const u32 range = Shared::AddProperty(document, "Float");
			Shared::RenameProperty(document, range, "range");
			const u32 minimum = AddOutputProperty(document, "Float");
			Shared::RenameProperty(document, minimum, "min");
			const u32 maximum = AddOutputProperty(document, "Float");
			Shared::RenameProperty(document, maximum, "max");
			const u32 in = Shared::AddPropertyNode(document, range, {814.0f, -378.0f});
			const u32 negate = Shared::AddNode(document, "Math/Multiply", {997.0f, -309.0f});
			Shared::SetInputValue(document, negate, "B", "-1");
			GraphDocumentOps::Connect(document, in, "Out", negate, "A");
			const u32 low = Shared::AddPropertyNode(document, minimum, {1189.0f, -282.0f});
			GraphDocumentOps::Connect(document, negate, "Out", low, GRAPH_SUBGRAPH_OUTPUT_PORT);
			const u32 high = Shared::AddPropertyNode(document, maximum, {1191.0f, -380.0f});
			GraphDocumentOps::Connect(document, in, "Out", high, GRAPH_SUBGRAPH_OUTPUT_PORT);
			return GraphDocumentOps::Write(document);
		}

		str BlockSubgraphTemplate() {
			GraphDocument document;
			document.type = str(GRAPH_EFFECT_BLOCK_SUBGRAPH_DOCUMENT_TYPE);
			document.settings.push_back({str(GRAPH_SUBGRAPH_CATEGORY_SETTING), NodeShaderEditOps::Quote(GRAPH_EFFECT_BLOCK_SUBGRAPH_CATEGORY)});
			GraphContext context;
			context.node.id = document.nextId++;
			context.node.def = str(GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT);
			context.node.x = 833;
			context.node.y = -478;
			document.contexts.push_back(std::move(context));
			return GraphDocumentOps::Write(document);
		}

		namespace {

			struct EffectTemplate {
				std::string_view label;
				GraphDocument (*build)();
			};

			u32 AddNamedBlock(GraphDocument& document, u32 context, std::string_view name) {
				Vector<BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
				BlockChoices(document, context, choices);
				const GraphContext* held = FindContext(document, context);
				for (const BlockChoice& choice : choices) {
					if (choice.name == name && held) {
						return AddBlock(document, context, static_cast<u32>(held->blocks.size()), choice);
					}
				}
				return 0;
			}

			void Constant(GraphDocument& document, u32 node, std::string_view port, std::initializer_list<f32> values) {
				NodeShaderEditOps::SetInputConstant(document, node, port, {values.begin(), values.size()});
			}

			GraphDocument EmptyDocument() {
				GraphDocument document;
				document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
				return document;
			}

			u32 AddNamedContext(GraphDocument& document, std::string_view name, Vec2 position) {
				Vector<BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
				ContextChoices(choices);
				for (const BlockChoice& choice : choices) {
					if (choice.name == name) {
						return AddContext(document, choice, position);
					}
				}
				return 0;
			}

			struct System {
				u32 spawn = 0;
				u32 initialize = 0;
				u32 update = 0;
				u32 output = 0;
			};

			System AddSystem(GraphDocument& document, std::string_view initialize = "Initialize Particle", std::string_view output = "Output Particle Primitive") {
				System system;
				system.spawn = AddContext(document, GRAPH_EFFECT_SPAWN, Vec2(0.0f, 0.0f));
				system.initialize = AddNamedContext(document, initialize, Vec2(0.0f, 220.0f));
				system.update = AddContext(document, GRAPH_EFFECT_UPDATE, Vec2(0.0f, 860.0f));
				system.output = AddNamedContext(document, output, Vec2(0.0f, 1080.0f));
				Link(document, system.spawn, system.initialize, GRAPH_EFFECT_IN);
				Link(document, system.initialize, system.update, GRAPH_EFFECT_IN);
				Link(document, system.update, system.output, GRAPH_EFFECT_IN);
				return system;
			}

			constexpr std::string_view TEMPLATE_GRADIENT = R"({"alphas":[[1,0],[1,0.8],[0,1]],"colors":[[0.940652,0.233992,0.00016445,0.5000076],)"
														   R"([0.0382634,0.429261,1,0.5020981]],"mode":"Blend"})";
			constexpr std::string_view TRAIL_GRADIENT = R"({"alphas":[[1,0],[1,0.8],[0,1]],"colors":[[0.940652,0.233992,0.00016445,0.3288624],)"
														R"([0.0382634,0.429261,1,0.4140078]],"mode":"Blend"})";

			Keyframe SizeKey(f32 time, f32 value, f32 inSlope, f32 outSlope, KeyframeTangentMode mode, bool broken) {
				Keyframe key;
				key.time = time;
				key.value = value;
				key.inTangent = inSlope;
				key.outTangent = outSlope;
				key.inTangentMode = mode;
				key.outTangentMode = mode;
				key.broken = broken;
				return key;
			}

			struct Turned {
				u32 velocity = 0;
				u32 scaled = 0;
			};

			Turned TurnedVelocity(GraphDocument& document, u32 initialize, f32 blend) {
				const u32 velocity = AddNamedBlock(document, initialize, "Set Velocity");
				BlockChoice direction;
				direction.def = str(GRAPH_EFFECT_GET_ATTRIBUTE);
				direction.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, "direction"));
				const u32 normal = AddOperator(document, direction, Vec2(-1100.0f, 300.0f));
				const u32 turned = NodeShaderEditOps::AddNode(document, "Math/Lerp", Vec2(-820.0f, 260.0f));
				const u32 unit = NodeShaderEditOps::AddNode(document, "Math/Normalize", Vec2(-540.0f, 260.0f));
				const u32 scaled = NodeShaderEditOps::AddNode(document, "Math/Multiply", Vec2(-260.0f, 340.0f));
				Constant(document, turned, "B", {0.0f, 1.0f, 0.0f});
				Constant(document, turned, "T", {blend, blend, blend});
				GraphDocumentOps::Connect(document, normal, "direction", turned, "A");
				GraphDocumentOps::Connect(document, turned, "Out", unit, "In");
				GraphDocumentOps::Connect(document, unit, "Out", scaled, "A");
				GraphDocumentOps::Connect(document, scaled, "Out", velocity, "Velocity");
				return {velocity, scaled};
			}

			void SphereFalling(GraphDocument& document, const System& system, std::initializer_list<f32> lifetimes, f32 radius, f32 blend,
							   std::initializer_list<f32> speeds, const AnimationCurve& size) {
				const u32 lifetime = AddNamedBlock(document, system.initialize, "Set Lifetime Random (Uniform)");
				Constant(document, lifetime, "A", {*lifetimes.begin()});
				Constant(document, lifetime, "B", {*(lifetimes.begin() + 1)});
				Constant(document, AddNamedBlock(document, system.initialize, "Set Position Shape Sphere"), "Radius", {radius});
				const Turned turned = TurnedVelocity(document, system.initialize, blend);
				BlockChoice draw;
				draw.def = str(GRAPH_EFFECT_RANDOM_NUMBER);
				const u32 speed = AddOperator(document, draw, Vec2(-820.0f, 440.0f));
				Constant(document, speed, "Min", {*speeds.begin()});
				Constant(document, speed, "Max", {*(speeds.begin() + 1)});
				GraphDocumentOps::Connect(document, speed, "r", turned.scaled, "B");

				AddNamedBlock(document, system.update, "Gravity");
				AddNamedBlock(document, system.update, "Linear Drag");

				AddNamedBlock(document, system.output, "Orient: Face Camera Plane");
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Color Over Life"), "Color", TEMPLATE_GRADIENT);
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Size Over Life"), "Size", GraphCurves::Write(size));
			}

			GraphDocument SimpleLoop() {
				GraphDocument document = EmptyDocument();
				const System system = AddSystem(document);
				Constant(document, AddNamedBlock(document, system.spawn, "Constant Spawn Rate"), "Rate", {64.0f});
				NodeShaderEditOps::SetNodeSetting(document, system.initialize, "Capacity", "68");
				Constant(document, system.initialize, "Bounds Center", {0.0f, 1.0f, 0.0f});
				Constant(document, system.initialize, "Bounds Size", {3.5f, 4.0f, 3.5f});
				AnimationCurve size;
				size.keys.push_back(SizeKey(0.0f, 0.75f, 0.0f, 1.25f, KeyframeTangentMode::Linear, true));
				size.keys.push_back(SizeKey(1.0f, 2.0f, 1.25f, 2.0f, KeyframeTangentMode::Linear, true));
				SphereFalling(document, system, {0.8f, 1.2f}, 0.03f, 0.85f, {4.8f, 7.5f}, size);
				return document;
			}

			GraphDocument SimpleBurst() {
				GraphDocument document = EmptyDocument();
				const System system = AddSystem(document);
				NodeShaderEditOps::SetNodeSetting(document, system.spawn, "Loop Duration", "\"Constant\"");
				Constant(document, system.spawn, "Loop Duration", {1.0f});
				Constant(document, AddNamedBlock(document, system.spawn, "Single Burst"), "Count", {128.0f});
				NodeShaderEditOps::SetNodeSetting(document, system.initialize, "Capacity", "128");
				Constant(document, system.initialize, "Bounds Center", {0.0f, -2.0f, 0.0f});
				Constant(document, system.initialize, "Bounds Size", {7.0f, 8.0f, 7.0f});
				AnimationCurve size;
				size.keys.push_back(SizeKey(0.0f, 0.0f, 6.615681f, 6.615681f, KeyframeTangentMode::Free, false));
				size.keys.push_back(SizeKey(0.1f, 1.5f, 0.06686528f, 0.06686528f, KeyframeTangentMode::Free, false));
				size.keys.push_back(SizeKey(1.0f, 0.0f, -1.6666667f, -1.6666667f, KeyframeTangentMode::Auto, false));
				SphereFalling(document, system, {0.5f, 1.0f}, 0.15f, 0.0f, {0.5f, 4.5f}, size);
				return document;
			}

			GraphDocument SimpleTrail() {
				GraphDocument document = EmptyDocument();
				const System system = AddSystem(document, "Initialize Particle Strip", "Output ParticleStrip Quad");
				Constant(document, AddNamedBlock(document, system.spawn, "Constant Spawn Rate"), "Rate", {24.0f});
				NodeShaderEditOps::SetNodeSetting(document, system.initialize, "Space", "\"World\"");
				NodeShaderEditOps::SetNodeSetting(document, system.initialize, "Particle Per Strip Count", "72");
				Constant(document, system.initialize, "Bounds Center", {0.0f, 1.5f, 0.0f});
				Constant(document, system.initialize, "Bounds Size", {2.0f, 3.0f, 2.0f});

				Constant(document, AddNamedBlock(document, system.initialize, "Set Lifetime"), "Lifetime", {3.0f});
				const u32 circle = AddNamedBlock(document, system.initialize, "Set Position Sequential Circle");
				NodeShaderEditOps::SetNodeSetting(document, circle, "Composition Position", "\"Add\"");
				NodeShaderEditOps::SetNodeSetting(document, circle, "Mode", "\"Wrap\"");
				Constant(document, circle, "Count", {72.0f});
				Constant(document, circle, "Normal", {0.0f, 1.0f, 0.0f});
				Constant(document, circle, "Up", {0.0f, 0.0f, 1.0f});
				Constant(document, circle, "Radius", {0.01f});
				const Turned turned = TurnedVelocity(document, system.initialize, 0.75f);
				Constant(document, turned.scaled, "B", {1.0f, 1.0f, 1.0f});
				GraphDocumentOps::SetInputSpace(document, turned.velocity, "Velocity", "World");

				AddNamedBlock(document, system.output, "Orient: Face Camera Position");
				AnimationCurve size;
				size.keys.push_back(SizeKey(0.0f, 1.0f, 4.3229609f, 4.3229609f, KeyframeTangentMode::Free, false));
				size.keys.push_back(SizeKey(1.0f, 1.0f, -3.6624568f, -3.6624568f, KeyframeTangentMode::Free, false));
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Size Over Life"), "Size", GraphCurves::Write(size));
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Color Over Life"), "Color", TRAIL_GRADIENT);
				return document;
			}

			GraphDocument HeadAndTrail() {
				GraphDocument document = EmptyDocument();
				System heads;
				heads.spawn = AddContext(document, GRAPH_EFFECT_SPAWN, Vec2(0.0f, 0.0f));
				heads.initialize = AddNamedContext(document, "Initialize Particle", Vec2(0.0f, 220.0f));
				heads.update = AddContext(document, GRAPH_EFFECT_UPDATE, Vec2(0.0f, 1180.0f));
				heads.output = AddNamedContext(document, "Output Particle Primitive", Vec2(0.0f, 1500.0f));
				Link(document, heads.spawn, heads.initialize, GRAPH_EFFECT_IN);
				Link(document, heads.initialize, heads.update, GRAPH_EFFECT_IN);
				Link(document, heads.update, heads.output, GRAPH_EFFECT_IN);
				Constant(document, AddNamedBlock(document, heads.spawn, "Constant Spawn Rate"), "Rate", {10.0f});
				NodeShaderEditOps::SetNodeSetting(document, heads.initialize, "Capacity", "13");
				NodeShaderEditOps::SetNodeSetting(document, heads.initialize, "Space", "\"World\"");
				Constant(document, heads.initialize, "Bounds Center", {0.0f, 2.0f, 0.0f});
				Constant(document, heads.initialize, "Bounds Size", {4.5f, 7.0f, 4.5f});

				const u32 lifetime = AddNamedBlock(document, heads.initialize, "Set Lifetime Random (Uniform)");
				Constant(document, lifetime, "A", {0.8f});
				Constant(document, lifetime, "B", {1.5f});
				const u32 size = AddNamedBlock(document, heads.initialize, "Set Size Random (Uniform)");
				Constant(document, size, "A", {0.1f});
				Constant(document, size, "B", {0.3f});
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, heads.initialize, "Set Color Random from Curve"), "Color",
												 R"({"alphas":[[1,0],[1,1]],"colors":[[0.940652,0.233992,0.00016445,0.5000076],)"
												 R"([0.0382634,0.429261,1,0.5020981]],"mode":"Blend"})");
				const u32 sphere = AddNamedBlock(document, heads.initialize, "Set Position Shape Sphere");
				NodeShaderEditOps::SetNodeSetting(document, sphere, "Position Mode", "\"Volume\"");
				Constant(document, sphere, "Radius", {0.02f});
				const Turned turned = TurnedVelocity(document, heads.initialize, 0.85f);
				BlockChoice draw;
				draw.def = str(GRAPH_EFFECT_RANDOM_NUMBER);
				const u32 speed = AddOperator(document, draw, Vec2(-820.0f, 440.0f));
				Constant(document, speed, "Min", {6.5f});
				Constant(document, speed, "Max", {9.5f});
				GraphDocumentOps::Connect(document, speed, "r", turned.scaled, "B");

				GraphDocumentOps::SetInputSpace(document, AddNamedBlock(document, heads.update, "Gravity"), "Force", "World");
				AddNamedBlock(document, heads.update, "Linear Drag");
				const u32 trigger = AddNamedBlock(document, heads.update, "Trigger Event Over Time");
				Constant(document, trigger, "Rate", {60.0f});

				AddNamedBlock(document, heads.output, "Orient: Face Camera Plane");
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, heads.output, "Multiply Color Over Life"), "Color",
												 R"({"alphas":[[1,0],[1,0.5079728],[0,1]],"colors":[[1,1,1,0],[1,1,1,1]],"mode":"Blend"})");

				const u32 event = AddContext(document, GRAPH_EFFECT_GPU_EVENT, Vec2(560.0f, 1180.0f));
				const u32 strip = AddNamedContext(document, "Initialize Particle Strip", Vec2(560.0f, 1360.0f));
				const u32 trailUpdate = AddContext(document, GRAPH_EFFECT_UPDATE, Vec2(560.0f, 1980.0f));
				const u32 trail = AddNamedContext(document, "Output ParticleStrip Quad", Vec2(560.0f, 2160.0f));
				GraphDocumentOps::Connect(document, trigger, GRAPH_EFFECT_EVT, event, GRAPH_EFFECT_EVT);
				Link(document, event, strip, GRAPH_EFFECT_IN);
				Link(document, strip, trailUpdate, GRAPH_EFFECT_IN);
				Link(document, trailUpdate, trail, GRAPH_EFFECT_IN);
				NodeShaderEditOps::SetNodeSetting(document, strip, "Space", "\"World\"");
				NodeShaderEditOps::SetNodeSetting(document, strip, "Strip Capacity", "16");
				NodeShaderEditOps::SetNodeSetting(document, strip, "Particle Per Strip Count", "100");
				Constant(document, strip, "Bounds Center", {0.0f, 2.0f, 0.0f});
				Constant(document, strip, "Bounds Size", {4.5f, 7.0f, 4.5f});
				for (const std::string_view inherited : {"Color", "Position", "Alpha", "Size", "Lifetime", "Age"}) {
					AddNamedBlock(document, strip, "Set " + str(inherited) + " from Source");
				}

				AddNamedBlock(document, trail, "Orient: Face Camera Position");
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, trail, "Multiply Color Over Life"), "Color",
												 R"({"alphas":[[1,0],[1,0.8],[0,1]],"colors":[[1,1,1,0],[1,1,1,1]],"mode":"Blend"})");
				const u32 narrowing = AddNamedBlock(document, trail, "Multiply Size Over Life");
				NodeShaderEditOps::SetNodeSetting(document, narrowing, "Sample Mode", "\"Custom\"");
				AnimationCurve along;
				along.keys.push_back(SizeKey(0.0f, 0.0f, 0.0f, 0.0f, KeyframeTangentMode::Free, false));
				along.keys.push_back(SizeKey(1.0f, 0.5f, 0.0f, 0.0f, KeyframeTangentMode::Free, false));
				NodeShaderEditOps::SetInputValue(document, narrowing, "Size", GraphCurves::Write(along));
				BlockChoice index;
				index.def = str(GRAPH_EFFECT_GET_ATTRIBUTE);
				index.settings.push_back(Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, "particleIndexInStrip"));
				BlockChoice count = index;
				count.settings[0] = Chosen(GRAPH_EFFECT_ATTRIBUTE_SETTING, "particleCountInStrip");
				const u32 place = AddOperator(document, index, Vec2(-20.0f, 2240.0f));
				const u32 places = AddOperator(document, count, Vec2(-600.0f, 2380.0f));
				const u32 less = NodeShaderEditOps::AddNode(document, "Math/Subtract", Vec2(-300.0f, 2380.0f));
				const u32 most = NodeShaderEditOps::AddNode(document, "Math/Maximum", Vec2(-20.0f, 2380.0f));
				const u32 ratio = NodeShaderEditOps::AddNode(document, "Math/Divide", Vec2(260.0f, 2300.0f));
				Constant(document, less, "B", {1.0f});
				Constant(document, most, "B", {1.0f});
				GraphDocumentOps::Connect(document, places, "particleCountInStrip", less, "A");
				GraphDocumentOps::Connect(document, less, "Out", most, "A");
				GraphDocumentOps::Connect(document, place, "particleIndexInStrip", ratio, "A");
				GraphDocumentOps::Connect(document, most, "Out", ratio, "B");
				GraphDocumentOps::Connect(document, ratio, "Out", narrowing, "Sample Time");
				return document;
			}

			constexpr EffectTemplate TEMPLATES[] = {
				{"Visual Effects/Empty", &EmptyDocument},
				{"Visual Effects/Simple Loop", &SimpleLoop},
				{"Visual Effects/Simple Burst", &SimpleBurst},
				{"Visual Effects/Simple Trail", &SimpleTrail},
				{"Visual Effects/Head & Trail", &HeadAndTrail},
			};

		} // anonymous namespace

		bool SetUpdateMode(GraphDocument& document, std::string_view key, bool on) {
			const bool fixed = key == GRAPH_EFFECT_FIXED_DELTA_TIME;
			const bool exact = key == GRAPH_EFFECT_EXACT_FIXED_TIME;
			if (!fixed && !exact && key != GRAPH_EFFECT_IGNORE_TIME_SCALE) {
				return false;
			}
			const GraphEffectTiming held = GraphEffectTarget::Timing(document);
			if ((fixed ? held.fixedDeltaTime : (exact ? held.exactFixedTime : held.ignoreTimeScale)) == on) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, key, on == fixed ? std::string_view() : (on ? "true" : "false"));
		}

		bool SetPrewarm(GraphDocument& document, PrewarmField field, f64 value) {
			if (!std::isfinite(value)) {
				return false;
			}
			constexpr i64 MOST = GRAPH_EFFECT_PREWARM_MAX_STEPS;
			const f64 least = GRAPH_EFFECT_PREWARM_MIN_DELTA_TIME;
			const GraphEffectTiming held = GraphEffectTarget::Timing(document);
			i64 steps = held.prewarmStepCount;
			f64 delta = held.prewarmDeltaTime;
			const f64 total = delta * static_cast<f64>(steps);
			switch (field) {
			case PrewarmField::TotalTime: {
				const f64 wanted = std::max(value, 0.0);
				steps = wanted == 0.0 ? 0 : (steps <= 0 ? 1 : steps);
				delta = steps > 0 ? wanted / static_cast<f64>(steps) : 0.0;
				break;
			}
			case PrewarmField::StepCount: {
				const bool prewarms = total != 0.0;
				steps = std::clamp(static_cast<i64>(std::round(std::clamp(value, -1.0e9, 1.0e9))), prewarms ? i64{1} : i64{0}, MOST);
				delta = std::max(least, prewarms ? total / static_cast<f64>(steps) : 0.0);
				break;
			}
			case PrewarmField::DeltaTime: {
				delta = std::max(least, value);
				if (!(delta * static_cast<f64>(steps) > total || steps == MOST)) {
					const f64 fit = std::min(total / delta, static_cast<f64>(MOST));
					const i64 below = static_cast<i64>(std::floor(fit));
					const i64 nearest = static_cast<i64>(std::round(fit));
					const bool belowNearer = std::fabs(static_cast<f64>(below) * delta - total) < std::fabs(static_cast<f64>(nearest) * delta - total);
					steps = std::clamp(belowNearer ? below : nearest, i64{1}, MOST);
				}
				break;
			}
			}
			const u32 count = static_cast<u32>(std::max(steps, i64{0}));
			const f32 each = count > 0 ? static_cast<f32>(delta) : 0.0f;
			if (count == held.prewarmStepCount && each == held.prewarmDeltaTime) {
				return false;
			}
			NodeShaderEditOps::SetGraphSetting(document, GRAPH_EFFECT_PREWARM_STEP_COUNT, count > 0 ? std::format("{}", count) : str());
			NodeShaderEditOps::SetGraphSetting(document, GRAPH_EFFECT_PREWARM_DELTA_TIME, count > 0 ? std::format("{}", each) : str());
			return true;
		}

		bool SetCullingFlags(GraphDocument& document, u32 flags) {
			if ((flags & ~GRAPH_EFFECT_CULL_DEFAULT) != 0 || GraphEffectTarget::Timing(document).cullingFlags == flags) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GRAPH_EFFECT_CULLING_FLAGS,
													  flags == GRAPH_EFFECT_CULL_DEFAULT ? str() : std::format("{}", flags));
		}

		bool SetInstancingMode(GraphDocument& document, i32 mode) {
			const bool known = mode == GRAPH_EFFECT_INSTANCING_DISABLED || mode == GRAPH_EFFECT_INSTANCING_AUTOMATIC || mode == GRAPH_EFFECT_INSTANCING_CUSTOM;
			if (!known || GraphEffectTarget::Instancing(document).mode == mode) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GRAPH_EFFECT_INSTANCING_MODE, mode == GRAPH_EFFECT_INSTANCING_AUTOMATIC ? str() : std::format("{}", mode));
		}

		bool SetInstancingCapacity(GraphDocument& document, u32 capacity) {
			capacity = std::clamp(capacity, 1u, GRAPH_EFFECT_INSTANCING_MAX_CAPACITY);
			if (GraphEffectTarget::Instancing(document).capacity == capacity) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GRAPH_EFFECT_INSTANCING_CAPACITY,
													  capacity == GRAPH_EFFECT_INSTANCING_DEFAULT_CAPACITY ? str() : std::format("{}", capacity));
		}

		bool IgnoreError(GraphDocument& document, u32 node, std::string_view key) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || key.empty() || GraphEffectRules::IgnoresError(*target, key)) {
				return false;
			}
			str list = "[";
			for (const GraphSetting& setting : GraphEffectRules::HasIgnoredErrors(*target) ? std::span<const GraphSetting>(target->settings)
																						   : std::span<const GraphSetting>()) {
				if (setting.key == GRAPH_IGNORED_ERRORS_SETTING) {
					list = setting.value.substr(0, setting.value.size() - 1) + ",";
				}
			}
			list += NodeShaderEditOps::Quote(key) + "]";
			return NodeShaderEditOps::SetNodeSetting(document, node, GRAPH_IGNORED_ERRORS_SETTING, list);
		}

		bool ClearIgnoredErrors(GraphDocument& document, u32 node) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || !GraphEffectRules::HasIgnoredErrors(*target)) {
				return false;
			}
			return NodeShaderEditOps::SetNodeSetting(document, node, GRAPH_IGNORED_ERRORS_SETTING, "");
		}

		BoundsExclusion BoundsExclusionOf(const GraphDocument& document, u32 initialize) {
			const GraphNode* node = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), initialize);
			if (!node || node->def != GRAPH_EFFECT_INITIALIZE) {
				return BoundsExclusion::Manual;
			}
			switch (GraphEffectRules::BoundsModeOf(*node)) {
			case GRAPH_EFFECT_BOUNDS_AUTOMATIC:
				return BoundsExclusion::Automatic;
			case GRAPH_EFFECT_BOUNDS_MANUAL:
				return BoundsExclusion::Manual;
			default:
				return GraphEffectRules::ComputesBounds(*node) ? BoundsExclusion::GraphComputed : BoundsExclusion::None;
			}
		}

		std::string_view BoundsExclusionLabel(BoundsExclusion exclusion) {
			switch (exclusion) {
			case BoundsExclusion::Automatic:
				return "(automatic bounds)";
			case BoundsExclusion::Manual:
				return "(manual bounds)";
			case BoundsExclusion::GraphComputed:
				return "(bounds from the graph)";
			default:
				return {};
			}
		}

		str BoundsExclusionTooltip(BoundsExclusion exclusion) {
			if (exclusion == BoundsExclusion::None) {
				return {};
			}
			return str("The recording leaves this system out (") +
				   (exclusion == BoundsExclusion::GraphComputed ? "operators set its bounds)." : "its Bounds Setting Mode is not Recorded).");
		}

		bool ApplyRecordedBounds(GraphDocument& document, u32 initialize, const AABB& box, bool worldSystem, const Mat4& localToWorld) {
			const GraphNode* node = GraphDocumentOps::FindNode(document, initialize);
			if (!node || BoundsExclusionOf(document, initialize) != BoundsExclusion::None || !box.IsValid()) {
				return false;
			}
			bool worldCenter = false;
			for (const GraphInput& input : node->inputs) {
				worldCenter = worldCenter || (input.port == GRAPH_EFFECT_BOUNDS_CENTER && input.space == GRAPH_SPACE_WORLD);
			}
			Vec3 center = box.Center();
			if (worldSystem && !worldCenter) {
				Mat4 worldToLocal;
				if (!localToWorld.TryInvert(worldToLocal)) {
					return false;
				}
				center = worldToLocal.TransformPoint(center);
			} else if (!worldSystem && worldCenter) {
				center = localToWorld.TransformPoint(center);
			}
			const Vec3 size = box.Size();
			const f32 centered[3] = {center.x, center.y, center.z};
			const f32 sized[3] = {size.x, size.y, size.z};
			NodeShaderEditOps::SetInputConstant(document, initialize, GRAPH_EFFECT_BOUNDS_CENTER, centered);
			NodeShaderEditOps::SetInputConstant(document, initialize, GRAPH_EFFECT_BOUNDS_SIZE, sized);
			return true;
		}

		u32 TemplateCount() { return static_cast<u32>(std::size(TEMPLATES)); }

		std::string_view TemplateLabel(u32 index) { return index < std::size(TEMPLATES) ? TEMPLATES[index].label : std::string_view{}; }

		str TemplateDocument(u32 index) { return index < std::size(TEMPLATES) ? GraphDocumentOps::Write(TEMPLATES[index].build()) : str(); }

	} // namespace EffectGraphEditOps

} // namespace Shalltear
