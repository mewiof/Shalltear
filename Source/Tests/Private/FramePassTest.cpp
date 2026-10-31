#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Core/Math/HalfFloat.h"

#include "Graphics/RenderGraph.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FramePassRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	struct KernelPush {
		u32 mode = 0;
		u32 count = 0;
		u32 base = 0;
		u32 spin = 0;
		u32 mask = 0;
	};

	class CountingPass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		RHI::BufferHandle values;
		RHI::BufferHandle stray;
		KernelPush push;
		u32 declares = 0;
		u32 records = 0;
		u32 dispatched = 0;

		void Declare(FramePassPlan& plan) override {
			++declares;
			plan.AddStep();
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
		}

		void Record(ComputeContext& context) override {
			++records;
			ComputeBinding binding{0, stray.IsValid() ? stray : values};
			if (context.Dispatch(kernel, {&binding, 1}, {}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, (push.count + 63) / 64)) {
				++dispatched;
			}
		}
	};

	class ReadbackPass final : public FramePass {
	public:
		static constexpr u32 WORDS = 4;
		static constexpr u32 SLOTS = RHI::FRAMES_IN_FLIGHT + 1;

		Handle<ShaderAsset> kernel;
		RHI::BufferHandle values;
		RHI::BufferHandle ring;
		RHI::BufferHandle copyInto;
		bool undeclaredSource = false;
		u32 count = 0;
		u32 frame = 0;
		u32 copied = 0;
		Vector<u32> read{GetMemoryResource(MemTag::Graphics)};

		static u32 BaseOf(u32 frame) { return 1000u * (frame + 1u); }

		void Declare(FramePassPlan& plan) override {
			if (frame >= RHI::FRAMES_IN_FLIGHT) {
				const auto* mapped = static_cast<const u32*>(RenderSystem::GetDevice()->GetMappedPointer(ring));
				read.push_back(mapped ? mapped[((frame - RHI::FRAMES_IN_FLIGHT) % SLOTS) * WORDS] : 0u);
			}
			plan.AddStep();
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
			plan.AddStep();
			plan.Read(values, undeclaredSource ? RHI::ResourceState::UnorderedAccess : RHI::ResourceState::TransferSrc);
			plan.Write(ring, RHI::ResourceState::TransferDst);
		}

		void Record(ComputeContext& context) override {
			if (context.GetStep() == 0) {
				const KernelPush push{0, count, BaseOf(frame)};
				ComputeBinding binding{0, values};
				context.Dispatch(kernel, {&binding, 1}, {}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, (count + 63) / 64);
				return;
			}
			const RHI::BufferHandle into = copyInto.IsValid() ? copyInto : ring;
			if (context.CopyBuffer(values, into, 0, (frame % SLOTS) * WORDS * 4, WORDS * 4)) {
				++copied;
			}
			++frame;
		}
	};

	struct TexturePush {
		f32 uvw[3] = {0.5f, 0.5f, 0.5f};
		f32 level = 0.0f;
	};

	class TexturePass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		RHI::BufferHandle values;
		Vector<ComputeTexture> slots{GetMemoryResource(MemTag::Graphics)};
		TexturePush push;
		u32 dispatched = 0;

		void Declare(FramePassPlan& plan) override {
			plan.AddStep();
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
		}

		void Record(ComputeContext& context) override {
			ComputeBinding binding{0, values};
			if (context.Dispatch(kernel, {&binding, 1}, slots, {reinterpret_cast<const u8*>(&push), sizeof(push)}, 1)) {
				++dispatched;
			}
		}
	};

	class LoadableTexture final : public TextureAsset {
	public:
		using TextureAsset::OnLoad;
		using TextureAsset::OnUnload;
	};

	class FramePassFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		ShaderAsset _kernel;
		static constexpr u32 COUNT = 500;
		static inline char OWNER = 0;
		static inline char OTHER_OWNER = 0;

		void SetUp() override {
			std::filesystem::path engineAssets = FramePassRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			std::filesystem::path source = FramePassRepoDir("Source/Tests/Fixtures/Graphics/ComputeFixture.hlsl");
			if (source.empty() || !_kernel.Import(Path(source.generic_string()))) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
		}

		void TearDown() override {
			FramePasses::UnregisterOwnedBy(&OWNER);
			FramePasses::UnregisterOwnedBy(&OTHER_OWNER);
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		RHI::BufferHandle MakeValues() {
			RHI::BufferDesc desc;
			desc.size = COUNT * 4;
			desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
			return RenderSystem::GetDevice()->CreateBufferResource(desc);
		}

		void Prepare(CountingPass& pass, u32 base) {
			pass.kernel = Handle<ShaderAsset>(&_kernel);
			pass.values = MakeValues();
			pass.push = {0, COUNT, base};
		}

		RHI::BufferHandle MakeRing() {
			RHI::BufferDesc desc;
			desc.size = ReadbackPass::SLOTS * ReadbackPass::WORDS * 4;
			desc.usage = RHI::BufferUsage::TransferDst;
			desc.access = RHI::MemoryAccess::Readback;
			desc.debugName = "FramePassReadbackRing";
			return RenderSystem::GetDevice()->CreateBufferResource(desc);
		}

		void Prepare(ReadbackPass& pass) {
			pass.kernel = Handle<ShaderAsset>(&_kernel);
			pass.values = MakeValues();
			pass.ring = MakeRing();
			pass.count = COUNT;
		}

		void Release(ReadbackPass& pass) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			for (RHI::BufferHandle buffer : {pass.values, pass.ring, pass.copyInto}) {
				if (buffer.IsValid()) {
					device.DestroyResource(buffer);
				}
			}
		}

		bool ImportTextureKernel(ShaderAsset& kernel) {
			const std::filesystem::path source = FramePassRepoDir("Source/Tests/Fixtures/Graphics/ComputeTextureFixture.hlsl");
			return !source.empty() && kernel.Import(Path(source.generic_string()));
		}

		static void FillVolume(LoadableTexture& texture) {
			texture.settings.shape = TextureShape::Texture3D;
			texture.settings.filter = RHI::FilterMode::Nearest;
			texture.settings.addressMode = RHI::AddressMode::ClampToEdge;
			texture.settings.generateMips = false;
			texture.size = Vec2I(2, 2);
			texture.slices = 2;
			texture.format = RHI::Format::RGBA16Float;
			texture.pixels.resize(8 * 8);
			u16* halves = reinterpret_cast<u16*>(texture.pixels.data());
			for (u32 i = 0; i < 8; ++i) {
				const f32 texel[4] = {static_cast<f32>(i) / 4.0f - 1.0f, static_cast<f32>(i), 0.0f, 1.0f};
				for (u32 c = 0; c < 4; ++c) {
					halves[i * 4 + c] = Math::FloatToHalf(texel[c]);
				}
			}
			texture.OnLoad();
		}

		void Prepare(TexturePass& pass, ShaderAsset& kernel) {
			pass.kernel = Handle<ShaderAsset>(&kernel);
			RHI::BufferDesc desc;
			desc.size = 16;
			desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
			pass.values = RenderSystem::GetDevice()->CreateBufferResource(desc);
		}

		static Vec4 Sampled(const TexturePass& pass) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			f32 out[4] = {};
			device.ReadbackBuffer(pass.values, {reinterpret_cast<u8*>(out), sizeof(out)});
			return Vec4(out[0], out[1], out[2], out[3]);
		}

		u32 WrongValues(RHI::BufferHandle values, u32 base) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			Vector<u32> out{GetMemoryResource(MemTag::Graphics)};
			out.assign(COUNT, 0);
			device.ReadbackBuffer(values, {reinterpret_cast<u8*>(out.data()), out.size() * 4});
			u32 wrong = 0;
			for (u32 i = 0; i < COUNT; ++i) {
				wrong += out[i] == base + i ? 0u : 1u;
			}
			return wrong;
		}
	};

	struct DepthPush {
		u32 pixel[2] = {0, 0};
	};

	class DepthPass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		RHI::BufferHandle values;
		Handle<Camera> camera;
		bool bindStray = false;
		Handle<TextureAsset> alsoAsset;
		bool askAfterSlots = false;
		RHI::BufferHandle fillers[GraphicsInternal::MAX_PASS_ACCESSES];
		Vector<CameraDepth> answers{GetMemoryResource(MemTag::Graphics)};
		u32 dispatched = 0;

		~DepthPass() override {
			RHI::Device* device = RenderSystem::GetDevice();
			if (!device || !values.IsValid()) {
				return;
			}
			device->WaitIdle();
			device->DestroyResource(values);
			for (RHI::BufferHandle filler : fillers) {
				device->DestroyResource(filler);
			}
		}

		void Declare(FramePassPlan& plan) override {
			CameraDepth stray;
			if (bindStray) {
				plan.AddStep();
				stray = plan.ReadCameraDepth(camera);
				plan.Write(fillers[0], RHI::ResourceState::UnorderedAccess);
			}
			plan.AddStep();
			if (askAfterSlots) {
				for (RHI::BufferHandle filler : fillers) {
					plan.Write(filler, RHI::ResourceState::UnorderedAccess);
				}
			}
			answers.push_back(bindStray ? stray : plan.ReadCameraDepth(camera));
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
		}

		void Record(ComputeContext& context) override {
			const CameraDepth& answer = answers.back();
			if (context.GetStep() != (bindStray ? 1u : 0u) || !answer.image.IsValid()) {
				return;
			}
			const DepthPush push{{static_cast<u32>(answer.size.x / 2), static_cast<u32>(answer.size.y / 2)}};
			ComputeBinding binding{0, values};
			ComputeTexture texture{1, 0, alsoAsset, answer.image};
			if (context.Dispatch(kernel, {&binding, 1}, {&texture, 1}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, 1)) {
				++dispatched;
			}
		}
	};

	class CameraDepthFixture : public ::testing::Test {
	protected:
		static constexpr f32 QUAD_DISTANCE = 4.0f;
		static inline char OWNER = 0;
		bool _mounted = false;
		bool _projectMounted = false;
		std::filesystem::path _root;
		ShaderAsset _kernel;
		Handle<Scene> _scene;
		Handle<SceneObject> _cameraObject;
		Handle<Camera> _camera;

		void SetUp() override {
			static std::atomic<u32> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_cameradepth_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
					 std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			_projectMounted = true;
			std::filesystem::path engineAssets = FramePassRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			std::filesystem::path source = FramePassRepoDir("Source/Tests/Fixtures/Graphics/ComputeDepthFixture.hlsl");
			Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (source.empty() || !_kernel.Import(Path(source.generic_string())) || !unlit || !quadMesh) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			FileSystem::WriteTextFile(Path((_root / "Surface.mat").generic_string()), "{\n\t\"shader\": \"" + unlit->assetId.ToString() + "\"\n}\n");
			auto* surface = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Surface.mat")));
			ASSERT_NE(surface, nullptr);

			_scene = SceneSystem::CreateScene("CameraDepth");
			Handle<SceneObject> quad = SceneSystem::CreateObject("Quad", _scene);
			quad->SetLocalPosition(Vec3(0.0f, 0.0f, QUAD_DISTANCE));
			quad->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
			quad->SetLocalScale(Vec3(20.0f, 20.0f, 1.0f));
			Handle<MeshRenderer> renderer = quad->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(surface));
			_cameraObject = SceneSystem::CreateObject("Camera", _scene);
			_camera = _cameraObject->AddComponent<Camera>();
			SceneSystem::EndFrame();
		}

		void TearDown() override {
			FramePasses::UnregisterOwnedBy(&OWNER);
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
			}
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Prepare(DepthPass& pass) {
			pass.kernel = Handle<ShaderAsset>(&_kernel);
			pass.camera = _camera;
			RHI::BufferDesc desc;
			desc.size = 16;
			desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
			pass.values = RenderSystem::GetDevice()->CreateBufferResource(desc);
			for (RHI::BufferHandle& filler : pass.fillers) {
				filler = RenderSystem::GetDevice()->CreateBufferResource(desc);
			}
		}

		ViewDesc CameraView(Vec2I size) const {
			ViewDesc desc;
			desc.view = _cameraObject->GetWorldMatrix().Inverse();
			desc.projection = _camera->BuildProjection(static_cast<f32>(size.x) / static_cast<f32>(size.y));
			desc.cameraPosition = _cameraObject->GetWorldPosition();
			desc.camera = _camera;
			return desc;
		}

		static f32 DepthAt(const Mat4& projection, f32 distance) {
			const Vec4 clip = projection * Vec4(0.0f, 0.0f, distance, 1.0f);
			return clip.z / clip.w;
		}

		static f32 Loaded(const DepthPass& pass) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			f32 value = -1.0f;
			device.ReadbackBuffer(pass.values, {reinterpret_cast<u8*>(&value), sizeof(value)});
			return value;
		}

		static bool SameMatrix(const Mat4& a, const Mat4& b) {
			for (u32 row = 0; row < 4; ++row) {
				for (u32 column = 0; column < 4; ++column) {
					if (std::abs(a(row, column) - b(row, column)) > 1.0e-6f) {
						return false;
					}
				}
			}
			return true;
		}
	};

} // anonymous namespace

TEST_F(FramePassFixture, PassRunsOnEmptyFrame) {
	CountingPass pass;
	Prepare(pass, 3);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	for (int frame = 0; frame < 3; ++frame) {
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(pass.declares, 3u);
	EXPECT_EQ(pass.records, 3u);
	EXPECT_EQ(pass.dispatched, 3u);
	EXPECT_EQ(WrongValues(pass.values, 3), 0u);
	RenderSystem::GetDevice()->DestroyResource(pass.values);
}

TEST_F(FramePassFixture, OnceFrameHoweverManyViewsFrameDraws) {
	CountingPass pass;
	Prepare(pass, 0);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	ViewTargetId first = RenderSystem::CreateViewTarget({64, 64});
	ViewTargetId second = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(first, INVALID_VIEW_TARGET);
	ASSERT_NE(second, INVALID_VIEW_TARGET);
	ViewDesc desc;
	desc.projection = Mat4::Perspective(60.0f * Math::DEG_TO_RAD, 1.0f, 0.1f, 100.0f);
	RenderSystem::SubmitView(first, desc);
	RenderSystem::SubmitView(second, desc);
	RenderSystem::RenderAll();
	EXPECT_EQ(pass.records, 1u);
	RenderSystem::DestroyViewTarget(first);
	RenderSystem::DestroyViewTarget(second);
	RenderSystem::GetDevice()->DestroyResource(pass.values);
}

TEST_F(FramePassFixture, StepBindsOnlyBuffersItDeclared) {
	CountingPass pass;
	Prepare(pass, 9);
	pass.stray = MakeValues();
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	RenderSystem::RenderAll();
	EXPECT_EQ(pass.records, 1u);
	EXPECT_EQ(pass.dispatched, 0u);
	RenderSystem::GetDevice()->DestroyResource(pass.values);
	RenderSystem::GetDevice()->DestroyResource(pass.stray);
}

TEST_F(FramePassFixture, SweepTakesOwnersPasses) {
	CountingPass swept;
	CountingPass kept;
	Prepare(swept, 0);
	Prepare(kept, 0);
	EXPECT_FALSE(FramePasses::Register(swept, nullptr));
	ASSERT_TRUE(FramePasses::Register(swept, &OWNER));
	EXPECT_FALSE(FramePasses::Register(swept, &OWNER));
	ASSERT_TRUE(FramePasses::Register(kept, &OTHER_OWNER));
	RenderSystem::RenderAll();
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(&OWNER), 1u);
	RenderSystem::RenderAll();
	EXPECT_EQ(swept.records, 1u);
	EXPECT_EQ(kept.records, 2u);
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(nullptr), 0u);
	RenderSystem::GetDevice()->DestroyResource(swept.values);
	RenderSystem::GetDevice()->DestroyResource(kept.values);
}

TEST_F(FramePassFixture, FrameCopyReadsBackLater) {
	ReadbackPass pass;
	Prepare(pass);
	ASSERT_TRUE(pass.values.IsValid() && pass.ring.IsValid());
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	constexpr u32 FRAMES = 7;
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(pass.copied, FRAMES);
	ASSERT_EQ(pass.read.size(), FRAMES - RHI::FRAMES_IN_FLIGHT);
	for (u32 i = 0; i < pass.read.size(); ++i) {
		EXPECT_EQ(pass.read[i], ReadbackPass::BaseOf(i)) << "frame " << i + RHI::FRAMES_IN_FLIGHT << ' ' << i;
	}
	Release(pass);
}

TEST_F(FramePassFixture, CopyTouchesDeclaredBuffers) {
	ReadbackPass wrongState;
	Prepare(wrongState);
	wrongState.undeclaredSource = true;
	ReadbackPass stray;
	Prepare(stray);
	stray.copyInto = MakeRing();
	ASSERT_TRUE(FramePasses::Register(wrongState, &OWNER));
	ASSERT_TRUE(FramePasses::Register(stray, &OWNER));
	RenderSystem::RenderAll();
	EXPECT_EQ(wrongState.copied, 0u);
	EXPECT_EQ(stray.copied, 0u);
	Release(wrongState);
	Release(stray);
}

TEST_F(FramePassFixture, KernelSamplesTextureAssetDispatchNames) {
	ShaderAsset kernel;
	if (!ImportTextureKernel(kernel)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	LoadableTexture volume;
	FillVolume(volume);
	ASSERT_TRUE(volume.texture.IsValid());
	TexturePass pass;
	Prepare(pass, kernel);
	pass.slots.push_back({1, 2, Handle<TextureAsset>(&volume)});
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	pass.push = {{0.25f, 0.75f, 0.25f}, 0.0f};
	RenderSystem::RenderAll();
	EXPECT_EQ(pass.dispatched, 1u);
	Vec4 sampled = Sampled(pass);
	EXPECT_FLOAT_EQ(sampled.x, -0.5f);
	EXPECT_FLOAT_EQ(sampled.y, 2.0f);
	pass.push = {{0.75f, 0.25f, 0.75f}, 0.0f};
	RenderSystem::RenderAll();
	sampled = Sampled(pass);
	EXPECT_FLOAT_EQ(sampled.x, 0.25f);
	EXPECT_FLOAT_EQ(sampled.y, 5.0f);
	volume.OnUnload();
	RenderSystem::GetDevice()->DestroyResource(pass.values);
}

TEST_F(FramePassFixture, BadSlotSamplesFallback) {
	ShaderAsset kernel;
	if (!ImportTextureKernel(kernel)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	LoadableTexture unloaded;
	FillVolume(unloaded);
	unloaded.OnUnload();
	LoadableTexture flat;
	flat.size = Vec2I(1, 1);
	flat.format = RHI::Format::RGBA8Unorm;
	flat.settings.generateMips = false;
	flat.pixels.assign({255, 0, 0, 255});
	flat.OnLoad();
	ASSERT_TRUE(flat.texture.IsValid());
	const Handle<TextureAsset> cases[] = {Handle<TextureAsset>{}, Handle<TextureAsset>(&unloaded), Handle<TextureAsset>(&flat)};
	const char* names[] = {"no asset", "an unloaded asset", "a 2D texture in a 3D slot"};
	for (u32 i = 0; i < 3; ++i) {
		TexturePass pass;
		Prepare(pass, kernel);
		pass.slots.push_back({1, 2, cases[i]});
		ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
		RenderSystem::RenderAll();
		FramePasses::UnregisterOwnedBy(&OWNER);
		EXPECT_EQ(pass.dispatched, 1u) << names[i];
		const Vec4 sampled = Sampled(pass);
		EXPECT_NEAR(sampled.x, 128.0f / 255.0f, 1.0e-3f) << names[i];
		EXPECT_NEAR(sampled.y, 128.0f / 255.0f, 1.0e-3f) << names[i];
		RenderSystem::GetDevice()->DestroyResource(pass.values);
	}
	flat.OnUnload();
}

TEST_F(FramePassFixture, TextureFillsDeclaredBindings) {
	ShaderAsset kernel;
	if (!ImportTextureKernel(kernel)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	LoadableTexture volume;
	FillVolume(volume);
	const Handle<TextureAsset> held(&volume);
	const ComputeTexture overBuffer[] = {{1, 2, held}, {0, 2, held}};
	const ComputeTexture swapped[] = {{2, 1, held}};
	const std::span<const ComputeTexture> wrong[] = {overBuffer, swapped, {}};
	const char* names[] = {"a texture at the buffer's binding", "a texture swapped with its sampler", "no texture at all"};
	for (u32 i = 0; i < 3; ++i) {
		TexturePass pass;
		Prepare(pass, kernel);
		pass.slots.assign(wrong[i].begin(), wrong[i].end());
		ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
		RenderSystem::RenderAll();
		FramePasses::UnregisterOwnedBy(&OWNER);
		EXPECT_EQ(pass.dispatched, 0u) << names[i] << " was dispatched";
		RenderSystem::GetDevice()->DestroyResource(pass.values);
	}
	volume.OnUnload();
}

namespace {

	class LoadableMesh final : public MeshAsset {
	public:
		using MeshAsset::OnLoad;
		using MeshAsset::OnUnload;
		~LoadableMesh() override { OnUnload(); }
	};

	struct MeshPush {
		u64 vertices = 0;
		u64 indices = 0;
		u32 count = 0;
		u32 padding = 0;
	};

	class MeshPass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		RHI::BufferHandle values;
		MeshPush push;
		u32 dispatched = 0;

		void Declare(FramePassPlan& plan) override {
			plan.AddStep();
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
		}

		void Record(ComputeContext& context) override {
			ComputeBinding binding{0, values};
			if (context.Dispatch(kernel, {&binding, 1}, {}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, (push.count + 63) / 64)) {
				++dispatched;
			}
		}
	};

} // namespace

TEST_F(FramePassFixture, KernelReadsMeshByAddress) {
	ShaderAsset kernel;
	const std::filesystem::path source = FramePassRepoDir("Source/Tests/Fixtures/Graphics/ComputeMeshFixture.hlsl");
	if (source.empty() || !kernel.Import(Path(source.generic_string()))) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	LoadableMesh mesh;
	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Cube, mesh));
	mesh.OnLoad();
	ASSERT_NE(mesh.vertexBufferAddress, 0u);
	ASSERT_NE(mesh.indexBufferAddress, 0u);
	const u32 count = static_cast<u32>(mesh.indices.size());
	MeshPass pass;
	pass.kernel = Handle<ShaderAsset>(&kernel);
	RHI::BufferDesc desc;
	desc.size = static_cast<u64>(count) * 8;
	desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
	pass.values = RenderSystem::GetDevice()->CreateBufferResource(desc);
	pass.push = {mesh.vertexBufferAddress, mesh.indexBufferAddress, count};
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	RenderSystem::RenderAll();
	ASSERT_EQ(pass.dispatched, 1u);
	RHI::Device& device = *RenderSystem::GetDevice();
	device.WaitIdle();
	Vector<u32> read{GetMemoryResource(MemTag::Graphics)};
	read.assign(static_cast<size_t>(count) * 2, ~0u);
	device.ReadbackBuffer(pass.values, {reinterpret_cast<u8*>(read.data()), read.size() * 4});
	u32 wrong = 0;
	for (u32 i = 0; i < count; ++i) {
		const u32 index = mesh.indices[i];
		wrong += read[i * 2] == index && std::bit_cast<f32>(read[i * 2 + 1]) == mesh.vertices[index].position.x ? 0u : 1u;
	}
	EXPECT_EQ(wrong, 0u) << "of " << count;
	FramePasses::UnregisterOwnedBy(&OWNER);
	device.DestroyResource(pass.values);
	mesh.OnUnload();
	EXPECT_EQ(mesh.indexBufferAddress, 0u);
	EXPECT_EQ(mesh.vertexBufferAddress, 0u);
}

TEST_F(CameraDepthFixture, PassReadsPreviousFrameDepth) {
	DepthPass pass;
	Prepare(pass);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	const Vec2I size{64, 64};
	const ViewTargetId target = RenderSystem::CreateViewTarget(size);
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	const ViewDesc first = CameraView(size);
	RenderSystem::SubmitView(target, first);
	RenderSystem::RenderAll();
	ASSERT_EQ(pass.answers.size(), 1u);
	EXPECT_EQ(pass.answers[0].size, Vec2I{});
	ASSERT_TRUE(pass.answers[0].image.IsValid());
	EXPECT_EQ(Loaded(pass), 0.0f);

	_cameraObject->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();
	const ViewDesc second = CameraView(size);
	RenderSystem::SubmitView(target, second);
	RenderSystem::RenderAll();
	ASSERT_EQ(pass.answers.size(), 2u);
	const CameraDepth& drawn = pass.answers[1];
	ASSERT_EQ(drawn.size, size);
	EXPECT_TRUE(SameMatrix(drawn.view, first.view));
	EXPECT_TRUE(SameMatrix(drawn.projection, first.projection));
	EXPECT_EQ(pass.dispatched, 2u);
	EXPECT_NEAR(Loaded(pass), DepthAt(first.projection, QUAD_DISTANCE), 1.0e-6f);

	RenderSystem::SubmitView(target, CameraView(size));
	RenderSystem::RenderAll();
	ASSERT_EQ(pass.answers.size(), 3u);
	EXPECT_TRUE(SameMatrix(pass.answers[2].view, second.view));
	EXPECT_NEAR(Loaded(pass), DepthAt(second.projection, QUAD_DISTANCE - 1.0f), 1.0e-6f);

	RenderSystem::DestroyViewTarget(target);
}

TEST_F(CameraDepthFixture, WindowCameraKeepsDepth) {
	WindowDesc windowDesc;
	windowDesc.title = "CameraDepthTest";
	windowDesc.clientSize = {64, 64};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());
	_camera->SetTargetWindow(window);
	DepthPass pass;
	Prepare(pass);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	for (u32 frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		RenderSystem::RenderAll();
	}
	ASSERT_EQ(pass.answers.size(), 3u);
	const CameraDepth& drawn = pass.answers.back();
	ASSERT_GT(drawn.size.x, 0);
	const Mat4 projection = _camera->BuildProjection(static_cast<f32>(drawn.size.x) / static_cast<f32>(drawn.size.y));
	EXPECT_TRUE(SameMatrix(drawn.projection, projection));
	EXPECT_NEAR(Loaded(pass), DepthAt(projection, QUAD_DISTANCE), 1.0e-6f);
	FramePasses::UnregisterOwnedBy(&OWNER);
	RenderSystem::RenderAll();
	WindowSystem::RequestDestroy(window);
}

TEST_F(CameraDepthFixture, FirstSubmittedViewKeepsDepth) {
	DepthPass pass;
	Prepare(pass);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	const Vec2I wide{64, 64};
	const Vec2I narrow{32, 48};
	const ViewTargetId a = RenderSystem::CreateViewTarget(wide);
	const ViewTargetId b = RenderSystem::CreateViewTarget(narrow);
	for (u32 frame = 0; frame < 2; ++frame) {
		RenderSystem::SubmitView(a, CameraView(wide));
		RenderSystem::SubmitView(b, CameraView(narrow));
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(pass.answers.back().size, wide);
	for (u32 frame = 0; frame < 2; ++frame) {
		RenderSystem::SubmitView(b, CameraView(narrow));
		RenderSystem::SubmitView(a, CameraView(wide));
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(pass.answers.back().size, narrow);
	EXPECT_NEAR(Loaded(pass), DepthAt(CameraView(narrow).projection, QUAD_DISTANCE), 1.0e-6f);
	RenderSystem::DestroyViewTarget(a);
	RenderSystem::DestroyViewTarget(b);
}

TEST_F(CameraDepthFixture, KeptDepthFreedWhenUnasked) {
	const Vec2I size{64, 64};
	const ViewTargetId target = RenderSystem::CreateViewTarget(size);
	ViewDesc anonymous = CameraView(size);
	anonymous.camera = {};
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::SubmitView(target, anonymous);
		RenderSystem::RenderAll();
	}
	const u32 base = RenderSystem::GetStats().liveTextures;
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::SubmitView(target, CameraView(size));
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, base);

	DepthPass pass;
	Prepare(pass);
	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::SubmitView(target, CameraView(size));
		RenderSystem::RenderAll();
	}
	ASSERT_EQ(pass.answers.back().size, size);
	const u32 asked = RenderSystem::GetStats().liveTextures;
	EXPECT_GT(asked, base);

	FramePasses::UnregisterOwnedBy(&OWNER);
	u32 frames = 0;
	while (RenderSystem::GetStats().liveTextures > base && frames < 1000) {
		RenderSystem::SubmitView(target, CameraView(size));
		RenderSystem::RenderAll();
		++frames;
	}
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, base);
	EXPECT_GT(frames, 30u);

	ASSERT_TRUE(FramePasses::Register(pass, &OWNER));
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::SubmitView(target, CameraView(size));
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, asked);
	SceneSystem::Destroy(_cameraObject);
	SceneSystem::EndFrame();
	for (u32 frame = 0; frame <= RHI::FRAMES_IN_FLIGHT + 1; ++frame) {
		RenderSystem::SubmitView(target, anonymous);
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, base);
	EXPECT_EQ(pass.answers.back().size, Vec2I{});
	RenderSystem::DestroyViewTarget(target);
}

TEST_F(CameraDepthFixture, StepBindsOnlyImageItDeclaredReading) {
	const Vec2I size{64, 64};
	const ViewTargetId target = RenderSystem::CreateViewTarget(size);
	LoadableTexture flat;
	flat.size = Vec2I(1, 1);
	flat.format = RHI::Format::RGBA8Unorm;
	flat.settings.generateMips = false;
	flat.pixels.assign({255, 255, 255, 255});
	flat.OnLoad();
	DepthPass stray;
	Prepare(stray);
	stray.bindStray = true;
	DepthPass both;
	Prepare(both);
	both.alsoAsset = Handle<TextureAsset>(&flat);
	DepthPass late;
	Prepare(late);
	late.askAfterSlots = true;
	DepthPass control;
	Prepare(control);
	for (DepthPass* pass : {&stray, &both, &late, &control}) {
		ASSERT_TRUE(FramePasses::Register(*pass, &OWNER));
	}
	for (u32 frame = 0; frame < 3; ++frame) {
		RenderSystem::SubmitView(target, CameraView(size));
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(stray.answers.back().size, size);
	EXPECT_EQ(stray.dispatched, 0u);
	EXPECT_EQ(both.answers.back().size, size);
	EXPECT_EQ(both.dispatched, 0u);
	EXPECT_FALSE(late.answers.back().image.IsValid());
	EXPECT_EQ(late.dispatched, 0u);
	EXPECT_EQ(control.answers.back().size, size);
	EXPECT_EQ(control.dispatched, 3u);
	FramePasses::UnregisterOwnedBy(&OWNER);
	RenderSystem::DestroyViewTarget(target);
	flat.OnUnload();
}
