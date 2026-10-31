#pragma once

#include "GraphsAPI.h"

#include "GraphCompiler.h"
#include "GraphNodeLibrary.h"
#include "GraphProgram.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	struct GraphNode;
	class GraphDefinitions;
	struct GraphBuiltDefinition;

	inline constexpr std::string_view GRAPH_EFFECT_DOCUMENT_TYPE = "EffectGraph";
	inline constexpr std::string_view GRAPH_EFFECT_EXTENSION = ".sleffect";

	inline constexpr std::string_view GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE = "VisualEffectSubgraphOperator";
	inline constexpr std::string_view GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION = ".sleffectop";
	inline constexpr std::string_view GRAPH_EFFECT_BLOCK_SUBGRAPH_DOCUMENT_TYPE = "VisualEffectSubgraphBlock";
	inline constexpr std::string_view GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION = ".sleffectblock";
	inline constexpr std::string_view GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT = "Context/BlockSubgraph";
	inline constexpr std::string_view GRAPH_EFFECT_SUBGRAPH_BLOCK = "Block/Subgraph";
	inline constexpr std::string_view GRAPH_EFFECT_SYSTEM_SUBGRAPH = "Context/Subgraph";
	inline constexpr std::string_view GRAPH_EFFECT_SUITABLE_CONTEXTS_SETTING = "Suitable Contexts";
	inline constexpr std::string_view GRAPH_EFFECT_SUITABLE_CONTEXTS[] = {"Spawner", "Init", "Update", "Output", "InitAndUpdate",
																		  "InitAndUpdateAndOutput", "UpdateAndOutput"};
	inline constexpr u32 GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS = 5;
	inline constexpr std::string_view GRAPH_EFFECT_OPERATOR_SUBGRAPH_CATEGORY = "Subgraph Operator";
	inline constexpr std::string_view GRAPH_EFFECT_BLOCK_SUBGRAPH_CATEGORY = "Subgraph Block";
	SHALLTEAR_GRAPHS_API bool IsEffectDocumentType(std::string_view type);

	inline constexpr std::string_view GRAPH_EFFECT_EVENT = "Context/Event";
	inline constexpr std::string_view GRAPH_EFFECT_SPAWN = "Context/Spawn";
	inline constexpr std::string_view GRAPH_EFFECT_GPU_EVENT = "Context/GPUEvent";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_EVENT = "Context/OutputEvent";
	inline constexpr std::string_view GRAPH_EFFECT_DEFAULT_OUTPUT_EVENT_NAME = "On Received Event";
	inline constexpr std::string_view GRAPH_EFFECT_INITIALIZE = "Context/Initialize";
	inline constexpr std::string_view GRAPH_EFFECT_UPDATE = "Context/Update";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT = "Context/OutputParticlePrimitive";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_MESH = "Context/OutputParticleMesh";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_POINT = "Context/OutputParticlePoint";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_LINE = "Context/OutputParticleLine";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_LIT = "Context/OutputParticleLitPrimitive";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_LIT_MESH = "Context/OutputParticleLitMesh";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_NODE_SHADER = "Context/OutputParticleNodeShaderPrimitive";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH = "Context/OutputParticleNodeShaderMesh";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_STRIP = "Context/OutputParticleStripQuad";
	inline constexpr std::string_view GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP = "Context/OutputParticleNodeShaderStripQuad";
	inline constexpr std::string_view GRAPH_EFFECT_STRIP_TILING_MODES[] = {"Stretch", "RepeatPerSegment", "Custom"};
	inline constexpr std::string_view GRAPH_EFFECT_DATA_TYPES[] = {"Particle", "ParticleStrip"};
	inline constexpr std::string_view GRAPH_EFFECT_SIMULATION_TARGET = "Simulation Target";
	inline constexpr std::string_view GRAPH_EFFECT_SIMULATION_TARGETS[] = {"GPU", "CPU"};
	inline constexpr std::string_view GRAPH_EFFECT_SYSTEM_NAME = "System Name";
	inline constexpr std::string_view GRAPH_EFFECT_DEFAULT_SYSTEM_NAME = "System";
	inline constexpr std::string_view GRAPH_EFFECT_FIXED_DELTA_TIME = "fixedDeltaTime";
	inline constexpr std::string_view GRAPH_EFFECT_EXACT_FIXED_TIME = "exactFixedTime";
	inline constexpr std::string_view GRAPH_EFFECT_IGNORE_TIME_SCALE = "ignoreTimeScale";
	inline constexpr std::string_view GRAPH_EFFECT_PREWARM_STEP_COUNT = "preWarmStepCount";
	inline constexpr std::string_view GRAPH_EFFECT_PREWARM_DELTA_TIME = "preWarmDeltaTime";
	inline constexpr u32 GRAPH_EFFECT_PREWARM_MAX_STEPS = 2400;
	inline constexpr f32 GRAPH_EFFECT_PREWARM_MIN_DELTA_TIME = 1.0f / 800.0f;
	inline constexpr std::string_view GRAPH_EFFECT_CULLING_FLAGS = "cullingFlags";
	inline constexpr u32 GRAPH_EFFECT_CULL_SIMULATION = 1;
	inline constexpr u32 GRAPH_EFFECT_CULL_BOUNDS_UPDATE = 2;
	inline constexpr u32 GRAPH_EFFECT_CULL_DEFAULT = GRAPH_EFFECT_CULL_SIMULATION | GRAPH_EFFECT_CULL_BOUNDS_UPDATE;
	inline constexpr std::string_view GRAPH_EFFECT_INSTANCING_MODE = "instancingMode";
	inline constexpr std::string_view GRAPH_EFFECT_INSTANCING_CAPACITY = "instancingCapacity";
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_DISABLED = -1;
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_AUTOMATIC = 0;
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_CUSTOM = 1;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_DEFAULT_CAPACITY = 64;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_MAX_CAPACITY = 65535;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_GPU_EVENT = 1u << 2;
	inline constexpr std::string_view GRAPH_EFFECT_BOUNDS_SETTING_MODE = "Bounds Setting Mode";
	inline constexpr std::string_view GRAPH_EFFECT_BOUNDS_MODES[] = {"Recorded", "Manual", "Automatic"};
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_RECORDED = 0;
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_MANUAL = 1;
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_AUTOMATIC = 2;
	inline constexpr std::string_view GRAPH_EFFECT_BOUNDS_CENTER = "Bounds Center";
	inline constexpr std::string_view GRAPH_EFFECT_BOUNDS_SIZE = "Bounds Size";
	inline constexpr std::string_view GRAPH_EFFECT_BOUNDS_PADDING = "Bounds Padding";
	inline constexpr std::string_view GRAPH_EFFECT_NODE_SHADER_SETTING = "Node Shader";
	inline constexpr std::string_view GRAPH_EFFECT_NODE_SHADER_TEXTURE_BINDINGS[] = {"Output/NodeShaderTexture2D", "Output/NodeShaderTexture2DArray",
																					 "Output/NodeShaderTexture3D", "Output/NodeShaderTextureCube"};
	inline constexpr std::string_view GRAPH_EFFECT_PRIMITIVES[] = {"Triangle", "Quad", "Octagon"};
	inline constexpr std::string_view GRAPH_EFFECT_COLOR_MODES[] = {"None", "BaseColor", "Emissive", "BaseColorAndEmissive"};
	inline constexpr std::string_view GRAPH_EFFECT_SMOOTHNESS_SOURCES[] = {"None", "MetallicAlpha", "AlbedoAlpha"};
	inline constexpr std::string_view GRAPH_EFFECT_LIT_MAP_PORTS[] = {"Occlusion Map", "Metallic Map", "Normal Map", "Emissive Map"};
	inline constexpr std::string_view GRAPH_EFFECT_LIT_MAP_SLOTS[] = {"occlusionMap", "metallicMap", "normalMap", "emissiveMap"};
	inline constexpr std::string_view GRAPH_EFFECT_LIT_MAP_BINDINGS[] = {"Output/OcclusionMap", "Output/MetallicMap", "Output/NormalMap",
																		 "Output/EmissiveMap"};
	inline constexpr std::string_view GRAPH_EFFECT_SORTS[] = {"Auto", "Off", "On"};
	inline constexpr std::string_view GRAPH_EFFECT_SORT_MODES[] = {"DistanceToCamera", "YoungestInFront", "CameraDepth", "Custom"};
	inline constexpr std::string_view GRAPH_EFFECT_CULL_MODE = "Cull Mode";
	inline constexpr std::string_view GRAPH_EFFECT_CULL_MODES[] = {"Default", "Front", "Back", "Off"};
	inline constexpr std::string_view GRAPH_EFFECT_Z_WRITE_MODE = "Z Write Mode";
	inline constexpr std::string_view GRAPH_EFFECT_Z_WRITE_MODES[] = {"Default", "Off", "On"};
	inline constexpr std::string_view GRAPH_EFFECT_Z_TEST_MODE = "Z Test Mode";
	inline constexpr std::string_view GRAPH_EFFECT_Z_TEST_MODES[] = {"Default", "Less", "Greater", "LEqual", "GEqual", "Equal", "NotEqual", "Always"};

	inline constexpr std::string_view GRAPH_EFFECT_CONSTANT_RATE = "Block/ConstantSpawnRate";
	inline constexpr std::string_view GRAPH_EFFECT_BURST = "Block/Burst";
	inline constexpr std::string_view GRAPH_EFFECT_SET_ATTRIBUTE = "Block/SetAttribute";
	inline constexpr std::string_view GRAPH_EFFECT_SET_SPAWN_EVENT = "Block/SetSpawnEvent";
	inline constexpr std::string_view GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE = "Block/AttributeFromCurve";
	inline constexpr std::string_view GRAPH_EFFECT_POSITION_SHAPE = "Block/PositionShape";
	inline constexpr std::string_view GRAPH_EFFECT_FORCE = "Block/Force";
	inline constexpr std::string_view GRAPH_EFFECT_GRAVITY = "Block/Gravity";
	inline constexpr std::string_view GRAPH_EFFECT_LINEAR_DRAG = "Block/LinearDrag";
	inline constexpr std::string_view GRAPH_EFFECT_TURBULENCE = "Block/Turbulence";
	inline constexpr std::string_view GRAPH_EFFECT_CONFORM_TO_SPHERE = "Block/ConformToSphere";
	inline constexpr std::string_view GRAPH_EFFECT_COLLISION_SHAPE = "Block/CollisionShape";
	inline constexpr std::string_view GRAPH_EFFECT_ORIENT = "Block/Orient";
	inline constexpr std::string_view GRAPH_EFFECT_FLIPBOOK_PLAYER = "Block/FlipbookPlayer";
	inline constexpr std::string_view GRAPH_EFFECT_VECTOR_FIELD_FORCE = "Block/VectorFieldForce";
	inline constexpr std::string_view GRAPH_EFFECT_CONFORM_TO_SDF = "Block/ConformToSignedDistanceField";
	inline constexpr std::string_view GRAPH_EFFECT_COLLISION_DEPTH = "Block/CollisionDepth";
	inline constexpr std::string_view GRAPH_EFFECT_POSITION_DEPTH = "Block/PositionDepth";
	inline constexpr std::string_view GRAPH_EFFECT_POSITION_MESH = "Block/PositionMesh";
	inline constexpr std::string_view GRAPH_EFFECT_POSITION_SEQUENTIAL = "Block/PositionSequential";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_FADE = "Block/CameraFade";
	inline constexpr std::string_view GRAPH_EFFECT_INCREMENT_STRIP_INDEX = "Block/IncrementStripIndexOnStart";
	inline constexpr std::string_view GRAPH_EFFECT_TRIGGER_EVENT = "Block/TriggerEvent";
	inline constexpr std::string_view GRAPH_EFFECT_TRIGGER_MODES[] = {"Always", "OverTime", "OverDistance", "OnDie", "OnCollide"};
	inline constexpr std::string_view GRAPH_EFFECT_EVT = "Evt";
	inline constexpr std::string_view GRAPH_EFFECT_IMPLICIT_REAP = "Trigger/ImplicitReap";
	inline constexpr std::string_view GRAPH_EFFECT_RATE_COUNT = "rateCount";

	inline constexpr std::string_view GRAPH_EFFECT_GET_ATTRIBUTE = "Attribute/GetAttribute";
	inline constexpr std::string_view GRAPH_EFFECT_AGE_OVER_LIFETIME = "Attribute/AgeOverLifetime";
	inline constexpr std::string_view GRAPH_EFFECT_RATIO_OVER_STRIP = "Attribute/RatioOverStrip";
	inline constexpr std::string_view GRAPH_EFFECT_RANDOM_NUMBER = "Random/RandomNumber";
	inline constexpr std::string_view GRAPH_EFFECT_TOTAL_TIME = "BuiltIn/TotalTime";
	inline constexpr std::string_view GRAPH_EFFECT_DELTA_TIME = "BuiltIn/DeltaTime";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_CURVE = "Sampling/SampleCurve";
	inline constexpr std::string_view GRAPH_EFFECT_NOISE = "Noise/Noise";
	inline constexpr std::string_view GRAPH_EFFECT_CURL_NOISE = "Noise/CurlNoise";
	inline constexpr std::string_view GRAPH_EFFECT_NOISE_TYPES[] = {"Value", "Perlin", "Cellular"};
	inline constexpr std::string_view GRAPH_EFFECT_NOISE_DIMENSIONS[] = {"One", "Two", "Three"};
	inline constexpr std::string_view GRAPH_EFFECT_CURL_DIMENSIONS[] = {"Two", "Three"};
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_TEXTURE_2D = "Sampling/SampleTexture2D";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_TEXTURE_2D_ARRAY = "Sampling/SampleTexture2DArray";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_TEXTURE_3D = "Sampling/SampleTexture3D";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_TEXTURE_CUBE = "Sampling/SampleTextureCube";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_SDF = "Sampling/SampleSignedDistanceField";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_MESH = "Sampling/SampleMesh";
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_MESH_INDEX = "Sampling/SampleMeshIndex";
	inline constexpr std::string_view GRAPH_EFFECT_MESH_VERTEX_COUNT = "Sampling/MeshVertexCount";
	inline constexpr std::string_view GRAPH_EFFECT_MESH_INDEX_COUNT = "Sampling/MeshIndexCount";
	inline constexpr std::string_view GRAPH_EFFECT_MESH_TRIANGLE_COUNT = "Sampling/MeshTriangleCount";

	inline constexpr std::string_view GRAPH_EFFECT_START = "Start";
	inline constexpr std::string_view GRAPH_EFFECT_STOP = "Stop";
	inline constexpr std::string_view GRAPH_EFFECT_IN = "In";

	inline constexpr std::string_view GRAPH_EFFECT_ON_PLAY = "OnPlay";
	inline constexpr std::string_view GRAPH_EFFECT_ON_STOP = "OnStop";

	inline constexpr std::string_view GRAPH_EFFECT_ATTRIBUTE_SETTING = "Attribute";
	inline constexpr std::string_view GRAPH_EFFECT_COMPOSITIONS[] = {"Set", "Add", "Multiply", "Blend"};
	inline constexpr std::string_view GRAPH_EFFECT_SOURCES[] = {"Port", "Source"};
	inline constexpr std::string_view GRAPH_EFFECT_RANDOMS[] = {"Off", "PerComponent", "Uniform"};
	inline constexpr std::string_view GRAPH_EFFECT_LOCATIONS[] = {"Current", "Source"};
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_MODES[] = {"OverLife", "BySpeed", "Random", "RandomConstantPerParticle", "Custom"};
	inline constexpr std::string_view GRAPH_EFFECT_SAMPLE_MODE_NAMES[] = {"Over Life", "By Speed", "Random from Curve", "Random Constant/Particle",
																		  "Custom"};

	inline constexpr std::string_view GRAPH_EFFECT_RANDOM_PARTICLE = "RandomKey/Particle";
	inline constexpr std::string_view GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT = "RandomKey/ParticleConstant";
	inline constexpr std::string_view GRAPH_EFFECT_RANDOM_EFFECT = "RandomKey/Effect";
	inline constexpr std::string_view GRAPH_EFFECT_RANDOM_EFFECT_CONSTANT = "RandomKey/EffectConstant";

	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_RIGHT = "Camera/Right";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_UP = "Camera/Up";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_FORWARD = "Camera/Forward";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_POSITION = "Camera/Position";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_ORTHOGRAPHIC = "Camera/Orthographic";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_CLIP_W = "Camera/ClipW";
	inline constexpr std::string_view GRAPH_EFFECT_SHADOW_PASS = "Pass/Shadow";

	inline constexpr std::string_view GRAPH_EFFECT_STRIP_PREVIOUS = "Strip/Previous";
	inline constexpr std::string_view GRAPH_EFFECT_STRIP_NEXT = "Strip/Next";

	inline constexpr std::string_view GRAPH_EFFECT_FLIPBOOK_SIZE = "Flipbook/Size";
	inline constexpr std::string_view GRAPH_EFFECT_FLIPBOOK_BLENDING = "Flipbook/Blending";

	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_DEPTH = "CameraDepth/Texture";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_DEPTH_SIZE = "CameraDepth/Size";
	inline constexpr std::string_view GRAPH_EFFECT_CAMERA_DEPTH_MATRICES[] = {"CameraDepth/EffectToView", "CameraDepth/ViewToEffect", "CameraDepth/ViewToClip",
																			  "CameraDepth/ClipToView"};
	inline constexpr u32 GRAPH_EFFECT_CAMERA_RECORD_BYTES = 4 * 64 + 16;
	inline constexpr u32 GRAPH_EFFECT_NO_BINDING = ~0u;

	inline constexpr u32 GRAPH_EFFECT_MESH_RECORD_BYTES = 32;
	inline constexpr std::string_view GRAPH_EFFECT_MESH_VERTEX_COUNT_INPUT = "/VertexCount";
	inline constexpr std::string_view GRAPH_EFFECT_MESH_INDEX_COUNT_INPUT = "/IndexCount";
	inline constexpr u32 GRAPH_EFFECT_MESH_CHANNEL_BITS = 3;

	inline constexpr std::string_view GRAPH_EFFECT_MAIN_TEXTURE = "mainTexture";
	inline constexpr std::string_view GRAPH_EFFECT_MAIN_TEXTURE_BINDING = "Output/MainTexture";

	inline constexpr u32 GRAPH_EFFECT_KERNEL_TEXTURES = 4;
	inline constexpr u32 GRAPH_EFFECT_OUTPUT_TEXTURES = 2;

	enum class GraphEffectKernel : u8 {
		Initialize,
		Update,
		Output,
		Sort
	};

	enum class GraphEffectSort : u8 {
		None,
		DistanceToCamera,
		YoungestInFront,
		CameraDepth,
		Custom
	};

	struct GraphEffectAttributeInfo {
		std::string_view name;
		GraphValueType type = GraphValueType::Float;
		f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		bool readOnly = false;
		GraphPortSpace space = GraphPortSpace::None;
	};

	namespace GraphEffectAttributes {
		SHALLTEAR_GRAPHS_API std::span<const GraphEffectAttributeInfo> All();
		SHALLTEAR_GRAPHS_API const GraphEffectAttributeInfo* Find(std::string_view name);
		SHALLTEAR_GRAPHS_API str CurrentBinding(std::string_view name);
		SHALLTEAR_GRAPHS_API str SourceBinding(std::string_view name);
		SHALLTEAR_GRAPHS_API str RateCount(u32 node);
		bool BuildsPerNode(std::string_view def);
		void Build(const GraphNode& node, GraphBuiltDefinition& out);
	} // namespace GraphEffectAttributes

	namespace GraphEffectShading {

		enum class Held : u8 {
			Default,
			Asset,
			None
		};
		SHALLTEAR_GRAPHS_API Held HeldNodeShader(const GraphNode& output, Guid& asset);

		SHALLTEAR_GRAPHS_API std::string_view DefaultDocumentText();
		inline constexpr std::string_view DEFAULT_NAME = "EffectDefault";

		SHALLTEAR_GRAPHS_API bool IsNodeShaderOutput(std::string_view def);

		void Build(const GraphNode& node, const GraphDocument* graph, GraphBuiltDefinition& out);

	} // namespace GraphEffectShading

	struct GraphEffectStored {
		str attribute;
		u32 slot = 0;
		u32 components = 1;
	};

	struct GraphEffectField {
		str name;
		u32 offset = 0;
		u32 components = 1;
	};

	struct GraphEffectSpawnBlock {
		enum class Kind : u8 {
			ConstantRate,
			SingleBurst,
			PeriodicBurst,
			SetAttribute,
			IncrementStripIndex
		};
		u32 node = 0;
		Kind kind = Kind::ConstantRate;
		GraphProgram program;
		str assembly;
		str attribute;
		u32 components = 0;
	};

	struct GraphEffectSpawner {
		u32 context = 0;
		Vector<GraphEffectSpawnBlock> blocks{GetMemoryResource(MemTag::Editor)};
		GraphProgram loop;
		str loopAssembly;
		Vector<str> start{GetMemoryResource(MemTag::Editor)};
		Vector<str> stop{GetMemoryResource(MemTag::Editor)};
		Vector<str> outputEvents{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphEffectRelay {
		str event;
		str output;
	};

	struct GraphEffectKernelTexture {
		u32 binding = 0;
		u32 samplerBinding = 0;
		Guid texture;
	};

	struct GraphEffectOutputTexture {
		str slot;
		Guid texture;
	};

	struct GraphEffectMesh {
		str binding;
		u32 offset = 0;
		Guid mesh;
	};

	struct GraphEffectOutput {
		u32 context = 0;
		str shader;
		Vector<u32> lineNodes{GetMemoryResource(MemTag::Editor)};
		RHI::BlendMode blend = RHI::BlendMode::Alpha;
		u32 vertices = 6;
		bool instanced = false;
		u32 mesh = GRAPH_EFFECT_NO_BINDING;
		Vector<GraphEffectOutputTexture> textures{GetMemoryResource(MemTag::Editor)};
		GraphEffectSort sort = GraphEffectSort::None;
		bool revertSorting = false;
		str sortKernel;
		Vector<u32> sortLines{GetMemoryResource(MemTag::Editor)};
		bool castShadows = false;
		bool strip = false;
	};

	struct GraphEffectEventTarget {
		u32 child = 0;
		u32 listsBinding = 0;
		u32 argumentsBinding = 0;
	};

	struct GraphEffectOutputEvent {
		str name;
		u32 root = 0;
	};

	struct GraphEffectCarried {
		str attribute;
		u32 root = 0;
		u32 components = 1;
	};

	struct GraphEffectSystem {
		str name;
		u32 initialize = 0;
		u32 update = 0;
		u32 capacity = 0;
		u32 strips = 0;
		bool world = false;
		u32 attributeCount = 0;
		Vector<GraphEffectStored> stored{GetMemoryResource(MemTag::Editor)};
		bool cpu = false;
		str initializeAssembly;
		str updateAssembly;
		Vector<GraphEffectOutputEvent> initializeOutputEvents{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectOutputEvent> updateOutputEvents{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectCarried> initializeCarried{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectCarried> updateCarried{GetMemoryResource(MemTag::Editor)};
		str initializeKernel;
		Vector<u32> initializeLines{GetMemoryResource(MemTag::Editor)};
		str updateKernel;
		Vector<u32> updateLines{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectKernelTexture> initializeTextures{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectKernelTexture> updateTextures{GetMemoryResource(MemTag::Editor)};
		u32 initializeDepthBinding = GRAPH_EFFECT_NO_BINDING;
		u32 updateDepthBinding = GRAPH_EFFECT_NO_BINDING;
		Vector<GraphEffectOutput> outputs{GetMemoryResource(MemTag::Editor)};
		Vector<u32> spawners{GetMemoryResource(MemTag::Editor)};
		Vector<str> events{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectField> source{GetMemoryResource(MemTag::Editor)};
		u32 batchStride = 4;
		GraphProgram bounds;
		str boundsAssembly;
		bool boundsWorld = false;
		u32 boundsMode = GRAPH_EFFECT_BOUNDS_RECORDED;
		bool measuresBounds = false;
		u32 initializeBoundsRoot = GRAPH_EFFECT_NO_BINDING;
		u32 updateBoundsRoot = GRAPH_EFFECT_NO_BINDING;
		u32 parent = GRAPH_EFFECT_NO_BINDING;
		u32 parentBinding = GRAPH_EFFECT_NO_BINDING;
		Vector<GraphEffectEventTarget> initializeEvents{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectEventTarget> updateEvents{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphEffectTiming {
		bool fixedDeltaTime = true;
		bool exactFixedTime = false;
		bool ignoreTimeScale = false;
		u32 prewarmStepCount = 0;
		f32 prewarmDeltaTime = 0.0f;
		u32 cullingFlags = GRAPH_EFFECT_CULL_DEFAULT;
	};

	struct GraphEffectInstancing {
		i32 mode = GRAPH_EFFECT_INSTANCING_AUTOMATIC;
		u32 capacity = GRAPH_EFFECT_INSTANCING_DEFAULT_CAPACITY;
		u32 refused = 0;
	};

	struct GraphEffect {
		GraphEffectTiming timing;
		GraphEffectInstancing instancing;
		Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectField> propertyFields{GetMemoryResource(MemTag::Editor)};
		u32 cameraOffset = 0;
		Vector<GraphEffectMesh> meshes{GetMemoryResource(MemTag::Editor)};
		u32 batchesOffset = 0;
		Vector<GraphEffectSpawner> spawners{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectSystem> systems{GetMemoryResource(MemTag::Editor)};
		Vector<str> events{GetMemoryResource(MemTag::Editor)};
		Vector<GraphEffectRelay> relays{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphEffectCompileOptions {
		bool recordBounds = false;
	};

	namespace GraphEffectTarget {

		// must match SltParticleEffect in ParticleSystem.hlsli
		inline constexpr u32 FRAME_HEADER_BYTES = 96;

		SHALLTEAR_GRAPHS_API bool Compile(const GraphDocument& document, std::string_view sourceName, GraphEffect& out,
										  Vector<GraphDiagnostic>& diagnostics, GraphDefinitions* definitions = nullptr,
										  const GraphEffectCompileOptions& options = {});
		SHALLTEAR_GRAPHS_API GraphEffectTiming Timing(const GraphDocument& document);
		SHALLTEAR_GRAPHS_API bool HasAutomaticBounds(const GraphDocument& document);
		SHALLTEAR_GRAPHS_API GraphEffectInstancing Instancing(const GraphDocument& document);

		SHALLTEAR_GRAPHS_API bool CheckSubgraph(const GraphDocument& document, const Guid& asset, Vector<GraphDiagnostic>& diagnostics,
												GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API std::span<const GraphBinding> Bindings();
		SHALLTEAR_GRAPHS_API void KernelBindings(GraphEffectKernel kernel, Vector<GraphBinding>& out, bool world = false, bool strip = false);

		SHALLTEAR_GRAPHS_API void PortTextureBindings(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphBinding>& out,
													  std::string_view unavailable = {});

		SHALLTEAR_GRAPHS_API Guid HeldTexture(const GraphNode& node, std::string_view port);

		SHALLTEAR_GRAPHS_API void PortMeshBindings(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphBinding>& out,
												   u32 first);

		SHALLTEAR_GRAPHS_API Guid HeldMesh(const GraphNode& node, std::string_view port);

	} // namespace GraphEffectTarget

	namespace GraphEffectRules {

		SHALLTEAR_GRAPHS_API bool MayFeed(std::string_view from, std::string_view to, std::string_view slot);
		SHALLTEAR_GRAPHS_API bool TakesOneSource(std::string_view context);
		SHALLTEAR_GRAPHS_API bool Holds(std::string_view context, std::string_view block);
		SHALLTEAR_GRAPHS_API bool HoldsBlock(std::string_view context, const GraphNode& block);
		SHALLTEAR_GRAPHS_API bool Suits(u32 suitable, std::string_view context);
		SHALLTEAR_GRAPHS_API u32 SuitableOf(const GraphNode& blockSubgraph);
		SHALLTEAR_GRAPHS_API void StandsFor(const GraphNode& context, Vector<std::string_view>& out);
		SHALLTEAR_GRAPHS_API bool HoldsIn(const GraphNode& context, const GraphNode& block);
		SHALLTEAR_GRAPHS_API bool IsContext(std::string_view def);
		SHALLTEAR_GRAPHS_API bool IsOutput(std::string_view def);
		SHALLTEAR_GRAPHS_API bool DrawsStrips(std::string_view def);
		SHALLTEAR_GRAPHS_API bool MakesStrips(const GraphNode& initialize);
		SHALLTEAR_GRAPHS_API u32 BoundsModeOf(const GraphNode& initialize);
		SHALLTEAR_GRAPHS_API bool ComputesBounds(const GraphNode& initialize);
		SHALLTEAR_GRAPHS_API bool IsEffectOnly(std::string_view def);
		SHALLTEAR_GRAPHS_API bool IgnoresError(const GraphNode& node, std::string_view key);
		SHALLTEAR_GRAPHS_API bool HasIgnoredErrors(const GraphNode& node);

	} // namespace GraphEffectRules

	namespace GraphEffectAssembly {

		SHALLTEAR_GRAPHS_API bool Emit(const GraphProgram& program, str& out, Vector<GraphDiagnostic>& diagnostics, std::string_view system = {});

	} // namespace GraphEffectAssembly

	namespace GraphEffectSplice {

		SHALLTEAR_GRAPHS_API bool Append(GraphProgram& into, const GraphProgram& block, const HashMap<str, GraphValueId>& current,
										 Vector<GraphValueId>& roots);

	} // namespace GraphEffectSplice

} // namespace Shalltear
