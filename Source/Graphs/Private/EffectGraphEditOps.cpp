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

		constexpr std::string_view ContextTable[] = {GraphEffectEvent, GraphEffectGPUEvent, GraphEffectOutputEventDefinition, GraphEffectSpawn, GraphEffectInitialize,
													 GraphEffectUpdate, GraphEffectOutputDefinition, GraphEffectOutputMesh, GraphEffectOutputPoint,
													 GraphEffectOutputLine,
													 GraphEffectOutputLit, GraphEffectOutputLitMesh, GraphEffectOutputNodeShader,
													 GraphEffectOutputNodeShaderMesh, GraphEffectOutputStrip, GraphEffectOutputNodeShaderStrip};

		constexpr std::string_view PropertyTypeTable[] = {"Float", "Vector2", "Vector3", "Vector4", "Color", "Boolean"};

		constexpr std::string_view CompositionSynonyms[] = {"overwrite write", "increase plus sum", "scale times product", "lerp mix"};

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

		std::span<const std::string_view> Contexts() { return ContextTable; }

		void ContextChoices(Vector<BlockChoice>& out) {
			out.clear();
			for (std::string_view def : ContextTable) {
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
				if (def == GraphEffectInitialize) {
					BlockChoice strips;
					strips.name = "Initialize Particle Strip";
					strips.category = "Context";
					strips.synonyms = "strip trail ribbon";
					strips.def = str(def);
					strips.settings.push_back(Chosen("Data Type", GraphEffectDataTypes[1]));
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
			if (held && held->node.def == GraphEffectSystemSubgraph) {
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
				if (source->node.def == GraphEffectUpdate) {
					initialize = nullptr;
					for (const GraphFlow& flow : source->flows) {
						const GraphContext* feeding = FindContext(document, flow.from);
						initialize = feeding && feeding->node.def == GraphEffectInitialize ? feeding : initialize;
					}
				}
				if (initialize && !GraphEffectRules::MakesStrips(initialize->node)) {
					return refuse(str(targetDefinition->name) + " draws strips + this system makes particles (its Initialize's Data Type is Particle)");
				}
			}
			if (target->node.def == GraphEffectInitialize) {
				const bool gpu = source->node.def == GraphEffectGPUEvent;
				for (const GraphFlow& flow : target->flows) {
					const GraphContext* feeding = FindContext(document, flow.from);
					const bool fedByGPU = feeding && feeding->node.def == GraphEffectGPUEvent;
					if (feeding && (gpu || fedByGPU)) {
						return refuse(gpu && !fedByGPU ? "a Spawn or an Event feeds this Initialize already (a GPU Event can't feed one beside them)"
													   : "a GPU Event feeds this Initialize already (it spawns from that one alone)");
					}
				}
			}
			if (target->node.def == GraphEffectUpdate && source->node.def == GraphEffectInitialize) {
				for (const GraphContext& other : document.contexts) {
					if (other.node.id == to || other.node.def != GraphEffectUpdate) {
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
			if (document.type != GraphEffectDocumentType) {
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
			const u32 created = AddContext(document, GraphEffectSystemSubgraph, position);
			for (GraphContext& context : document.contexts) {
				if (created != 0 && context.node.id == created) {
					context.node.settings.push_back({str(GraphSubgraphAssetSetting), NodeShaderEditOps::Quote(asset.ToString())});
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
			if (GraphEffectRules::Holds(def, GraphEffectConstantRate)) {
				const GraphNodeDefinition* rate = GraphNodeLibrary::Find(GraphEffectConstantRate);
				BlockChoice constant;
				constant.name = str(rate->name);
				constant.category = "Spawn";
				constant.synonyms = str(rate->synonyms);
				constant.def = str(GraphEffectConstantRate);
				out.push_back(std::move(constant));
				for (std::string_view repeat : {std::string_view("Single"), std::string_view("Periodic")}) {
					BlockChoice burst;
					burst.name = str(repeat) + " Burst";
					burst.category = "Spawn";
					burst.synonyms = "emission burst";
					burst.def = str(GraphEffectBurst);
					burst.settings.push_back(Chosen("Repeat", repeat));
					out.push_back(std::move(burst));
				}
				const GraphNodeDefinition* increment = GraphNodeLibrary::Find(GraphEffectIncrementStripIndex);
				BlockChoice strip;
				strip.name = str(increment->name);
				strip.category = "Spawn/Custom";
				strip.synonyms = str(increment->synonyms);
				strip.def = str(GraphEffectIncrementStripIndex);
				out.push_back(std::move(strip));
				for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
					if (attribute.readOnly) {
						continue;
					}
					BlockChoice set;
					set.name = "Set SpawnEvent " + VariantName("Set", attribute.name, false).substr(4);
					set.category = "Attribute";
					set.synonyms = "spawn event attribute " + str(attribute.name);
					set.def = str(GraphEffectSetSpawnEvent);
					set.settings.push_back(Chosen(GraphEffectAttributeSetting, attribute.name));
					out.push_back(std::move(set));
				}
			}
			if (!GraphEffectRules::Holds(def, GraphEffectSetAttribute)) {
				return;
			}
			const bool initialize = def == GraphEffectInitialize;
			constexpr std::string_view RandomNames[] = {"", "Uniform", "Per Component"};
			constexpr std::string_view RandomOptions[] = {"Off", "Uniform", "PerComponent"};
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				if (attribute.readOnly) {
					continue;
				}
				for (u32 composition = 0; composition < std::size(GraphEffectCompositions); ++composition) {
					if (attribute.type == GraphValueType::Bool && composition != 0) {
						continue;
					}
					for (u32 random = 0; random < std::size(RandomNames); ++random) {
						if (random != 0 && (attribute.type == GraphValueType::Bool || (random == 2 && attribute.type != GraphValueType::Float3))) {
							continue;
						}
						BlockChoice choice;
						choice.name = VariantName(GraphEffectCompositions[composition], attribute.name, false, RandomNames[random]);
						choice.category = "Attribute/" + str(GraphEffectCompositions[composition]);
						choice.synonyms = str(CompositionSynonyms[composition]) + " attribute " + str(attribute.name) + (random != 0 ? " random range" : "");
						choice.def = str(GraphEffectSetAttribute);
						choice.settings.push_back(Chosen(GraphEffectAttributeSetting, attribute.name));
						if (composition != 0) {
							choice.settings.push_back(Chosen("Composition", GraphEffectCompositions[composition]));
						}
						if (random != 0) {
							choice.settings.push_back(Chosen("Random", RandomOptions[random]));
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
					inherit.def = str(GraphEffectSetAttribute);
					inherit.settings.push_back(Chosen(GraphEffectAttributeSetting, attribute.name));
					inherit.settings.push_back(Chosen("Source", "Source"));
					SortSettings(inherit.settings);
					out.push_back(std::move(inherit));
				}
			}
			if (!GraphEffectRules::Holds(def, GraphEffectAttributeFromCurve)) {
				return;
			}
			const auto fromCurve = [&out](const GraphEffectAttributeInfo& attribute, u32 composition, u32 mode) {
				BlockChoice choice;
				choice.name = VariantName(GraphEffectCompositions[composition], attribute.name, false) + " " + str(GraphEffectSampleModeNames[mode]);
				choice.category = "Attribute from Curve/" + str(GraphEffectCompositions[composition]);
				choice.synonyms = str(CompositionSynonyms[composition]) + " curve " + str(attribute.name) + (attribute.name == "color" ? " gradient" : "");
				choice.def = str(GraphEffectAttributeFromCurve);
				choice.settings.push_back(Chosen(GraphEffectAttributeSetting, attribute.name));
				if (composition != 0) {
					choice.settings.push_back(Chosen("Composition", GraphEffectCompositions[composition]));
				}
				if (mode != 0) {
					choice.settings.push_back(Chosen("Sample Mode", GraphEffectSampleModes[mode]));
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
				for (u32 composition = 0; composition < std::size(GraphEffectCompositions); ++composition) {
					for (u32 mode = 0; mode < 3; ++mode) {
						if ((composition == 0 && mode == first) || (age && mode == OVER_LIFE) || (attribute.name == "velocity" && mode == BY_SPEED)) {
							continue;
						}
						fromCurve(attribute, composition, mode);
					}
				}
			}

			namespace B = GraphEffectBlocks;
			if (GraphEffectRules::Holds(def, GraphEffectPositionShape)) {
				for (u32 shape = 0; shape < std::size(B::PositionShapes); ++shape) {
					BlockChoice choice;
					choice.name = "Set Position Shape " + str(B::PositionShapeNames[shape]);
					choice.category = "Position Shape";
					choice.synonyms = "spawn emit position shape " + str(B::PositionShapeNames[shape]);
					choice.def = str(GraphEffectPositionShape);
					choice.settings.push_back(Chosen("Shape", B::PositionShapes[shape]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GraphEffectPositionMesh)) {
				const GraphNodeDefinition* mesh = GraphNodeLibrary::Find(GraphEffectPositionMesh);
				BlockChoice choice;
				choice.name = str(mesh->name);
				choice.category = "Position Shape";
				choice.synonyms = str(mesh->synonyms);
				choice.def = str(GraphEffectPositionMesh);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GraphEffectPositionDepth)) {
				const GraphNodeDefinition* depth = GraphNodeLibrary::Find(GraphEffectPositionDepth);
				BlockChoice choice;
				choice.name = str(depth->name);
				choice.category = "Position Shape";
				choice.synonyms = str(depth->synonyms);
				choice.def = str(GraphEffectPositionDepth);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GraphEffectPositionSequential)) {
				for (const u32 shape : {u32(B::SequentialCircle), u32(B::SequentialLine), u32(B::SequentialThreeDimensional)}) {
					BlockChoice choice;
					choice.name = "Set Position Sequential " + str(B::SequentialShapes[shape]);
					choice.category = "Position Shape/Sequential";
					choice.synonyms = "spawn emit sequence sequential order " + str(B::SequentialShapes[shape]);
					choice.def = str(GraphEffectPositionSequential);
					choice.settings.push_back(Chosen("Composition Position", GraphEffectCompositions[0]));
					choice.settings.push_back(Chosen("Shape", B::SequentialShapes[shape]));
					SortSettings(choice.settings);
					out.push_back(std::move(choice));
				}
			}
			constexpr std::string_view Forces[] = {GraphEffectForce, GraphEffectGravity, GraphEffectLinearDrag, GraphEffectTurbulence,
												   GraphEffectConformToSphere};
			for (std::string_view force : Forces) {
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
			if (GraphEffectRules::Holds(def, GraphEffectFlipbookPlayer)) {
				const GraphNodeDefinition* player = GraphNodeLibrary::Find(GraphEffectFlipbookPlayer);
				BlockChoice choice;
				choice.name = str(player->name);
				choice.category = "FlipBook";
				choice.synonyms = str(player->synonyms);
				choice.def = str(GraphEffectFlipbookPlayer);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GraphEffectOrient)) {
				for (const u32 mode : {u32(B::FaceCameraPlane), u32(B::AlongVelocity), u32(B::OrientAdvanced), u32(B::FaceCameraPosition), u32(B::LookAtPosition),
									   u32(B::LookAtLine), u32(B::FixedAxis), u32(B::CustomZ), u32(B::CustomY)}) {
					BlockChoice choice;
					choice.name = "Orient: " + str(B::OrientModeNames[mode]);
					choice.category = "Orientation";
					choice.synonyms = "orientation billboard facing rotate " + str(B::OrientModeNames[mode]);
					choice.def = str(GraphEffectOrient);
					choice.settings.push_back(Chosen("Mode", B::OrientModes[mode]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GraphEffectCameraFade)) {
				const GraphNodeDefinition* fade = GraphNodeLibrary::Find(GraphEffectCameraFade);
				BlockChoice choice;
				choice.name = str(fade->name);
				choice.category = "Output";
				choice.synonyms = str(fade->synonyms);
				choice.def = str(GraphEffectCameraFade);
				out.push_back(std::move(choice));
			}
			if (GraphEffectRules::Holds(def, GraphEffectCollisionShape)) {
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
					probe.def = str(GraphEffectCollisionShape);
					probe.settings.push_back(Chosen("Behavior", B::Behaviors[family.behavior]));
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
						choice.name = str(B::BehaviorNames[family.behavior]) + " Shape " + str(B::CollisionShapeNames[shape]);
						choice.category = str(family.category);
						choice.synonyms = str(family.synonyms) + str(B::CollisionShapeNames[shape]);
						choice.def = str(GraphEffectCollisionShape);
						choice.settings.push_back(Chosen("Behavior", B::Behaviors[family.behavior]));
						choice.settings.push_back(Chosen("Shape", B::CollisionShapes[shape]));
						SortSettings(choice.settings);
						out.push_back(std::move(choice));
					}
				}
			}
			if (GraphEffectRules::Holds(def, GraphEffectCollisionDepth)) {
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
					choice.name = str(B::BehaviorNames[family.behavior]) + " Depth Buffer";
					choice.category = str(family.category);
					choice.synonyms = str(family.synonyms);
					choice.def = str(GraphEffectCollisionDepth);
					choice.settings.push_back(Chosen("Behavior", B::Behaviors[family.behavior]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GraphEffectTriggerEvent)) {
				for (u32 mode = 0; mode < std::size(GraphEffectTriggerModes); ++mode) {
					BlockChoice choice;
					choice.name = "Trigger Event " + str(B::TriggerModeNames[mode]);
					choice.category = "GPUEvent";
					choice.synonyms = "gpu event child spawn sub emitter " + str(B::TriggerModeNames[mode]);
					choice.def = str(GraphEffectTriggerEvent);
					choice.settings.push_back(Chosen("Mode", GraphEffectTriggerModes[mode]));
					out.push_back(std::move(choice));
				}
			}
			if (GraphEffectRules::Holds(def, GraphEffectCustomHLSLBlock)) {
				const GraphNodeDefinition* custom = GraphNodeLibrary::Find(GraphEffectCustomHLSLBlock);
				BlockChoice choice;
				choice.name = str(custom->name);
				choice.category = "HLSL";
				choice.synonyms = str(custom->synonyms);
				choice.def = str(GraphEffectCustomHLSLBlock);
				out.push_back(std::move(choice));
			}
		}

		static bool Takes(const GraphDocument& document, const GraphContext& context, const GraphNode& block, GraphDefinitions* definitions) {
			if (!GraphEffectRules::HoldsIn(context.node, block)) {
				return false;
			}
			if (block.def != GraphEffectSubgraphBlock || !definitions) {
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
				definition.id == GraphPropertyNode || definition.id == GraphDocumentOps::RedirectDef || definition.subgraphOnly ||
				definition.id == GraphCustomFunctionNode || definition.id == GraphSubgraphNode || definition.outputs.empty()) {
				return false;
			}
			GraphDocument probe;
			probe.type = str(GraphEffectDocumentType);
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
			const GraphNodeDefinition* get = GraphNodeLibrary::Find(GraphEffectGetAttribute);
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				BlockChoice choice;
				choice.name = "Get Attribute: " + str(attribute.name);
				choice.category = str(get->category);
				choice.synonyms = str(get->synonyms) + " " + str(attribute.name);
				choice.def = str(GraphEffectGetAttribute);
				choice.settings.push_back(Chosen(GraphEffectAttributeSetting, attribute.name));
				out.push_back(std::move(choice));
			}
			const GraphNodeDefinition* random = GraphNodeLibrary::Find(GraphEffectRandomNumber);
			for (std::string_view type : {std::string_view("Float"), std::string_view("Vector2"), std::string_view("Vector3"), std::string_view("Vector4")}) {
				BlockChoice choice;
				choice.name = type == "Float" ? str("Random Number") : "Random Number (" + str(type) + ")";
				choice.category = str(random->category);
				choice.synonyms = str(random->synonyms);
				choice.def = str(GraphEffectRandomNumber);
				if (type != "Float") {
					choice.settings.push_back(Chosen("Type", type));
				}
				out.push_back(std::move(choice));
			}
			const auto noise = [&out](std::string_view def, u32 type, std::string_view dimension, u32 width) {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
				BlockChoice choice;
				choice.name = std::format("{} {} {}D", GraphEffectNoiseTypes[type], def == GraphEffectCurlNoise ? "Curl Noise" : "Noise", width);
				choice.category = str(definition->category);
				choice.synonyms = str(definition->synonyms);
				choice.def = str(def);
				choice.settings.push_back(Chosen("Dimensions", dimension));
				choice.settings.push_back(Chosen("Type", GraphEffectNoiseTypes[type]));
				out.push_back(std::move(choice));
			};
			for (const bool curl : {false, true}) {
				const std::string_view def = curl ? GraphEffectCurlNoise : GraphEffectNoiseDefinition;
				noise(def, 1, "Three", 3);
				for (u32 type = 0; type < std::size(GraphEffectNoiseTypes); ++type) {
					for (u32 width = curl ? 2 : 1; width <= 3; ++width) {
						if ((type == 1 && width == 3) || (curl && type == 2)) {
							continue;
						}
						noise(def, type, GraphEffectNoiseDimensions[width - 1], width);
					}
				}
			}
		}

		bool IsOfferedAsVariants(std::string_view definition) {
			return definition == GraphEffectGetAttribute || definition == GraphEffectRandomNumber || definition == GraphEffectNoiseDefinition ||
				   definition == GraphEffectCurlNoise;
		}

		u32 AddOperator(GraphDocument& document, const BlockChoice& choice, Vec2 graph) {
			if (choice.def == GraphSubgraphNode) {
				for (const GraphSetting& setting : choice.settings) {
					if (setting.key == GraphSubgraphAssetSetting && setting.value.size() > 2) {
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

		std::span<const std::string_view> PropertyTypes() { return PropertyTypeTable; }

		u32 AddOutputProperty(GraphDocument& document, std::string_view type) {
			if (document.type != GraphEffectOperatorSubgraphDocumentType) {
				return 0;
			}
			const u32 created = NodeShaderEditOps::AddProperty(document, type);
			if (created != 0) {
				NodeShaderEditOps::SetPropertySetting(document, created, GraphPropertyOutputSetting, "true");
			}
			return created;
		}

		static void LeaveBlockContexts(const GraphDocument& document, std::span<const u32> ids, Vector<u32>& out) {
			out.clear();
			for (const u32 id : ids) {
				const GraphContext* context = FindContext(document, id);
				if (!context || context->node.def != GraphEffectBlockSubgraphContext) {
					out.push_back(id);
				}
			}
		}

		u32 Delete(GraphDocument& document, std::span<const u32> ids) {
			Vector<u32> kept{GetMemoryResource(MemTag::Editor)};
			LeaveBlockContexts(document, ids, kept);
			return GraphDocumentOps::Delete(document, kept);
		}

		constexpr std::string_view BlocksOnlySetting = "Blocks Only";
		constexpr std::string_view BlockPasteError = "Select one block or one context to paste the blocks into.";

		static bool BlocksOnly(const GraphDocument& fragment) {
			return std::any_of(fragment.settings.begin(), fragment.settings.end(),
							   [](const GraphSetting& setting) { return setting.key == BlocksOnlySetting && setting.value == "true"; });
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
				fragment.settings.push_back({str(BlocksOnlySetting), "true"});
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
			return GraphDocumentOps::Read(fragment, read, error) && read.type == GraphEffectDocumentType && BlocksOnly(read);
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
				error = str(BlockPasteError);
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
				choice.def = str(kind == GraphSubgraphKind::EffectBlock ? GraphEffectSubgraphBlock : GraphSubgraphNode);
				choice.settings.push_back({str(GraphSubgraphAssetSetting), NodeShaderEditOps::Quote(asset.ToString())});
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
			document.type = str(GraphEffectOperatorSubgraphDocumentType);
			document.settings.push_back({str(GraphSubgraphCategorySetting), Shared::Quote(GraphEffectOperatorSubgraphCategory)});
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
			GraphDocumentOps::Connect(document, negate, "Out", low, GraphSubgraphOutputPort);
			const u32 high = Shared::AddPropertyNode(document, maximum, {1191.0f, -380.0f});
			GraphDocumentOps::Connect(document, in, "Out", high, GraphSubgraphOutputPort);
			return GraphDocumentOps::Write(document);
		}

		str BlockSubgraphTemplate() {
			GraphDocument document;
			document.type = str(GraphEffectBlockSubgraphDocumentType);
			document.settings.push_back({str(GraphSubgraphCategorySetting), NodeShaderEditOps::Quote(GraphEffectBlockSubgraphCategory)});
			GraphContext context;
			context.node.id = document.nextId++;
			context.node.def = str(GraphEffectBlockSubgraphContext);
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
				document.type = str(GraphEffectDocumentType);
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
				system.spawn = AddContext(document, GraphEffectSpawn, Vec2(0.0f, 0.0f));
				system.initialize = AddNamedContext(document, initialize, Vec2(0.0f, 220.0f));
				system.update = AddContext(document, GraphEffectUpdate, Vec2(0.0f, 860.0f));
				system.output = AddNamedContext(document, output, Vec2(0.0f, 1080.0f));
				Link(document, system.spawn, system.initialize, GraphEffectIn);
				Link(document, system.initialize, system.update, GraphEffectIn);
				Link(document, system.update, system.output, GraphEffectIn);
				return system;
			}

			constexpr std::string_view TemplateGradient = R"({"alphas":[[1,0],[1,0.8],[0,1]],"colors":[[0.940652,0.233992,0.00016445,0.5000076],)"
														  R"([0.0382634,0.429261,1,0.5020981]],"mode":"Blend"})";
			constexpr std::string_view TrailGradient = R"({"alphas":[[1,0],[1,0.8],[0,1]],"colors":[[0.940652,0.233992,0.00016445,0.3288624],)"
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
				direction.def = str(GraphEffectGetAttribute);
				direction.settings.push_back(Chosen(GraphEffectAttributeSetting, "direction"));
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
				draw.def = str(GraphEffectRandomNumber);
				const u32 speed = AddOperator(document, draw, Vec2(-820.0f, 440.0f));
				Constant(document, speed, "Min", {*speeds.begin()});
				Constant(document, speed, "Max", {*(speeds.begin() + 1)});
				GraphDocumentOps::Connect(document, speed, "r", turned.scaled, "B");

				AddNamedBlock(document, system.update, "Gravity");
				AddNamedBlock(document, system.update, "Linear Drag");

				AddNamedBlock(document, system.output, "Orient: Face Camera Plane");
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Color Over Life"), "Color", TemplateGradient);
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
				NodeShaderEditOps::SetInputValue(document, AddNamedBlock(document, system.output, "Multiply Color Over Life"), "Color", TrailGradient);
				return document;
			}

			GraphDocument HeadAndTrail() {
				GraphDocument document = EmptyDocument();
				System heads;
				heads.spawn = AddContext(document, GraphEffectSpawn, Vec2(0.0f, 0.0f));
				heads.initialize = AddNamedContext(document, "Initialize Particle", Vec2(0.0f, 220.0f));
				heads.update = AddContext(document, GraphEffectUpdate, Vec2(0.0f, 1180.0f));
				heads.output = AddNamedContext(document, "Output Particle Primitive", Vec2(0.0f, 1500.0f));
				Link(document, heads.spawn, heads.initialize, GraphEffectIn);
				Link(document, heads.initialize, heads.update, GraphEffectIn);
				Link(document, heads.update, heads.output, GraphEffectIn);
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
				draw.def = str(GraphEffectRandomNumber);
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

				const u32 event = AddContext(document, GraphEffectGPUEvent, Vec2(560.0f, 1180.0f));
				const u32 strip = AddNamedContext(document, "Initialize Particle Strip", Vec2(560.0f, 1360.0f));
				const u32 trailUpdate = AddContext(document, GraphEffectUpdate, Vec2(560.0f, 1980.0f));
				const u32 trail = AddNamedContext(document, "Output ParticleStrip Quad", Vec2(560.0f, 2160.0f));
				GraphDocumentOps::Connect(document, trigger, GraphEffectEvt, event, GraphEffectEvt);
				Link(document, event, strip, GraphEffectIn);
				Link(document, strip, trailUpdate, GraphEffectIn);
				Link(document, trailUpdate, trail, GraphEffectIn);
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
				index.def = str(GraphEffectGetAttribute);
				index.settings.push_back(Chosen(GraphEffectAttributeSetting, "particleIndexInStrip"));
				BlockChoice count = index;
				count.settings[0] = Chosen(GraphEffectAttributeSetting, "particleCountInStrip");
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

			constexpr EffectTemplate Templates[] = {
				{"Visual Effects/Empty", &EmptyDocument},
				{"Visual Effects/Simple Loop", &SimpleLoop},
				{"Visual Effects/Simple Burst", &SimpleBurst},
				{"Visual Effects/Simple Trail", &SimpleTrail},
				{"Visual Effects/Head & Trail", &HeadAndTrail},
			};

		} // anonymous namespace

		bool SetUpdateMode(GraphDocument& document, std::string_view key, bool on) {
			const bool fixed = key == GraphEffectFixedDeltaTime;
			const bool exact = key == GraphEffectExactFixedTime;
			if (!fixed && !exact && key != GraphEffectIgnoreTimeScale) {
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
			NodeShaderEditOps::SetGraphSetting(document, GraphEffectPrewarmStepCount, count > 0 ? std::format("{}", count) : str());
			NodeShaderEditOps::SetGraphSetting(document, GraphEffectPrewarmDeltaTime, count > 0 ? std::format("{}", each) : str());
			return true;
		}

		bool SetCullingFlags(GraphDocument& document, u32 flags) {
			if ((flags & ~GRAPH_EFFECT_CULL_DEFAULT) != 0 || GraphEffectTarget::Timing(document).cullingFlags == flags) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GraphEffectCullingFlags,
													  flags == GRAPH_EFFECT_CULL_DEFAULT ? str() : std::format("{}", flags));
		}

		bool SetInstancingMode(GraphDocument& document, i32 mode) {
			const bool known = mode == GRAPH_EFFECT_INSTANCING_DISABLED || mode == GRAPH_EFFECT_INSTANCING_AUTOMATIC || mode == GRAPH_EFFECT_INSTANCING_CUSTOM;
			if (!known || GraphEffectTarget::Instancing(document).mode == mode) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GraphEffectInstancingMode, mode == GRAPH_EFFECT_INSTANCING_AUTOMATIC ? str() : std::format("{}", mode));
		}

		bool SetInstancingCapacity(GraphDocument& document, u32 capacity) {
			capacity = std::clamp(capacity, 1u, GRAPH_EFFECT_INSTANCING_MAX_CAPACITY);
			if (GraphEffectTarget::Instancing(document).capacity == capacity) {
				return false;
			}
			return NodeShaderEditOps::SetGraphSetting(document, GraphEffectInstancingCapacity,
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
				if (setting.key == GraphIgnoredErrorsSetting) {
					list = setting.value.substr(0, setting.value.size() - 1) + ",";
				}
			}
			list += NodeShaderEditOps::Quote(key) + "]";
			return NodeShaderEditOps::SetNodeSetting(document, node, GraphIgnoredErrorsSetting, list);
		}

		bool ClearIgnoredErrors(GraphDocument& document, u32 node) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || !GraphEffectRules::HasIgnoredErrors(*target)) {
				return false;
			}
			return NodeShaderEditOps::SetNodeSetting(document, node, GraphIgnoredErrorsSetting, "");
		}

		BoundsExclusion BoundsExclusionOf(const GraphDocument& document, u32 initialize) {
			const GraphNode* node = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), initialize);
			if (!node || node->def != GraphEffectInitialize) {
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
				worldCenter = worldCenter || (input.port == GraphEffectBoundsCenter && input.space == GraphSpaceWorld);
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
			NodeShaderEditOps::SetInputConstant(document, initialize, GraphEffectBoundsCenter, centered);
			NodeShaderEditOps::SetInputConstant(document, initialize, GraphEffectBoundsSize, sized);
			return true;
		}

		u32 TemplateCount() { return static_cast<u32>(std::size(Templates)); }

		std::string_view TemplateLabel(u32 index) { return index < std::size(Templates) ? Templates[index].label : std::string_view{}; }

		str TemplateDocument(u32 index) { return index < std::size(Templates) ? GraphDocumentOps::Write(Templates[index].build()) : str(); }

	} // namespace EffectGraphEditOps

} // namespace Shalltear
