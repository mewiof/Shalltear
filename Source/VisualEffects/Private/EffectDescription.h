#pragma once

#include "EffectProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class CookReader;
	class CookWriter;

	namespace VisualEffectsImpl {

		enum class EffectPropertyType : u8 {
			Float,
			Integer,
			Boolean,
			Vector2,
			Vector3,
			Vector4,
			Color
		};

		struct EffectProperty {
			str name;
			str reference;
			EffectPropertyType type = EffectPropertyType::Float;
			u32 offset = 0;
			u32 components = 1;
			f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		};

		enum class EffectSpawnKind : u8 {
			ConstantRate,
			SingleBurst,
			PeriodicBurst,
			SetAttribute,
			IncrementStripIndex
		};

		struct EffectSpawnBlock {
			EffectSpawnKind kind = EffectSpawnKind::ConstantRate;
			EffectProgram program;
			str attribute;
			u32 components = 0;
		};

		struct EffectSpawner {
			Vector<EffectSpawnBlock> blocks{GetMemoryResource(MemTag::Asset)};
			EffectProgram loop;
			Vector<str> start{GetMemoryResource(MemTag::Asset)};
			Vector<str> stop{GetMemoryResource(MemTag::Asset)};
			Vector<str> outputEvents{GetMemoryResource(MemTag::Asset)};
		};

		struct EffectRelay {
			str event;
			str output;
		};

		struct EffectStored {
			str attribute;
			u32 slot = 0;
			u32 components = 1;
		};

		struct EffectField {
			str name;
			u32 offset = 0;
			u32 components = 1;
			f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		};

		inline constexpr u32 EFFECT_NO_BINDING = ~0u;
		inline constexpr u32 EFFECT_CAMERA_RECORD_BYTES = 4 * 64 + 16;
		inline constexpr u32 EFFECT_MESH_RECORD_BYTES = 32;
		inline constexpr std::string_view EFFECT_MESH_VERTEX_COUNT_INPUT = "/VertexCount";
		inline constexpr std::string_view EFFECT_MESH_INDEX_COUNT_INPUT = "/IndexCount";

		struct EffectMesh {
			str binding;
			u32 offset = 0;
			Guid mesh;
		};

		struct EffectKernelTexture {
			u32 binding = 0;
			u32 samplerBinding = 0;
			Guid texture;
		};

		struct EffectOutputTexture {
			str slot;
			Guid texture;
		};

		enum class EffectSort : u8 {
			None,
			DistanceToCamera,
			YoungestInFront,
			CameraDepth,
			Custom
		};

		struct EffectOutput {
			u32 shader = 0;
			u32 vertices = 6;
			bool instanced = false;
			u32 mesh = EFFECT_NO_BINDING;
			Vector<EffectOutputTexture> textures{GetMemoryResource(MemTag::Asset)};
			EffectSort sort = EffectSort::None;
			bool revertSorting = false;
			u32 sortKernel = EFFECT_NO_BINDING;
			bool castShadows = false;
			bool strip = false;
		};

		struct EffectEventTarget {
			u32 child = 0;
			u32 lists = 0;
			u32 arguments = 0;
		};

		enum class EffectBoundsMode : u8 {
			Recorded,
			Manual,
			Automatic
		};

		struct EffectOutputEvent {
			str name;
			u32 root = 0;
		};

		struct EffectCarried {
			str attribute;
			u32 root = 0;
			u32 components = 1;
		};

		struct EffectSystem {
			str name;
			u32 capacity = 0;
			u32 strips = 0;
			bool world = false;
			u32 attributeCount = 0;
			Vector<EffectStored> stored{GetMemoryResource(MemTag::Asset)};
			Vector<EffectField> source{GetMemoryResource(MemTag::Asset)};
			u32 batchStride = 4;
			u32 initialize = 0;
			u32 update = 0;
			Vector<EffectKernelTexture> initializeTextures{GetMemoryResource(MemTag::Asset)};
			Vector<EffectKernelTexture> updateTextures{GetMemoryResource(MemTag::Asset)};
			u32 initializeDepthBinding = EFFECT_NO_BINDING;
			u32 updateDepthBinding = EFFECT_NO_BINDING;
			Vector<EffectOutput> outputs{GetMemoryResource(MemTag::Asset)};
			Vector<u32> spawners{GetMemoryResource(MemTag::Asset)};
			Vector<str> events{GetMemoryResource(MemTag::Asset)};
			EffectProgram bounds;
			bool boundsWorld = false;
			EffectBoundsMode boundsMode = EffectBoundsMode::Recorded;
			bool measuresBounds = false;
			u32 initializeBoundsRoot = EFFECT_NO_BINDING;
			u32 updateBoundsRoot = EFFECT_NO_BINDING;
			u32 parent = EFFECT_NO_BINDING;
			u32 parentBinding = EFFECT_NO_BINDING;
			Vector<EffectEventTarget> initializeEvents{GetMemoryResource(MemTag::Asset)};
			Vector<EffectEventTarget> updateEvents{GetMemoryResource(MemTag::Asset)};
			bool cpu = false;
			EffectProgram initializeProgram;
			EffectProgram updateProgram;
			Vector<EffectOutputEvent> initializeOutputEvents{GetMemoryResource(MemTag::Asset)};
			Vector<EffectOutputEvent> updateOutputEvents{GetMemoryResource(MemTag::Asset)};
			Vector<EffectCarried> initializeCarried{GetMemoryResource(MemTag::Asset)};
			Vector<EffectCarried> updateCarried{GetMemoryResource(MemTag::Asset)};
		};

		struct EffectTiming {
			bool fixedDeltaTime = true;
			bool exactFixedTime = false;
			bool ignoreTimeScale = false;
			u32 prewarmStepCount = 0;
			f32 prewarmDeltaTime = 0.0f;
			bool cullSimulation = true;
			bool cullBoundsUpdate = true;
		};

		inline constexpr i32 EFFECT_INSTANCING_DISABLED = -1;
		inline constexpr i32 EFFECT_INSTANCING_AUTOMATIC = 0;
		inline constexpr i32 EFFECT_INSTANCING_CUSTOM = 1;
		struct EffectInstancing {
			i32 mode = EFFECT_INSTANCING_AUTOMATIC;
			u32 capacity = 64;
		};

		struct EffectDescription {
			EffectTiming timing;
			EffectInstancing instancing;
			Vector<EffectProperty> properties{GetMemoryResource(MemTag::Asset)};
			u32 cameraOffset = 0;
			Vector<EffectMesh> meshes{GetMemoryResource(MemTag::Asset)};
			u32 batchesOffset = 0;
			Vector<EffectSpawner> spawners{GetMemoryResource(MemTag::Asset)};
			Vector<EffectSystem> systems{GetMemoryResource(MemTag::Asset)};
			Vector<str> events{GetMemoryResource(MemTag::Asset)};
			Vector<EffectRelay> relays{GetMemoryResource(MemTag::Asset)};
		};

		inline constexpr u32 EFFECT_SLOT_TIME = 0;
		inline constexpr u32 EFFECT_SLOT_SMOOTH_DELTA = 1;
		inline constexpr u32 EFFECT_SLOT_RANDOM = 2;
		inline constexpr u32 EFFECT_SLOT_CONSTANT_RANDOM = 3;
		inline constexpr u32 EFFECT_SLOT_PROPERTIES = 4;

		inline u32 EffectMeshSlot(const EffectDescription& description, u32 mesh, bool indices) {
			return EFFECT_SLOT_PROPERTIES + static_cast<u32>(description.properties.size()) + 2 * mesh + (indices ? 1u : 0u);
		}
		inline u32 EffectSlotCount(const EffectDescription& description) {
			return EFFECT_SLOT_PROPERTIES + static_cast<u32>(description.properties.size() + 2 * description.meshes.size());
		}

		inline u32 EffectHash(u32 seed) {
			seed = (seed ^ 61u) ^ (seed >> 16);
			seed += seed << 3;
			seed = seed ^ (seed >> 4);
			seed *= 0x27d4eb2du;
			return seed ^ (seed >> 15);
		}

		inline constexpr u32 EFFECT_PARTICLE_SLOT_RANDOM = 0;
		inline constexpr u32 EFFECT_PARTICLE_SLOT_ROWS = 4;
		inline constexpr u32 EFFECT_PARTICLE_SLOT_ID = 7;
		inline constexpr u32 EFFECT_PARTICLE_SLOT_SPAWN_INDEX = 8;
		inline constexpr u32 EFFECT_PARTICLE_SLOT_SPAWN_COUNT = 9;
		inline constexpr u32 EFFECT_PARTICLE_SLOT_STORED = 10;

		inline u32 EffectParticleSlot(const EffectDescription& description, u32 slot) { return EffectSlotCount(description) + slot; }
		inline u32 EffectParticleSlotCount(const EffectDescription& description, const EffectSystem& system) {
			return EffectParticleSlot(description, EFFECT_PARTICLE_SLOT_STORED + static_cast<u32>(system.stored.size() + system.source.size()));
		}

		namespace EffectDescriptions {

			bool Bind(EffectDescription& description, str& error);

			bool Validate(const EffectDescription& description, u32 shaderCount, str& error);

			void Cook(const EffectDescription& description, CookWriter& out);
			bool LoadCooked(CookReader& in, EffectDescription& out);

		} // namespace EffectDescriptions

		enum class EffectLoopPhase : u8 {
			Finished,
			DelayingBeforeLoop,
			Looping,
			DelayingAfterLoop
		};

		struct EffectSpawnerState {
			bool playing = false;
			EffectLoopPhase phase = EffectLoopPhase::Finished;
			f32 phaseTime = 0.0f;
			f32 time = 0.0f;
			f32 carry = 0.0f;
			u32 loopIndex = 0;
			f32 loopCount = -1.0f;
			f32 loopDuration = -1.0f;
			f32 delayBeforeLoop = 0.0f;
			f32 delayAfterLoop = 0.0f;
			Vector<u8> fired{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> next{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectValue> values{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> stripIndices{GetMemoryResource(MemTag::Graphics)};
		};

		namespace EffectSpawners {

			void Start(const EffectSpawner& spawner, EffectSpawnerState& state, std::span<const EffectValue> slots, EffectMachine& machine,
					   std::span<const EffectMeshView> meshes = {});
			void Stop(EffectSpawnerState& state);

			u32 Advance(const EffectSpawner& spawner, EffectSpawnerState& state, f32 deltaTime, std::span<const EffectValue> slots,
						EffectMachine& machine, std::span<const EffectMeshView> meshes = {});

			bool FindAttribute(const EffectSpawner& spawner, const EffectSpawnerState& state, std::string_view attribute, EffectValue& out);

		} // namespace EffectSpawners

	} // namespace VisualEffectsImpl

} // namespace Shalltear
