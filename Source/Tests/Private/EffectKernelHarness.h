#pragma once

#ifndef SHALLTEAR_SHARED

#include "GraphEffect.h"
#include "ParticleSimulation.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>

SLT_DECLARE_LINKED_PACKAGE(VisualEffects)

namespace Shalltear::EffectKernelHarness {

	using VisualEffectsImpl::ParticleSystem;
	using VisualEffectsImpl::ParticleSystemDesc;

	inline std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			if (probe.parent_path() == probe) {
				break;
			}
			probe = probe.parent_path();
		}
		return {};
	}

	inline GraphDocument ParseEffect(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	class GraphEffectKernelFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		char _token = 0;
		std::filesystem::path _root;
		ShaderAsset _initialize;
		ShaderAsset _update;
		ShaderAsset _output;

		void SetUp() override {
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			static std::atomic<u32> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_effect_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
					 std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
		}

		void TearDown() override {
			VisualEffectsImpl::ShutdownSimulation();
			FramePasses::UnregisterOwnedBy(&_token);
			FrameStages::UnregisterOwnedBy(&_token);
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool Import(ShaderAsset& shader, const str& text, const char* name) {
			const std::filesystem::path file = _root / name;
			std::ofstream(file, std::ios::binary) << text;
			return shader.Import(Path(file.generic_string()));
		}

		static Vector<u8> Frame(const GraphEffect& effect, f32 time, f32 delta, u32 spawn, u32 seed = 0, u32 idBase = 0) {
			const u32 one[] = {spawn};
			return Frame(effect, time, delta, spawn > 0 ? std::span<const u32>(one) : std::span<const u32>(), seed, idBase);
		}

		static Vector<u8> Frame(const GraphEffect& effect, f32 time, f32 delta, std::span<const u32> batches, u32 seed, u32 idBase,
								std::span<const f32> localToWorld = {}) {
			const u32 stride = effect.systems[0].batchStride;
			Vector<u8> bytes{GetMemoryResource(MemTag::Graphics)};
			bytes.resize(effect.batchesOffset + std::max<size_t>(batches.size(), 1) * stride, 0);
			const f32 clock[4] = {time, std::sin(time), std::cos(time), delta};
			std::memcpy(bytes.data(), clock, sizeof(clock));
			const u32 count = static_cast<u32>(batches.size());
			std::memcpy(bytes.data() + 20, &count, 4);
			std::memcpy(bytes.data() + 24, &idBase, 4);
			std::memcpy(bytes.data() + 28, &seed, 4);
			f32 matrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
			if (localToWorld.size() == 12) {
				std::memcpy(matrix, localToWorld.data(), 12 * sizeof(f32));
			}
			std::memcpy(bytes.data() + 32, matrix, sizeof(matrix));
			for (u32 i = 0; i < count; ++i) {
				std::memcpy(bytes.data() + effect.batchesOffset + i * stride, &batches[i], 4);
			}
			return bytes;
		}

		static void Step(ParticleSystem& system, const GraphEffect& effect, f32 time, f32 delta, u32 spawn) {
			const Vector<u8> frame = Frame(effect, time, delta, spawn);
			system.SetFrameData(frame);
			system.Advance(delta, spawn);
			RenderSystem::RenderAll();
		}

		static u32 Alive(const ParticleSystem& system, Vector<u32>& indices) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			u32 counts[4] = {};
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(counts), sizeof(counts)}, system.GetListsOffset());
			const u32 alive = counts[system.GetParity()];
			indices.assign(alive, 0);
			if (alive > 0) {
				const u64 offset = system.GetListsOffset() + (4ull + static_cast<u64>(system.GetParity()) * system.GetCapacity()) * 4;
				device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(indices.data()), indices.size() * 4}, offset);
			}
			return alive;
		}

		static f32 Attribute(const ParticleSystem& system, u32 slot, u32 index) {
			f32 value = 0.0f;
			RenderSystem::GetDevice()->ReadbackBuffer(system.GetAttributesBuffer(), {reinterpret_cast<u8*>(&value), 4},
													  system.GetAttributesOffset() + (static_cast<u64>(slot) * system.GetCapacity() + index) * 4);
			return value;
		}

		struct StripWords {
			u32 first = 0;
			u32 count = 0;
			u32 began = 0;
			u32 least = 0;
			u32 most = 0;
			u32 held = 0;
		};
		static StripWords Strip(const ParticleSystem& system, u32 strip) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			StripWords words;
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(&words), sizeof(words)},
								  system.GetListsOffset() + (4ull + 3ull * system.GetCapacity() + 6ull * strip) * 4);
			return words;
		}

		static u32 Dropped(const ParticleSystem& system) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			u32 counts[4] = {};
			device.ReadbackBuffer(system.GetListsBuffer(), {reinterpret_cast<u8*>(counts), sizeof(counts)}, system.GetListsOffset());
			return counts[3];
		}
	};

	inline str EffectWith(std::string_view nodes, std::string_view initialize, std::string_view update) {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
						   nodes, initialize, update);
	}

	struct BlockRun {
		GraphEffect effect;
		ShaderAsset initialize;
		ShaderAsset update;
		ParticleSystem system;
	};

	class LoadableTexture final : public TextureAsset {
	public:
		using TextureAsset::OnLoad;
		using TextureAsset::OnUnload;
		~LoadableTexture() override { OnUnload(); }

		template <typename Field>
		void Volume(u32 side, Field&& field, bool linear = true) {
			settings.shape = TextureShape::Texture3D;
			settings.filter = linear ? RHI::FilterMode::Linear : RHI::FilterMode::Nearest;
			settings.addressMode = RHI::AddressMode::ClampToEdge;
			settings.generateMips = false;
			settings.sRGB = false;
			size = Vec2I(static_cast<i32>(side), static_cast<i32>(side));
			slices = side;
			format = RHI::Format::RGBA16Float;
			pixels.resize(static_cast<size_t>(side) * side * side * 8);
			u16* halves = reinterpret_cast<u16*>(pixels.data());
			size_t at = 0;
			for (u32 z = 0; z < side; ++z) {
				for (u32 y = 0; y < side; ++y) {
					for (u32 x = 0; x < side; ++x) {
						const auto center = [side](u32 i) { return (static_cast<f32>(i) + 0.5f) / static_cast<f32>(side) - 0.5f; };
						const std::array<f32, 4> texel = field(center(x), center(y), center(z));
						for (f32 value : texel) {
							halves[at++] = Math::FloatToHalf(value);
						}
					}
				}
			}
			OnLoad();
		}

		void Image(u32 width, u32 height, std::span<const u8> rgba, u32 layers = 0) {
			settings.shape = layers > 0 ? TextureShape::Texture2DArray : TextureShape::Texture2D;
			settings.filter = RHI::FilterMode::Nearest;
			settings.addressMode = RHI::AddressMode::ClampToEdge;
			settings.generateMips = false;
			settings.sRGB = false;
			size = Vec2I(static_cast<i32>(width), static_cast<i32>(height));
			slices = std::max(layers, 1u);
			format = RHI::Format::RGBA8Unorm;
			pixels.assign(rgba.begin(), rgba.end());
			OnLoad();
		}
	};

	struct HeldTexture {
		Guid guid;
		TextureAsset* texture = nullptr;
	};

	class LoadableMesh final : public MeshAsset {
	public:
		using MeshAsset::OnLoad;
		using MeshAsset::OnUnload;
		~LoadableMesh() override { OnUnload(); }
	};

	struct StagedMesh {
		Guid guid;
		MeshAsset* mesh = nullptr;
	};

	class GraphEffectBlockFixture : public GraphEffectKernelFixture {
	protected:
		bool Begin(BlockRun& run, const str& text, std::string_view name, std::span<const HeldTexture> textures = {}, std::span<const StagedMesh> meshes = {},
				   const GraphEffectCompileOptions& options = {}) {
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			if (!GraphEffectTarget::Compile(ParseEffect(text), name, run.effect, diagnostics, nullptr, options)) {
				ADD_FAILURE() << name << " doesn't compile: " << (diagnostics.empty() ? str() : diagnostics[0].message);
				return false;
			}
			const GraphEffectSystem& compiled = run.effect.systems[0];
			if (!Import(run.initialize, compiled.initializeKernel, (str(name) + ".Initialize.hlsl").c_str()) ||
				!Import(run.update, compiled.updateKernel, (str(name) + ".Update.hlsl").c_str())) {
				return false;
			}
			ParticleSystemDesc desc;
			desc.capacity = compiled.capacity;
			desc.strips = compiled.strips;
			desc.attributeCount = compiled.attributeCount;
			desc.initialize = Handle<ShaderAsset>(&run.initialize);
			desc.update = Handle<ShaderAsset>(&run.update);
			const auto bind = [&](const Vector<GraphEffectKernelTexture>& sampled, Vector<ComputeTexture>& out) {
				for (const GraphEffectKernelTexture& slot : sampled) {
					Handle<TextureAsset> held;
					for (const HeldTexture& texture : textures) {
						held = texture.guid == slot.texture ? Handle<TextureAsset>(texture.texture) : held;
					}
					out.push_back({slot.binding, slot.samplerBinding, held});
				}
			};
			bind(compiled.initializeTextures, desc.initializeTextures);
			bind(compiled.updateTextures, desc.updateTextures);
			desc.initializeDepth = compiled.initializeDepthBinding;
			desc.updateDepth = compiled.updateDepthBinding;
			desc.cameraOffset = run.effect.cameraOffset;
			for (const GraphEffectMesh& slot : run.effect.meshes) {
				Handle<MeshAsset> held;
				for (const StagedMesh& mesh : meshes) {
					held = mesh.guid == slot.mesh ? Handle<MeshAsset>(mesh.mesh) : held;
				}
				desc.meshes.push_back({slot.offset, held});
			}
			desc.world = compiled.world;
			desc.measuresBounds = compiled.measuresBounds;
			return run.system.Create(desc);
		}

		static void Advance(BlockRun& run, f32 time, f32 delta, std::initializer_list<u32> batches, u32 seed = 0, u32 idBase = 0,
							std::span<const f32> localToWorld = {}) {
			u32 spawn = 0;
			for (u32 count : batches) {
				spawn += count;
			}
			const Vector<u8> frame = Frame(run.effect, time, delta, {batches.begin(), batches.size()}, seed, idBase, localToWorld);
			run.system.SetFrameData(frame);
			run.system.Advance(delta, spawn);
			RenderSystem::RenderAll();
		}

		static Vector<f32> Values(const BlockRun& run, std::string_view attribute, u32 component = 0) {
			Vector<f32> out{GetMemoryResource(MemTag::Graphics)};
			for (const GraphEffectStored& stored : run.effect.systems[0].stored) {
				if (stored.attribute != attribute) {
					continue;
				}
				Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
				GraphEffectKernelFixture::Alive(run.system, indices);
				for (u32 index : indices) {
					out.push_back(Attribute(run.system, stored.slot + component, index));
				}
			}
			return out;
		}

		static Vector<f32> Sorted(Vector<f32> values) {
			std::sort(values.begin(), values.end());
			return values;
		}

		static size_t Distinct(const Vector<f32>& values) {
			Vector<f32> sorted = Sorted(values);
			return static_cast<size_t>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
		}
	};

	struct SystemsRun {
		GraphEffect effect;
		std::deque<ShaderAsset> shaders;
		std::deque<ParticleSystem> systems;
	};

	class GraphEffectSystemsFixture : public GraphEffectKernelFixture {
	protected:
		bool Begin(SystemsRun& run, const str& text, std::string_view name) {
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			if (!GraphEffectTarget::Compile(ParseEffect(text), name, run.effect, diagnostics)) {
				ADD_FAILURE() << name << " doesn't compile: " << (diagnostics.empty() ? str() : diagnostics[0].message);
				return false;
			}
			for (u32 s = 0; s < run.effect.systems.size(); ++s) {
				const GraphEffectSystem& compiled = run.effect.systems[s];
				ShaderAsset& initialize = run.shaders.emplace_back();
				ShaderAsset& update = run.shaders.emplace_back();
				if (!Import(initialize, compiled.initializeKernel, std::format("{}.System{}.Initialize.hlsl", name, s).c_str()) ||
					!Import(update, compiled.updateKernel, std::format("{}.System{}.Update.hlsl", name, s).c_str())) {
					return false;
				}
				run.systems.emplace_back();
			}
			const auto targets = [&](const Vector<GraphEffectEventTarget>& compiled, Vector<VisualEffectsImpl::ParticleEventTarget>& out) {
				for (const GraphEffectEventTarget& target : compiled) {
					out.push_back({&run.systems[target.child], target.listsBinding, target.argumentsBinding});
				}
			};
			for (u32 s = 0; s < run.effect.systems.size(); ++s) {
				const GraphEffectSystem& compiled = run.effect.systems[s];
				ParticleSystemDesc desc;
				desc.capacity = compiled.capacity;
				desc.strips = compiled.strips;
				desc.attributeCount = compiled.attributeCount;
				desc.initialize = Handle<ShaderAsset>(&run.shaders[2 * s]);
				desc.update = Handle<ShaderAsset>(&run.shaders[2 * s + 1]);
				desc.world = compiled.world;
				desc.measuresBounds = compiled.measuresBounds;
				for (const GraphEffectOutput& output : compiled.outputs) {
					VisualEffectsImpl::ParticleOutputDesc drawn;
					drawn.vertices = output.vertices;
					drawn.strip = output.strip;
					desc.outputs.push_back(drawn);
				}
				if (compiled.parent != GRAPH_EFFECT_NO_BINDING) {
					desc.parent = &run.systems[compiled.parent];
					desc.parentBinding = compiled.parentBinding;
				}
				targets(compiled.initializeEvents, desc.initializeEvents);
				targets(compiled.updateEvents, desc.updateEvents);
				if (!run.systems[s].Create(desc)) {
					ADD_FAILURE() << name << ": system " << s << " not created";
					return false;
				}
			}
			return true;
		}

		static void Advance(SystemsRun& run, f32 time, f32 delta, std::initializer_list<u32> spawns) {
			const f32 one[] = {delta};
			AdvanceUpdates(run, time, one, spawns);
		}

		static void AdvanceUpdates(SystemsRun& run, f32 time, std::span<const f32> deltas, std::initializer_list<u32> spawns) {
			for (size_t u = 0; u < deltas.size(); ++u) {
				const f32 delta = deltas[u];
				for (u32 s = 0; s < run.systems.size(); ++s) {
					const u32 spawn = s < spawns.size() && u == 0 ? spawns.begin()[s] : 0u;
					const GraphEffectSystem& compiled = run.effect.systems[s];
					Vector<u8> bytes{GetMemoryResource(MemTag::Graphics)};
					bytes.resize(run.effect.batchesOffset + compiled.batchStride, 0);
					const f32 clock[4] = {time, std::sin(time), std::cos(time), delta};
					std::memcpy(bytes.data(), clock, sizeof(clock));
					const u32 batches = spawn > 0 ? 1u : 0u;
					std::memcpy(bytes.data() + 20, &batches, 4);
					const f32 identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
					std::memcpy(bytes.data() + 32, identity, sizeof(identity));
					std::memcpy(bytes.data() + run.effect.batchesOffset, &spawn, 4);
					run.systems[s].SetFrameData(bytes);
					run.systems[s].Advance(delta, spawn);
				}
				time += delta;
			}
			RenderSystem::RenderAll();
		}

		static Vector<f32> Values(const SystemsRun& run, u32 system, std::string_view attribute, u32 component = 0) {
			Vector<f32> out{GetMemoryResource(MemTag::Graphics)};
			for (const GraphEffectStored& stored : run.effect.systems[system].stored) {
				if (stored.attribute != attribute) {
					continue;
				}
				Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
				Alive(run.systems[system], indices);
				for (u32 index : indices) {
					out.push_back(Attribute(run.systems[system], stored.slot + component, index));
				}
			}
			return out;
		}

		static u32 AliveCount(const SystemsRun& run, u32 system) {
			Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
			return Alive(run.systems[system], indices);
		}

		static u32 DroppedOf(const SystemsRun& run, u32 system) { return Dropped(run.systems[system]); }

		static Vector<f32> Sorted(Vector<f32> values) {
			std::sort(values.begin(), values.end());
			return values;
		}
	};

} // namespace Shalltear::EffectKernelHarness

#endif
