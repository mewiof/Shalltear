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

	inline constexpr std::string_view GraphEffectDocumentType = "EffectGraph";
	inline constexpr std::string_view GraphEffectExtension = ".sleffect";

	inline constexpr std::string_view GraphEffectOperatorSubgraphDocumentType = "VisualEffectSubgraphOperator";
	inline constexpr std::string_view GraphEffectOperatorSubgraphExtension = ".sleffectop";
	inline constexpr std::string_view GraphEffectBlockSubgraphDocumentType = "VisualEffectSubgraphBlock";
	inline constexpr std::string_view GraphEffectBlockSubgraphExtension = ".sleffectblock";
	inline constexpr std::string_view GraphEffectBlockSubgraphContext = "Context/BlockSubgraph";
	inline constexpr std::string_view GraphEffectSubgraphBlock = "Block/Subgraph";
	inline constexpr std::string_view GraphEffectSystemSubgraph = "Context/Subgraph";
	inline constexpr std::string_view GraphEffectSuitableContextsSetting = "Suitable Contexts";
	inline constexpr std::string_view GraphEffectSuitableContexts[] = {"Spawner", "Init", "Update", "Output", "InitAndUpdate",
																	   "InitAndUpdateAndOutput", "UpdateAndOutput"};
	inline constexpr u32 GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS = 5;
	inline constexpr std::string_view GraphEffectOperatorSubgraphCategory = "Subgraph Operator";
	inline constexpr std::string_view GraphEffectBlockSubgraphCategory = "Subgraph Block";
	SHALLTEAR_GRAPHS_API bool IsEffectDocumentType(std::string_view type);

	inline constexpr std::string_view GraphEffectEvent = "Context/Event";
	inline constexpr std::string_view GraphEffectSpawn = "Context/Spawn";
	inline constexpr std::string_view GraphEffectGPUEvent = "Context/GPUEvent";
	inline constexpr std::string_view GraphEffectOutputEventDefinition = "Context/OutputEvent";
	inline constexpr std::string_view GraphEffectDefaultOutputEventName = "On Received Event";
	inline constexpr std::string_view GraphEffectInitialize = "Context/Initialize";
	inline constexpr std::string_view GraphEffectUpdate = "Context/Update";
	inline constexpr std::string_view GraphEffectOutputDefinition = "Context/OutputParticlePrimitive";
	inline constexpr std::string_view GraphEffectOutputMesh = "Context/OutputParticleMesh";
	inline constexpr std::string_view GraphEffectOutputPoint = "Context/OutputParticlePoint";
	inline constexpr std::string_view GraphEffectOutputLine = "Context/OutputParticleLine";
	inline constexpr std::string_view GraphEffectOutputLit = "Context/OutputParticleLitPrimitive";
	inline constexpr std::string_view GraphEffectOutputLitMesh = "Context/OutputParticleLitMesh";
	inline constexpr std::string_view GraphEffectOutputNodeShader = "Context/OutputParticleNodeShaderPrimitive";
	inline constexpr std::string_view GraphEffectOutputNodeShaderMesh = "Context/OutputParticleNodeShaderMesh";
	inline constexpr std::string_view GraphEffectOutputStrip = "Context/OutputParticleStripQuad";
	inline constexpr std::string_view GraphEffectOutputNodeShaderStrip = "Context/OutputParticleNodeShaderStripQuad";
	inline constexpr std::string_view GraphEffectStripTilingModes[] = {"Stretch", "RepeatPerSegment", "Custom"};
	inline constexpr std::string_view GraphEffectDataTypes[] = {"Particle", "ParticleStrip"};
	inline constexpr std::string_view GraphEffectSimulationTarget = "Simulation Target";
	inline constexpr std::string_view GraphEffectSimulationTargets[] = {"GPU", "CPU"};
	inline constexpr std::string_view GraphEffectSystemName = "System Name";
	inline constexpr std::string_view GraphEffectDefaultSystemName = "System";
	inline constexpr std::string_view GraphEffectFixedDeltaTime = "fixedDeltaTime";
	inline constexpr std::string_view GraphEffectExactFixedTime = "exactFixedTime";
	inline constexpr std::string_view GraphEffectIgnoreTimeScale = "ignoreTimeScale";
	inline constexpr std::string_view GraphEffectPrewarmStepCount = "preWarmStepCount";
	inline constexpr std::string_view GraphEffectPrewarmDeltaTime = "preWarmDeltaTime";
	inline constexpr u32 GRAPH_EFFECT_PREWARM_MAX_STEPS = 2400;
	inline constexpr f32 GRAPH_EFFECT_PREWARM_MIN_DELTA_TIME = 1.0f / 800.0f;
	inline constexpr std::string_view GraphEffectCullingFlags = "cullingFlags";
	inline constexpr u32 GRAPH_EFFECT_CULL_SIMULATION = 1;
	inline constexpr u32 GRAPH_EFFECT_CULL_BOUNDS_UPDATE = 2;
	inline constexpr u32 GRAPH_EFFECT_CULL_DEFAULT = GRAPH_EFFECT_CULL_SIMULATION | GRAPH_EFFECT_CULL_BOUNDS_UPDATE;
	inline constexpr std::string_view GraphEffectInstancingMode = "instancingMode";
	inline constexpr std::string_view GraphEffectInstancingCapacity = "instancingCapacity";
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_DISABLED = -1;
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_AUTOMATIC = 0;
	inline constexpr i32 GRAPH_EFFECT_INSTANCING_CUSTOM = 1;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_DEFAULT_CAPACITY = 64;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_MAX_CAPACITY = 65535;
	inline constexpr u32 GRAPH_EFFECT_INSTANCING_GPU_EVENT = 1u << 2;
	inline constexpr std::string_view GraphEffectBoundsSettingMode = "Bounds Setting Mode";
	inline constexpr std::string_view GraphEffectBoundsModes[] = {"Recorded", "Manual", "Automatic"};
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_RECORDED = 0;
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_MANUAL = 1;
	inline constexpr u32 GRAPH_EFFECT_BOUNDS_AUTOMATIC = 2;
	inline constexpr std::string_view GraphEffectBoundsCenter = "Bounds Center";
	inline constexpr std::string_view GraphEffectBoundsSize = "Bounds Size";
	inline constexpr std::string_view GraphEffectBoundsPadding = "Bounds Padding";
	inline constexpr std::string_view GraphEffectNodeShaderSetting = "Node Shader";
	inline constexpr std::string_view GraphEffectNodeShaderTextureBindings[] = {"Output/NodeShaderTexture2D", "Output/NodeShaderTexture2DArray",
																				"Output/NodeShaderTexture3D", "Output/NodeShaderTextureCube"};
	inline constexpr std::string_view GraphEffectPrimitives[] = {"Triangle", "Quad", "Octagon"};
	inline constexpr std::string_view GraphEffectColorModes[] = {"None", "BaseColor", "Emissive", "BaseColorAndEmissive"};
	inline constexpr std::string_view GraphEffectSmoothnessSources[] = {"None", "MetallicAlpha", "AlbedoAlpha"};
	inline constexpr std::string_view GraphEffectLitMapPorts[] = {"Occlusion Map", "Metallic Map", "Normal Map", "Emissive Map"};
	inline constexpr std::string_view GraphEffectLitMapSlots[] = {"occlusionMap", "metallicMap", "normalMap", "emissiveMap"};
	inline constexpr std::string_view GraphEffectLitMapBindings[] = {"Output/OcclusionMap", "Output/MetallicMap", "Output/NormalMap",
																	 "Output/EmissiveMap"};
	inline constexpr std::string_view GraphEffectSorts[] = {"Auto", "Off", "On"};
	inline constexpr std::string_view GraphEffectSortModes[] = {"DistanceToCamera", "YoungestInFront", "CameraDepth", "Custom"};
	inline constexpr std::string_view GraphEffectCullMode = "Cull Mode";
	inline constexpr std::string_view GraphEffectCullModes[] = {"Default", "Front", "Back", "Off"};
	inline constexpr std::string_view GraphEffectZWriteMode = "Z Write Mode";
	inline constexpr std::string_view GraphEffectZWriteModes[] = {"Default", "Off", "On"};
	inline constexpr std::string_view GraphEffectZTestMode = "Z Test Mode";
	inline constexpr std::string_view GraphEffectZTestModes[] = {"Default", "Less", "Greater", "LEqual", "GEqual", "Equal", "NotEqual", "Always"};

	inline constexpr std::string_view GraphEffectConstantRate = "Block/ConstantSpawnRate";
	inline constexpr std::string_view GraphEffectBurst = "Block/Burst";
	inline constexpr std::string_view GraphEffectSetAttribute = "Block/SetAttribute";
	inline constexpr std::string_view GraphEffectSetSpawnEvent = "Block/SetSpawnEvent";
	inline constexpr std::string_view GraphEffectAttributeFromCurve = "Block/AttributeFromCurve";
	inline constexpr std::string_view GraphEffectPositionShape = "Block/PositionShape";
	inline constexpr std::string_view GraphEffectForce = "Block/Force";
	inline constexpr std::string_view GraphEffectGravity = "Block/Gravity";
	inline constexpr std::string_view GraphEffectLinearDrag = "Block/LinearDrag";
	inline constexpr std::string_view GraphEffectTurbulence = "Block/Turbulence";
	inline constexpr std::string_view GraphEffectConformToSphere = "Block/ConformToSphere";
	inline constexpr std::string_view GraphEffectCollisionShape = "Block/CollisionShape";
	inline constexpr std::string_view GraphEffectOrient = "Block/Orient";
	inline constexpr std::string_view GraphEffectFlipbookPlayer = "Block/FlipbookPlayer";
	inline constexpr std::string_view GraphEffectVectorFieldForce = "Block/VectorFieldForce";
	inline constexpr std::string_view GraphEffectConformToSDF = "Block/ConformToSignedDistanceField";
	inline constexpr std::string_view GraphEffectCollisionDepth = "Block/CollisionDepth";
	inline constexpr std::string_view GraphEffectPositionDepth = "Block/PositionDepth";
	inline constexpr std::string_view GraphEffectPositionMesh = "Block/PositionMesh";
	inline constexpr std::string_view GraphEffectPositionSequential = "Block/PositionSequential";
	inline constexpr std::string_view GraphEffectCameraFade = "Block/CameraFade";
	inline constexpr std::string_view GraphEffectIncrementStripIndex = "Block/IncrementStripIndexOnStart";
	inline constexpr std::string_view GraphEffectTriggerEvent = "Block/TriggerEvent";
	inline constexpr std::string_view GraphEffectTriggerModes[] = {"Always", "OverTime", "OverDistance", "OnDie", "OnCollide"};
	inline constexpr std::string_view GraphEffectEvt = "Evt";
	inline constexpr std::string_view GraphEffectImplicitReap = "Trigger/ImplicitReap";
	inline constexpr std::string_view GraphEffectRateCount = "rateCount";

	inline constexpr std::string_view GraphEffectGetAttribute = "Attribute/GetAttribute";
	inline constexpr std::string_view GraphEffectAgeOverLifetime = "Attribute/AgeOverLifetime";
	inline constexpr std::string_view GraphEffectRatioOverStrip = "Attribute/RatioOverStrip";
	inline constexpr std::string_view GraphEffectRandomNumber = "Random/RandomNumber";
	inline constexpr std::string_view GraphEffectTotalTime = "BuiltIn/TotalTime";
	inline constexpr std::string_view GraphEffectDeltaTime = "BuiltIn/DeltaTime";
	inline constexpr std::string_view GraphEffectSampleCurve = "Sampling/SampleCurve";
	inline constexpr std::string_view GraphEffectNoiseDefinition = "Noise/Noise";
	inline constexpr std::string_view GraphEffectCurlNoise = "Noise/CurlNoise";
	inline constexpr std::string_view GraphEffectNoiseTypes[] = {"Value", "Perlin", "Cellular"};
	inline constexpr std::string_view GraphEffectNoiseDimensions[] = {"One", "Two", "Three"};
	inline constexpr std::string_view GraphEffectCurlDimensions[] = {"Two", "Three"};
	inline constexpr std::string_view GraphEffectSampleTexture2D = "Sampling/SampleTexture2D";
	inline constexpr std::string_view GraphEffectSampleTexture2DArray = "Sampling/SampleTexture2DArray";
	inline constexpr std::string_view GraphEffectSampleTexture3D = "Sampling/SampleTexture3D";
	inline constexpr std::string_view GraphEffectSampleTextureCube = "Sampling/SampleTextureCube";
	inline constexpr std::string_view GraphEffectSampleSDF = "Sampling/SampleSignedDistanceField";
	inline constexpr std::string_view GraphEffectSampleMesh = "Sampling/SampleMesh";
	inline constexpr std::string_view GraphEffectSampleMeshIndex = "Sampling/SampleMeshIndex";
	inline constexpr std::string_view GraphEffectMeshVertexCount = "Sampling/MeshVertexCount";
	inline constexpr std::string_view GraphEffectMeshIndexCount = "Sampling/MeshIndexCount";
	inline constexpr std::string_view GraphEffectMeshTriangleCount = "Sampling/MeshTriangleCount";

	inline constexpr std::string_view GraphEffectStart = "Start";
	inline constexpr std::string_view GraphEffectStop = "Stop";
	inline constexpr std::string_view GraphEffectIn = "In";

	inline constexpr std::string_view GraphEffectOnPlay = "OnPlay";
	inline constexpr std::string_view GraphEffectOnStop = "OnStop";

	inline constexpr std::string_view GraphEffectAttributeSetting = "Attribute";
	inline constexpr std::string_view GraphEffectCompositions[] = {"Set", "Add", "Multiply", "Blend"};
	inline constexpr std::string_view GraphEffectSources[] = {"Port", "Source"};
	inline constexpr std::string_view GraphEffectRandoms[] = {"Off", "PerComponent", "Uniform"};
	inline constexpr std::string_view GraphEffectLocations[] = {"Current", "Source"};
	inline constexpr std::string_view GraphEffectSampleModes[] = {"OverLife", "BySpeed", "Random", "RandomConstantPerParticle", "Custom"};
	inline constexpr std::string_view GraphEffectSampleModeNames[] = {"Over Life", "By Speed", "Random from Curve", "Random Constant/Particle",
																	  "Custom"};

	inline constexpr std::string_view GraphEffectRandomParticle = "RandomKey/Particle";
	inline constexpr std::string_view GraphEffectRandomParticleConstant = "RandomKey/ParticleConstant";
	inline constexpr std::string_view GraphEffectRandomEffect = "RandomKey/Effect";
	inline constexpr std::string_view GraphEffectRandomEffectConstant = "RandomKey/EffectConstant";

	inline constexpr std::string_view GraphEffectCameraRight = "Camera/Right";
	inline constexpr std::string_view GraphEffectCameraUp = "Camera/Up";
	inline constexpr std::string_view GraphEffectCameraForward = "Camera/Forward";
	inline constexpr std::string_view GraphEffectCameraPosition = "Camera/Position";
	inline constexpr std::string_view GraphEffectCameraOrthographic = "Camera/Orthographic";
	inline constexpr std::string_view GraphEffectCameraClipW = "Camera/ClipW";
	inline constexpr std::string_view GraphEffectShadowPass = "Pass/Shadow";

	inline constexpr std::string_view GraphEffectStripPrevious = "Strip/Previous";
	inline constexpr std::string_view GraphEffectStripNext = "Strip/Next";

	inline constexpr std::string_view GraphEffectFlipbookSize = "Flipbook/Size";
	inline constexpr std::string_view GraphEffectFlipbookBlending = "Flipbook/Blending";

	inline constexpr std::string_view GraphEffectCameraDepth = "CameraDepth/Texture";
	inline constexpr std::string_view GraphEffectCameraDepthSize = "CameraDepth/Size";
	inline constexpr std::string_view GraphEffectCameraDepthMatrices[] = {"CameraDepth/EffectToView", "CameraDepth/ViewToEffect", "CameraDepth/ViewToClip",
																		  "CameraDepth/ClipToView"};
	inline constexpr u32 GRAPH_EFFECT_CAMERA_RECORD_BYTES = 4 * 64 + 16;
	inline constexpr u32 GRAPH_EFFECT_NO_BINDING = ~0u;

	inline constexpr u32 GRAPH_EFFECT_MESH_RECORD_BYTES = 32;
	inline constexpr std::string_view GraphEffectMeshVertexCountInput = "/VertexCount";
	inline constexpr std::string_view GraphEffectMeshIndexCountInput = "/IndexCount";
	inline constexpr u32 GRAPH_EFFECT_MESH_CHANNEL_BITS = 3;

	inline constexpr std::string_view GraphEffectMainTexture = "mainTexture";
	inline constexpr std::string_view GraphEffectMainTextureBinding = "Output/MainTexture";

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
		inline constexpr std::string_view DefaultName = "EffectDefault";

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
