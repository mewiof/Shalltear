#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "UITestUtil.h"

#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Graphics/ReflectionProbe.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ReflectionProbeBake.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace Shalltear;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;
using Shalltear::GraphicsInternal::RadianceCubeFaceOffset;

namespace {

	Vec3 WallColor(u32 face) {
		switch (face) {
		case 0:
			return Vec3(0.8f, 0.0f, 0.0f);
		case 1:
			return Vec3(0.0f, 0.8f, 0.0f);
		case 2:
			return Vec3(0.0f, 0.0f, 0.8f);
		case 3:
			return Vec3(0.8f, 0.8f, 0.0f);
		case 4:
			return Vec3(0.0f, 0.8f, 0.8f);
		default:
			return Vec3(0.8f, 0.0f, 0.8f);
		}
	}

	Guid UnlitShaderGuid() {
		Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
		return shader ? shader->assetId : Guid{};
	}

	Vec3 FaceCentre(const TextureAsset& cube, u32 face) {
		const u32 side = static_cast<u32>(cube.size.x);
		const size_t index = (static_cast<size_t>(side / 2) * side + side / 2) * 4;
		Vector<u8> decoded{GetMemoryResource(MemTag::Asset)};
		const u16* texels = nullptr;
		if (RHI::IsBlockCompressed(cube.format)) {
			const TextureCodec* codec = TextureCodecs::Find(cube.format);
			const size_t faceBytes = static_cast<size_t>(RHI::FormatLevelBytes(cube.format, cube.size));
			decoded.resize(static_cast<size_t>(RHI::FormatLevelBytes(TextureCodecs::DecodedFormat(cube.format), cube.size)));
			if (!codec || cube.pixels.size() < faceBytes * (face + 1) ||
				!codec->decode(cube.format, {cube.pixels.data() + faceBytes * face, faceBytes}, cube.size, decoded)) {
				return Vec3(0.0f);
			}
			texels = reinterpret_cast<const u16*>(decoded.data());
		} else {
			texels = reinterpret_cast<const u16*>(cube.pixels.data() + RadianceCubeFaceOffset(side, cube.storedMipLevels, 0, face));
		}
		return Vec3(Math::HalfToFloat(texels[index]), Math::HalfToFloat(texels[index + 1]), Math::HalfToFloat(texels[index + 2]));
	}

	void ExpectWall(const Vec3& measured, const Vec3& wall, const char* what) {
		const f32 peak = Math::Max(measured.x, Math::Max(measured.y, measured.z));
		ASSERT_GT(peak, 0.4f) << what;
		EXPECT_EQ(measured.x > peak * 0.5f, wall.x > 0.4f) << what << ": red";
		EXPECT_EQ(measured.y > peak * 0.5f, wall.y > 0.4f) << what << ": green";
		EXPECT_EQ(measured.z > peak * 0.5f, wall.z > 0.4f) << what << ": blue";
	}

	class ReflectionProbeBakeFixture : public MaterialReadbackFixture {
	protected:
		Handle<Scene> _room;
		Handle<SceneObject> _walls[6];

		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			ReflectionProbeBake::Initialize();
		}

		void TearDown() override {
			ReflectionProbeBake::Terminate();
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			EditorAssetOps::Terminate();
			MaterialReadbackFixture::TearDown();
		}

		MaterialAsset* LoadWall(const char* fileName, const Vec3& color) {
			return LoadMaterial(fileName, UnlitShaderGuid(),
								std::format("\t\"cull\": \"off\",\n\t\"colors\": {{ \"baseColor\": [{}, {}, {}, 1] }}", color.x, color.y,
											color.z));
		}

		bool BuildRoom() {
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!quadMesh) {
				return false;
			}
			_room = SceneSystem::CreateScene("Room");
			for (u32 face = 0; face < 6; ++face) {
				MaterialAsset* material = LoadWall(std::format("Wall{}.mat", face).c_str(), WallColor(face));
				if (!material) {
					return false;
				}
				const Vec3 normal = GraphicsInternal::CubeTexelDirection(face, 0.5f, 0.5f);
				Handle<SceneObject> wall = SceneSystem::CreateObject(std::format("Wall{}", face), _room);
				wall->SetLocalPosition(normal);
				if (face < 2) {
					wall->SetLocalRotation(Quat::AngleAxis(90.0f, Vec3::Up()));
				} else if (face < 4) {
					wall->SetLocalRotation(Quat::AngleAxis(90.0f, Vec3::Right()));
				}
				wall->SetLocalScale(Vec3(3.0f, 3.0f, 3.0f));
				Handle<MeshRenderer> renderer = wall->AddComponent<MeshRenderer>();
				renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
				renderer->SetMaterial(0, Handle<MaterialAsset>(material));
				_walls[face] = wall;
			}
			SceneSystem::EndFrame();
			return true;
		}

		Handle<ReflectionProbe> AddProbe(const char* name) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _room);
			Handle<ReflectionProbe> probe = object->AddComponent<ReflectionProbe>();
			SceneSystem::EndFrame();
			return probe;
		}

		bool SaveRoom() { return EditorSceneOps::SaveScene(_room, Path("project://Assets/Room.scene")); }

		void RunBakes() {
			for (u32 frame = 0; frame < 16 && ReflectionProbeBake::GetPendingCount() > 0; ++frame) {
				ReflectionProbeBake::Drain();
				RenderSystem::RenderAll();
			}
			ReflectionProbeBake::Drain();
		}

		bool Exists(const char* virtualPath) { return FileSystem::Exists(Path(virtualPath)); }
	};

} // anonymous namespace

TEST_F(ReflectionProbeBakeFixture, BakeCapturesEachWallOntoFace) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());

	ASSERT_TRUE(ReflectionProbeBake::Request(probe));
	RunBakes();

	ASSERT_TRUE(Exists("project://Assets/Room/Probe-reflectionHDR.hdr"));
	TextureAsset* cube = probe->GetBakedCubemap().Get();
	ASSERT_NE(cube, nullptr);
	EXPECT_TRUE(cube->isCube);
	EXPECT_EQ(cube->size.x, RenderSystem::GetReflectionProbeResolution());
	EXPECT_GT(cube->storedMipLevels, 1u);
	EXPECT_EQ(cube->format, RHI::Format::BC6HUFloat);
	for (u32 face = 0; face < 6; ++face) {
		ExpectWall(FaceCentre(*cube, face), WallColor(face), std::format("face {}", face).c_str());
	}
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Bake Reflection Probe");
}

TEST_F(ReflectionProbeBakeFixture, RebakeRefillsCubeInPlace) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());
	ASSERT_TRUE(ReflectionProbeBake::Request(probe));
	RunBakes();
	TextureAsset* cube = probe->GetBakedCubemap().Get();
	ASSERT_NE(cube, nullptr);
	const u64 instance = cube->GetInstanceId();
	const u32 version = cube->version;
	const u32 undoDepth = UndoSystem::GetUndoDepth();

	MaterialAsset* white = LoadWall("White.mat", Vec3(0.8f, 0.8f, 0.8f));
	ASSERT_NE(white, nullptr);
	_walls[0]->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(white));
	SceneSystem::EndFrame();

	ASSERT_TRUE(ReflectionProbeBake::Request(probe));
	RunBakes();

	EXPECT_FALSE(Exists("project://Assets/Room/Probe-reflectionHDR 1.hdr"));
	ASSERT_EQ(probe->GetBakedCubemap().GetInstanceId(), instance);
	EXPECT_EQ(cube->version, version + 1);
	EXPECT_EQ(UndoSystem::GetUndoDepth(), undoDepth);
	ExpectWall(FaceCentre(*cube, 0), Vec3(0.8f, 0.8f, 0.8f), "the repainted +X face");
	ExpectWall(FaceCentre(*cube, 1), WallColor(1), "the untouched -X face");
}

TEST_F(ReflectionProbeBakeFixture, UndoingBakeGivesProbeBackPreviousCube) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());
	ASSERT_TRUE(ReflectionProbeBake::Request(probe));
	RunBakes();
	ASSERT_TRUE(probe->GetBakedCubemap().IsValid());

	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_FALSE(probe->GetBakedCubemap().IsValid());
	EXPECT_TRUE(Exists("project://Assets/Room/Probe-reflectionHDR.hdr"));
}

TEST_F(ReflectionProbeBakeFixture, SameNameProbesBakeSeparately) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> first = AddProbe("Probe");
	Handle<ReflectionProbe> second = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());

	ASSERT_TRUE(ReflectionProbeBake::Request(first));
	ASSERT_TRUE(ReflectionProbeBake::Request(second));
	EXPECT_EQ(ReflectionProbeBake::GetPendingCount(), 2u);
	RunBakes();

	ASSERT_TRUE(first->GetBakedCubemap().IsValid());
	ASSERT_TRUE(second->GetBakedCubemap().IsValid());
	EXPECT_NE(first->GetBakedCubemap().GetInstanceId(), second->GetBakedCubemap().GetInstanceId());
	EXPECT_TRUE(Exists("project://Assets/Room/Probe-reflectionHDR.hdr"));
	EXPECT_TRUE(Exists("project://Assets/Room/Probe-reflectionHDR 1.hdr"));
}

TEST_F(ReflectionProbeBakeFixture, BakingDuplicatedProbeLeavesOriginalsFileAlone) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> original = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());
	ASSERT_TRUE(ReflectionProbeBake::Request(original));
	RunBakes();
	TextureAsset* originalCube = original->GetBakedCubemap().Get();
	ASSERT_NE(originalCube, nullptr);
	const u32 originalVersion = originalCube->version;

	Handle<ReflectionProbe> duplicate = AddProbe("Probe");
	duplicate->SetBakedCubemap(original->GetBakedCubemap());
	const Path duplicatePath = ReflectionProbeBakeOps::BakePath(duplicate);
	EXPECT_NE(duplicatePath.ToString(), "project://Assets/Room/Probe-reflectionHDR.hdr");

	ASSERT_TRUE(ReflectionProbeBake::Request(duplicate));
	RunBakes();
	EXPECT_NE(duplicate->GetBakedCubemap().GetInstanceId(), originalCube->GetInstanceId());
	EXPECT_EQ(originalCube->version, originalVersion);
}

TEST_F(ReflectionProbeBakeFixture, AProbeDestroyedMidBakeWritesNothing) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> warm = AddProbe("Warm");
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());
	auto settle = []() {
		for (u32 frame = 0; frame < 4; ++frame) {
			RenderSystem::RenderAll();
		}
	};
	ASSERT_TRUE(ReflectionProbeBake::Request(warm));
	RunBakes();
	ASSERT_TRUE(warm->GetBakedCubemap().IsValid());
	settle();
	const u32 texturesBefore = RenderSystem::GetStats().liveTextures;

	ASSERT_TRUE(ReflectionProbeBake::Request(probe));
	ReflectionProbeBake::Drain();
	SceneSystem::Destroy(probe->GetOwner());
	SceneSystem::EndFrame();
	RenderSystem::RenderAll();
	ReflectionProbeBake::Drain();

	EXPECT_EQ(ReflectionProbeBake::GetPendingCount(), 0u);
	EXPECT_FALSE(Exists("project://Assets/Room/Probe-reflectionHDR.hdr"));
	settle();
	EXPECT_EQ(RenderSystem::GetStats().liveTextures, texturesBefore);
}

TEST_F(ReflectionProbeBakeFixture, ProbeInUnsavedSceneIsNotBaked) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	EXPECT_FALSE(ReflectionProbeBake::WhyNotBakeable(probe).empty());
	EXPECT_FALSE(ReflectionProbeBake::Request(probe));
	EXPECT_EQ(ReflectionProbeBake::GetPendingCount(), 0u);
}

TEST_F(ReflectionProbeBakeFixture, ClickingBakeInInspectorQueuesProbe) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ASSERT_TRUE(BuildRoom());
	Handle<ReflectionProbe> probe = AddProbe("Probe");
	ASSERT_TRUE(SaveRoom());
	EditorSelection::Select(Handle<Object>(probe->GetOwner().Get()));
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
	ASSERT_TRUE(panel.IsValid());

	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 1400};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_TRUE(context.IsAlive(UITest::FindByText(context, root, "boxSize")));
	EXPECT_TRUE(context.IsAlive(UITest::FindByText(context, root, "nearPlane")));
	UI::ElementHandle label = UITest::FindByText(context, root, "Bake");
	ASSERT_TRUE(context.IsAlive(label));
	const UI::Element* labelElement = context.GetElement(label);
	ASSERT_NE(labelElement, nullptr);
	UITest::ClickElement(context, context.MakeHandle(labelElement->parent));
	EXPECT_EQ(ReflectionProbeBake::GetPendingCount(), 1u);
	RunBakes();
	EXPECT_TRUE(probe->GetBakedCubemap().IsValid());
}

TEST(ReflectionProbeBakeOpsTest, StripRoundTripsWithinPrecision) {
	constexpr u32 side = 16;
	Vector<u8> faces{GetMemoryResource(MemTag::Core)};
	faces.resize(static_cast<size_t>(side) * side * 8 * 6);
	u16* halves = reinterpret_cast<u16*>(faces.data());
	auto value = [](u32 face, u32 x, u32 y, u32 channel) {
		return std::pow(10.0f, static_cast<f32>(face) - 3.0f) * (1.0f + static_cast<f32>(x * side + y + channel * 7) / 512.0f);
	};
	for (u32 face = 0; face < 6; ++face) {
		for (u32 y = 0; y < side; ++y) {
			for (u32 x = 0; x < side; ++x) {
				u16* texel = halves + ((static_cast<size_t>(face) * side + y) * side + x) * 4;
				for (u32 channel = 0; channel < 3; ++channel) {
					texel[channel] = Math::FloatToHalf(value(face, x, y, channel));
				}
				texel[3] = Math::FloatToHalf(1.0f);
			}
		}
	}
	const Vector<u8> file = ReflectionProbeBakeOps::EncodeStrip({faces.data(), faces.size()}, side);
	ASSERT_FALSE(file.empty());

	const std::filesystem::path root = std::filesystem::temp_directory_path() / "shalltear_bake_strip";
	std::filesystem::create_directories(root);
	const std::filesystem::path strip = root / "Strip.hdr";
	{
		std::ofstream out(strip, std::ios::binary);
		out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
	}
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("convolveIBL", JSONValue::MakeBool(false));
	settings.AddMember("compression", JSONValue::MakeString("none"));
	TextureAssetImporter importer;
	TextureAsset* cube = static_cast<TextureAsset*>(importer.ImportWithSettings(Path(strip.generic_string()), Path("project://Strip"), settings));
	ASSERT_NE(cube, nullptr);
	ASSERT_TRUE(cube->isCube);
	ASSERT_EQ(cube->size.x, static_cast<i32>(side));
	const u16* read = reinterpret_cast<const u16*>(cube->pixels.data());
	for (u32 face = 0; face < 6; ++face) {
		for (u32 y = 0; y < side; ++y) {
			for (u32 x = 0; x < side; ++x) {
				const u16* texel = read + ((static_cast<size_t>(face) * side + y) * side + x) * 4;
				const f32 peak = value(face, x, y, 2);
				for (u32 channel = 0; channel < 3; ++channel) {
					ASSERT_NEAR(Math::HalfToFloat(texel[channel]), value(face, x, y, channel), peak * 0.01f)
						<< "face " << face << " (" << x << ", " << y << ") channel " << channel;
				}
			}
		}
	}
	Delete(cube);
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

TEST(ReflectionProbeBakeOpsTest, EncodingNeeds6Faces) {
	Vector<u8> faces{GetMemoryResource(MemTag::Core)};
	faces.resize(16 * 16 * 8 * 5);
	EXPECT_TRUE(ReflectionProbeBakeOps::EncodeStrip({faces.data(), faces.size()}, 16).empty());
	EXPECT_TRUE(ReflectionProbeBakeOps::EncodeStrip({}, 0).empty());
}
