#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/ObjectPass.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class RenderPassAssetFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			_dir = std::filesystem::temp_directory_path() / "shalltear_render_pass_asset_test";
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
			std::filesystem::create_directories(_dir, ec);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
		}

		Path FilePath(const char* name) const { return Path((_dir / name).generic_string()); }

		Asset* ImportText(const char* name, std::string_view text) {
			Path path = FilePath(name);
			EXPECT_TRUE(FileSystem::WriteTextFile(path, str(text)));
			RenderPassImporter importer;
			return importer.Import(path);
		}

		std::filesystem::path _dir;
	};

	TEST_F(RenderPassAssetFixture, DocumentNamesKindAndImporterBuildsIt) {
		ObjectPass authored;
		authored.point = PassPoint::BeforeTransparent;
		authored.position = FrameStagePosition::Early;
		authored.renderingLayerMask = 0x0000002Au;

		Path path = FilePath("Outline.slpass");
		ASSERT_TRUE(authored.Export(path));

		RenderPassImporter importer;
		Asset* imported = importer.Import(path);
		ASSERT_NE(imported, nullptr);

		ASSERT_EQ(imported->GetType(), ObjectPass::GetReflectType());
		auto* row = static_cast<ObjectPass*>(imported);
		EXPECT_EQ(row->point, PassPoint::BeforeTransparent);
		EXPECT_EQ(row->position, FrameStagePosition::Early);
		EXPECT_EQ(row->renderingLayerMask, 0x0000002Au);

		TypeRegistry::DestroyObjectInstance(row);
	}

	TEST_F(RenderPassAssetFixture, FullScreenRowRoundTrips) {
		FullScreenPass authored;
		authored.point = PassPoint::Post;
		authored.position = FrameStagePosition::Late;

		Path path = FilePath("Vignette.slpass");
		ASSERT_TRUE(authored.Export(path));

		RenderPassImporter importer;
		Asset* imported = importer.Import(path);
		ASSERT_NE(imported, nullptr);
		ASSERT_EQ(imported->GetType(), FullScreenPass::GetReflectType());

		auto* row = static_cast<FullScreenPass*>(imported);
		EXPECT_EQ(row->point, PassPoint::Post);
		EXPECT_EQ(row->position, FrameStagePosition::Late);

		TypeRegistry::DestroyObjectInstance(row);
	}

	TEST_F(RenderPassAssetFixture, PlacementWrittenAsName) {
		ObjectPass authored;
		authored.point = PassPoint::AfterTransparent;
		authored.position = FrameStagePosition::Late;

		Path path = FilePath("Named.slpass");
		ASSERT_TRUE(authored.Export(path));

		str text = FileSystem::ReadTextFile(path, MemTag::Asset);
		EXPECT_NE(text.find("\"AfterTransparent\""), str::npos) << text;
		EXPECT_NE(text.find("\"Late\""), str::npos) << text;
		EXPECT_NE(text.find("\"ObjectPass\""), str::npos);
	}

	TEST_F(RenderPassAssetFixture, DocumentNamingUnknownKindIsRefused) {
		EXPECT_EQ(ImportText("Unknown.slpass", R"({"_type": "APassThisBuildHasNeverHeardOf"})"), nullptr);
	}

	TEST_F(RenderPassAssetFixture, NonPassTypeRefused) {
		ASSERT_NE(Asset::GetReflectType(), nullptr);
		ASSERT_NE(TypeRegistry::FindType(StringHash("Asset")), nullptr);
		EXPECT_EQ(ImportText("NotAPass.slpass", R"({"_type": "Asset"})"), nullptr);
	}

	TEST_F(RenderPassAssetFixture, DocumentNamingAbstractBaseIsRefused) {
		ASSERT_NE(TypeRegistry::FindType(StringHash("RenderPass")), nullptr);
		EXPECT_EQ(ImportText("Abstract.slpass", R"({"_type": "RenderPass"})"), nullptr);
	}

	TEST_F(RenderPassAssetFixture, KindlessDocumentRefused) {
		EXPECT_EQ(ImportText("Anonymous.slpass", R"({"point": "Overlay"})"), nullptr);
		EXPECT_EQ(ImportText("NotJson.slpass", "this is not a document"), nullptr);
	}

	TEST_F(RenderPassAssetFixture, MissingKeyKeepsKindDefault) {
		Asset* imported = ImportText("Partial.slpass", R"({"_type": "ObjectPass", "point": "Overlay"})");
		ASSERT_NE(imported, nullptr);
		auto* row = static_cast<ObjectPass*>(imported);
		EXPECT_EQ(row->point, PassPoint::Overlay);
		EXPECT_EQ(row->position, FrameStagePosition::Normal);
		EXPECT_EQ(row->renderingLayerMask, ~0u);
		TypeRegistry::DestroyObjectInstance(row);
	}

	class RenderPassBindingFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_pass_binding_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid WriteMaterial(const char* name) {
			str virtualPath = str("project://Assets/") + name;
			EXPECT_TRUE(FileSystem::WriteTextFile(
				Path(virtualPath),
				str(R"({"shader": "0000000000000001-0000000000000002", "floats": {}, "colors": {}, "textures": {}})")));
			Asset* material = AssetDatabase::LoadAsset(Path(virtualPath));
			EXPECT_NE(material, nullptr);
			if (!material) {
				return Guid{};
			}
			Guid guid = material->assetId;
			AssetDatabase::UnloadAsset(material);
			return guid;
		}

		void WriteRow(const char* name, const Guid& material) {
			str document = str(R"({"_type": "ObjectPass", "point": "AfterOpaque", "overrideMaterial": )") +
						   str(R"({"_handle": true, "pointeeType": "MaterialAsset", "assetId": ")") +
						   material.ToString() + str(R"("}})");
			EXPECT_TRUE(FileSystem::WriteTextFile(Path(str("project://Assets/") + name), document));
		}

		std::filesystem::path _root;
	};

	TEST_F(RenderPassBindingFixture, RowBindsUnloadedReferences) {
		Guid material = WriteMaterial("Highlight.mat");
		ASSERT_TRUE(material.IsValid());
		ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

		WriteRow("Cold.slpass", material);
		Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Cold.slpass"));
		ASSERT_NE(loaded, nullptr);
		EXPECT_TRUE(static_cast<ObjectPass*>(loaded)->overrideMaterial.IsValid());
	}

	TEST_F(RenderPassBindingFixture, ARowsBindingSurvivesTheCollector) {
		Guid material = WriteMaterial("Held.mat");
		ASSERT_TRUE(material.IsValid());
		ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

		WriteRow("Held.slpass", material);
		Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Held.slpass"));
		ASSERT_NE(loaded, nullptr);
		ASSERT_TRUE(static_cast<ObjectPass*>(loaded)->overrideMaterial.IsValid());

		AssetDatabase::CollectGarbage();
		EXPECT_TRUE(static_cast<ObjectPass*>(loaded)->overrideMaterial.IsValid());
	}

	TEST_F(RenderPassBindingFixture, UnloadingARowReleasesWhatItBound) {
		Guid material = WriteMaterial("Released.mat");
		ASSERT_TRUE(material.IsValid());
		ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

		WriteRow("Released.slpass", material);
		Asset* loaded = AssetDatabase::LoadAsset(Path("project://Assets/Released.slpass"));
		ASSERT_NE(loaded, nullptr);
		Handle<Asset> witness(static_cast<ObjectPass*>(loaded)->overrideMaterial.Get());
		ASSERT_TRUE(witness.IsValid());

		AssetDatabase::UnloadAsset(loaded);
		AssetDatabase::CollectGarbage();
		EXPECT_EQ(witness.Get(), nullptr);
	}

} // namespace
