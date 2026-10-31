#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Asset/TextAsset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ObjectPass.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/PakReader.h"

#include <atomic>
#include <filesystem>

using namespace Shalltear;

namespace {

	struct Cooked {
		CookOps::PakAssetSource source;
		Vector<u8> package{GetMemoryResource(MemTag::Editor)};
		u32 stringCount = 0;
	};

	class AssetCookFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			_dir = std::filesystem::temp_directory_path() / "shalltear_asset_cook_test";
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
			std::filesystem::create_directories(_dir, ec);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
		}

		Path FilePath(const char* name) const { return Path((_dir / name).generic_string()); }

		template <typename T>
		::testing::AssertionResult RoundTrip(const T& original, T& restored, Cooked& cooked) {
			PakStringPool strings;
			str error;
			if (!CookOps::CookAsset(original, strings, cooked.source, error)) {
				return ::testing::AssertionFailure() << "CookAsset: " << error;
			}
			cooked.stringCount = strings.Count();

			CookOps::PakWriteDesc desc;
			desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
			CookOps::PakAssetSource packaged = cooked.source;
			packaged.dependencies.clear();
			Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
			sources.push_back(std::move(packaged));
			if (!CookOps::BuildPackage(sources, desc, cooked.package, error)) {
				return ::testing::AssertionFailure() << "BuildPackage: " << error;
			}

			Path path = FilePath("Round.slpak");
			Buffer buffer(cooked.package.size(), MemTag::Editor);
			std::memcpy(buffer.data, cooked.package.data(), cooked.package.size());
			if (!FileSystem::WriteFile(path, buffer)) {
				return ::testing::AssertionFailure() << "couldn't stage the package";
			}

			PakReader reader;
			if (!reader.Mount(path, error)) {
				return ::testing::AssertionFailure() << "Mount: " << error;
			}
			u32 index = reader.FindAsset(original.assetId);
			if (index == PAK_INVALID_INDEX) {
				return ::testing::AssertionFailure();
			}
			const PakAssetEntry* entry = reader.GetAsset(index);
			if (!entry || entry->typeIndex >= reader.GetTypeCount()) {
				return ::testing::AssertionFailure() << "asset entry is unreadable";
			}
			if (reader.GetType(entry->typeIndex)->typeNameHash != original.GetAssetTypeId().GetValue()) {
				return ::testing::AssertionFailure() << "package names another type than the asset";
			}

			CookReader descriptor = reader.GetDescriptor(index);
			if (descriptor.Size() != cooked.source.descriptor.size()) {
				return ::testing::AssertionFailure()
					   << "descriptor is " << descriptor.Size() << " bytes on disk and "
					   << cooked.source.descriptor.size() << " as cooked";
			}
			if (!cooked.source.descriptor.empty()) {
				const u8* bytes = descriptor.ReadSpan(descriptor.Size());
				if (!bytes || std::memcmp(bytes, cooked.source.descriptor.data(), descriptor.Size()) != 0) {
					return ::testing::AssertionFailure() << "descriptor read back different bytes";
				}
			}

			Buffer payloadBytes;
			if (cooked.source.payload.empty()) {
				if (entry->regionIndex != PAK_NO_REGION) {
					return ::testing::AssertionFailure() << "an asset with no payload names a region";
				}
			} else {
				if (entry->regionIndex == PAK_NO_REGION) {
					return ::testing::AssertionFailure() << "an asset with a payload names no region";
				}
				if (!reader.ReadRegion(entry->regionIndex, payloadBytes, error)) {
					return ::testing::AssertionFailure() << "ReadRegion: " << error;
				}
				if (payloadBytes.size != cooked.source.payload.size() ||
					std::memcmp(payloadBytes.data, cooked.source.payload.data(), payloadBytes.size) != 0) {
					return ::testing::AssertionFailure() << "payload read back different bytes";
				}
			}

			CookReader descriptorCursor = reader.GetDescriptor(index);
			CookReader payloadCursor(payloadBytes.data, payloadBytes.size);
			if (!restored.LoadCooked(descriptorCursor, payloadCursor)) {
				return ::testing::AssertionFailure() << "LoadCooked refused what Cook wrote";
			}
			return ::testing::AssertionSuccess();
		}

		std::filesystem::path _dir;
	};

	void ExpectNodesEqual(const UIDocumentNode& a, const UIDocumentNode& b, const str& where) {
		EXPECT_EQ(a.tag, b.tag) << where;
		EXPECT_EQ(a.id, b.id) << where;
		EXPECT_EQ(a.style, b.style) << where;
		EXPECT_EQ(a.text, b.text) << where;
		EXPECT_EQ(a.templateName, b.templateName) << where;
		ASSERT_EQ(a.classCount, b.classCount) << where;
		for (u8 i = 0; i < a.classCount; ++i) {
			EXPECT_EQ(a.classes[i], b.classes[i]) << where << " class " << static_cast<int>(i);
		}
		ASSERT_EQ(a.children.size(), b.children.size()) << where;
		for (size_t i = 0; i < a.children.size(); ++i) {
			ExpectNodesEqual(a.children[i], b.children[i], where + "/" + a.children[i].tag);
		}
	}

} // anonymous namespace

TEST_F(AssetCookFixture, TextAssetRoundTripsThroughReflectedDefault) {
	TextAsset original;
	original.assetId = Guid(0x1111, 0x2222);
	original.virtualPath = "project://Assets/Readme.txt";
	original.content = "the quick brown fox\njumped over the lazy dog\n";

	TextAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.content, original.content);
	EXPECT_EQ(restored.assetId, original.assetId);
	EXPECT_EQ(restored.virtualPath, original.virtualPath);
	EXPECT_TRUE(cooked.source.payload.empty());
}

TEST_F(AssetCookFixture, StyleSheetRoundTripsAsText) {
	StyleSheetAsset original;
	original.assetId = Guid(0x3333, 0x4444);
	original.virtualPath = "project://UI/Main.slss";
	original.source = ".row { flex-direction: row; padding: 4px; }\n.row:hover { background-color: #ff0000; }\n";

	StyleSheetAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.source, original.source);
	EXPECT_TRUE(cooked.source.payload.empty());
}

TEST_F(AssetCookFixture, MeshAssetRoundTripsFieldByField) {
	MeshAsset original;
	original.assetId = Guid(0x5555, 0x6666);
	original.virtualPath = "project://Assets/Rock.primitive";
	for (u32 i = 0; i < 64; ++i) {
		MeshVertex vertex;
		vertex.position = Vec3(static_cast<f32>(i), static_cast<f32>(i) * 2.0f, static_cast<f32>(i) * 3.0f);
		vertex.u = static_cast<f32>(i) * 0.01f;
		vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
		vertex.v = static_cast<f32>(i) * 0.02f;
		vertex.tangent = Vec3(1.0f, 0.0f, 0.0f);
		vertex.tangentSign = (i % 2) ? 1.0f : -1.0f;
		vertex.color = Color32(static_cast<u8>(i), 20, 30, 255);
		original.vertices.push_back(vertex);
	}
	for (u32 i = 0; i < 96; ++i) {
		original.indices.push_back(i % 64);
	}
	for (u32 s = 0; s < 3; ++s) {
		SubMesh submesh;
		submesh.firstIndex = s * 32;
		submesh.indexCount = 32;
		submesh.materialSlot = s;
		submesh.bounds = AABB(Vec3(static_cast<f32>(s), -1.0f, -2.0f), Vec3(static_cast<f32>(s) + 4.0f, 1.0f, 2.0f));
		original.submeshes.push_back(submesh);
	}
	original.bounds = AABB(Vec3(0.0f, -1.0f, -2.0f), Vec3(7.0f, 1.0f, 2.0f));

	MeshAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.bounds.min, original.bounds.min);
	EXPECT_EQ(restored.bounds.max, original.bounds.max);

	ASSERT_EQ(restored.vertices.size(), original.vertices.size());
	for (size_t i = 0; i < original.vertices.size(); ++i) {
		const MeshVertex& a = original.vertices[i];
		const MeshVertex& b = restored.vertices[i];
		EXPECT_EQ(b.position, a.position) << "vertex " << i;
		EXPECT_EQ(b.u, a.u) << "vertex " << i;
		EXPECT_EQ(b.normal, a.normal) << "vertex " << i;
		EXPECT_EQ(b.v, a.v) << "vertex " << i;
		EXPECT_EQ(b.tangent, a.tangent) << "vertex " << i;
		EXPECT_EQ(b.tangentSign, a.tangentSign) << "vertex " << i;
		EXPECT_EQ(b.color.r, a.color.r) << "vertex " << i;
		EXPECT_EQ(b.color.g, a.color.g) << "vertex " << i;
		EXPECT_EQ(b.color.b, a.color.b) << "vertex " << i;
		EXPECT_EQ(b.color.a, a.color.a) << "vertex " << i;
	}

	ASSERT_EQ(restored.indices.size(), original.indices.size());
	for (size_t i = 0; i < original.indices.size(); ++i) {
		EXPECT_EQ(restored.indices[i], original.indices[i]) << "index " << i;
	}

	ASSERT_EQ(restored.submeshes.size(), original.submeshes.size());
	for (size_t i = 0; i < original.submeshes.size(); ++i) {
		const SubMesh& a = original.submeshes[i];
		const SubMesh& b = restored.submeshes[i];
		EXPECT_EQ(b.firstIndex, a.firstIndex) << "submesh " << i;
		EXPECT_EQ(b.indexCount, a.indexCount) << "submesh " << i;
		EXPECT_EQ(b.materialSlot, a.materialSlot) << "submesh " << i;
		EXPECT_EQ(b.bounds.min, a.bounds.min) << "submesh " << i;
		EXPECT_EQ(b.bounds.max, a.bounds.max) << "submesh " << i;
		EXPECT_TRUE(b.bounds.IsValid()) << "submesh " << i;
	}

	EXPECT_EQ(cooked.source.payload.size(), original.vertices.size() * sizeof(MeshVertex) +
												original.indices.size() * sizeof(u32) +
												original.skinInfluences.size() * sizeof(MeshSkinInfluence));
	EXPECT_EQ(cooked.source.descriptor.size(), 6 * sizeof(f32) + 5 * sizeof(u32) + 3 * sizeof(SubMesh));
}

TEST_F(AssetCookFixture, TextureAssetRoundTripsPixelsAndSettings) {
	TextureAsset original;
	original.assetId = Guid(0x7777, 0x8888);
	original.virtualPath = "project://Assets/Brick.png";
	original.size = Vec2I(8, 4);
	original.format = RHI::Format::RGBA8SRGB;
	original.settings.sRGB = false;
	original.settings.generateMips = false;
	original.settings.filter = RHI::FilterMode::Nearest;
	original.settings.addressMode = RHI::AddressMode::MirroredRepeat;
	for (u32 i = 0; i < 8 * 4 * 4; ++i) {
		original.pixels.push_back(static_cast<u8>(i * 7));
	}

	TextureAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.size, original.size);
	EXPECT_EQ(restored.format, original.format);
	EXPECT_EQ(restored.settings.sRGB, original.settings.sRGB);
	EXPECT_EQ(restored.settings.generateMips, original.settings.generateMips);
	EXPECT_EQ(restored.settings.filter, original.settings.filter);
	EXPECT_EQ(restored.settings.addressMode, original.settings.addressMode);
	ASSERT_EQ(restored.pixels.size(), original.pixels.size());
	EXPECT_EQ(std::memcmp(restored.pixels.data(), original.pixels.data(), original.pixels.size()), 0);

	EXPECT_EQ(cooked.source.payload.size(), original.pixels.size());
}

TEST_F(AssetCookFixture, FontAssetRoundTripsItsBytesVerbatim) {
	FontAsset original;
	original.assetId = Guid(0x9999, 0xAAAA);
	original.virtualPath = "project://Fonts/Inter.ttf";
	for (u32 i = 0; i < 5000; ++i) {
		original.data.push_back(static_cast<u8>(i * 31 + 7));
	}

	FontAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	ASSERT_EQ(restored.data.size(), original.data.size());
	EXPECT_EQ(std::memcmp(restored.data.data(), original.data.data(), original.data.size()), 0);
	EXPECT_TRUE(cooked.source.descriptor.empty());
	EXPECT_EQ(cooked.source.payload.size(), original.data.size());
}

TEST_F(AssetCookFixture, EmptyFontNotCookedOrLoaded) {
	FontAsset empty;
	empty.assetId = Guid(0x9999, 0xAAAB);
	PakStringPool strings;
	CookOps::PakAssetSource source;
	str error;
	EXPECT_FALSE(CookOps::CookAsset(empty, strings, source, error));

	FontAsset loaded;
	CookReader descriptor;
	CookReader payload;
	EXPECT_FALSE(loaded.LoadCooked(descriptor, payload));
}

TEST_F(AssetCookFixture, MaterialAssetRoundTripsItsParamTable) {
	MaterialAsset original;
	original.assetId = Guid(0xBBBB, 0xCCCC);
	original.virtualPath = "project://Assets/Brick.mat";
	original.shaderGuid = Guid(0xDEAD, 0xBEEF);
	original.SetFloat("_Metallic", 0.25f);
	original.SetFloat("_Roughness", 0.75f);
	original.SetColor("_BaseColor", Color(0.1f, 0.2f, 0.3f, 0.4f));
	original.SetTexture("_AlbedoMap", Guid(0x1234, 0x5678));
	original.SetTexture("_NormalMap", Guid(0x8765, 0x4321));

	MaterialAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.shaderGuid, original.shaderGuid);

	EXPECT_EQ(cooked.source.dependencies.size(), 3u);

	ASSERT_EQ(restored.params.size(), original.params.size());
	for (size_t i = 0; i < original.params.size(); ++i) {
		EXPECT_EQ(restored.params[i].name, original.params[i].name);
		EXPECT_EQ(restored.params[i].kind, original.params[i].kind);
		for (u32 c = 0; c < UniformMemberFloatCount(original.params[i].kind); ++c) {
			EXPECT_EQ(restored.params[i].numbers[c], original.params[i].numbers[c]) << "param " << i << " component " << c;
		}
		EXPECT_EQ(restored.params[i].integer, original.params[i].integer);
		EXPECT_EQ(restored.params[i].hash, StringHash(original.params[i].name));
	}
	ASSERT_EQ(restored.textures.size(), original.textures.size());
	for (size_t i = 0; i < original.textures.size(); ++i) {
		EXPECT_EQ(restored.textures[i].name, original.textures[i].name);
		EXPECT_EQ(restored.textures[i].texture, original.textures[i].texture);
		EXPECT_EQ(restored.textures[i].hash, StringHash(original.textures[i].name));
	}

	EXPECT_TRUE(cooked.source.payload.empty());
	EXPECT_GT(cooked.source.descriptor.size(), 0u);
	EXPECT_EQ(cooked.source.recordVersion, PAK_RECORD_VERSION);

	EXPECT_EQ(cooked.stringCount, 5u);
	EXPECT_EQ(cooked.source.descriptor.size(),
			  sizeof(Guid) + 2 * sizeof(u32) +
				  2 * (sizeof(u32) + sizeof(u8) + sizeof(f32)) +
				  1 * (sizeof(u32) + sizeof(u8) + 4 * sizeof(f32)) +
				  2 * (sizeof(u32) + sizeof(Guid)) +
				  sizeof(u32) +
				  4 * sizeof(u8) + sizeof(f32) + sizeof(i32));
}

TEST_F(AssetCookFixture, InterningIsPerPackageAcrossMaterials) {
	MaterialAsset first;
	first.assetId = Guid(1, 1);
	first.virtualPath = "project://Assets/A.mat";
	first.SetFloat("_Metallic", 0.5f);
	first.SetColor("_BaseColor", Color::White());

	MaterialAsset second;
	second.assetId = Guid(2, 2);
	second.virtualPath = "project://Assets/B.mat";
	second.SetFloat("_Metallic", 0.9f);
	second.SetColor("_BaseColor", Color::Black());
	second.SetFloat("_Roughness", 0.1f);

	PakStringPool strings;
	CookOps::PakAssetSource sourceA;
	CookOps::PakAssetSource sourceB;
	str error;
	ASSERT_TRUE(CookOps::CookAsset(first, strings, sourceA, error)) << error;
	ASSERT_TRUE(CookOps::CookAsset(second, strings, sourceB, error)) << error;

	ASSERT_EQ(strings.Count(), 3u);
	EXPECT_EQ(strings.Texts()[0], "_Metallic");
	EXPECT_EQ(strings.Texts()[1], "_BaseColor");
	EXPECT_EQ(strings.Texts()[2], "_Roughness");

	CookOps::PakWriteDesc desc;
	desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	sources.push_back(std::move(sourceA));
	sources.push_back(std::move(sourceB));
	Path path = FilePath("Materials.slpak");
	ASSERT_TRUE(CookOps::WritePackage(path, sources, desc, error)) << error;

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	ASSERT_EQ(reader.GetStringCount(), 3u);

	for (const MaterialAsset* original : {&first, &second}) {
		u32 index = reader.FindAsset(original->assetId);
		ASSERT_NE(index, PAK_INVALID_INDEX);
		MaterialAsset restored;
		CookReader descriptor = reader.GetDescriptor(index);
		CookReader payload;
		ASSERT_TRUE(restored.LoadCooked(descriptor, payload));
		ASSERT_EQ(restored.params.size(), original->params.size());
		for (size_t i = 0; i < original->params.size(); ++i) {
			EXPECT_EQ(restored.params[i].name, original->params[i].name);
			EXPECT_EQ(restored.params[i].kind, original->params[i].kind);
			EXPECT_EQ(restored.params[i].numbers[0], original->params[i].numbers[0]);
		}
	}
}

TEST_F(AssetCookFixture, ShaderAssetRoundTripsSpirvAndInterface) {
	ShaderAsset original;
	original.assetId = Guid(0xEEEE, 0xFFFF);
	original.virtualPath = "engine://Shaders/Lit.hlsl";

	original.keywords.push_back(str("USE_SECOND"));
	ShaderVariant base;
	for (u32 i = 0; i < 512; ++i) {
		base.vertexSpirv.push_back(static_cast<u8>(i));
	}
	for (u32 i = 0; i < 768; ++i) {
		base.pixelSpirv.push_back(static_cast<u8>(255 - (i & 0xFF)));
	}
	base.keywordHash = ShaderAsset::KeywordSetHash({});
	original.variants.push_back(std::move(base));

	ShaderVariant featured;
	featured.keywords.push_back(str("USE_SECOND"));
	featured.keywordHash = ShaderAsset::KeywordSetHash({featured.keywords.data(), featured.keywords.size()});
	for (u32 i = 0; i < 300; ++i) {
		featured.vertexSpirv.push_back(static_cast<u8>(i * 3));
	}
	for (u32 i = 0; i < 100; ++i) {
		featured.pixelSpirv.push_back(static_cast<u8>(i * 7));
	}
	for (u32 i = 0; i < 64; ++i) {
		featured.depthVertexSpirv.push_back(static_cast<u8>(i ^ 0x5A));
	}
	for (u32 i = 0; i < 48; ++i) {
		featured.depthPixelSpirv.push_back(static_cast<u8>(i + 11));
	}
	for (u32 i = 0; i < 40; ++i) {
		featured.depthClipPixelSpirv.push_back(static_cast<u8>(i * 5));
	}
	for (u32 i = 0; i < 36; ++i) {
		featured.depthValuePixelSpirv.push_back(static_cast<u8>(i * 7 + 3));
	}
	original.variants.push_back(std::move(featured));

	original.pushConstantSize = 64;
	original.renderState.blend = RHI::BlendMode::Alpha;
	original.renderState.depth = RHI::DepthMode::TestOnly;
	original.renderState.cull = RHI::CullMode::Front;
	original.renderState.topology = RHI::PrimitiveTopology::LineList;
	original.renderState.fill = RHI::FillMode::Wireframe;
	original.renderState.depthBiasConstant = 1.5f;
	original.renderState.depthBiasSlope = -2.25f;
	original.renderState.depthCompare = RHI::DepthCompare::Farther;

	original.bindings.push_back({0, 0, RHI::DescriptorType::UniformBuffer, RHI::ShaderStage::Vertex});
	original.bindings.push_back({1, 0, RHI::DescriptorType::UniformBuffer,
								 RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel});
	original.bindings.push_back({1, 1, RHI::DescriptorType::SampledTexture, RHI::ShaderStage::Pixel});

	UniformBlockLayout block;
	block.set = MATERIAL_DESCRIPTOR_SET;
	block.binding = 0;
	block.sizeBytes = 32;
	block.members.push_back({str("_BaseColor"), UniformMemberType::Vec4, 0, 16});
	block.members.push_back({str("_Metallic"), UniformMemberType::Float, 16, 4});
	block.members.push_back({str("_Roughness"), UniformMemberType::Float, 20, 4});
	original.uniformBlocks.push_back(std::move(block));

	original.resources.push_back({0, 0, RHI::DescriptorType::UniformBuffer, str("ViewData")});
	original.resources.push_back({1, 0, RHI::DescriptorType::UniformBuffer, str("MaterialParams")});
	original.resources.push_back({1, 1, RHI::DescriptorType::SampledTexture, str("_AlbedoMap")});

	ShaderMemberDefault declared;
	declared.name = str("_BaseColor");
	declared.count = 4;
	declared.numbers[0] = 1.0f;
	declared.numbers[3] = 0.5f;
	original.memberDefaults.push_back(std::move(declared));
	original.textureDefaults.push_back({"_AlbedoMap", ShaderTextureFallback::Normal});

	ShaderAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	ASSERT_EQ(restored.variants.size(), original.variants.size());
	size_t payloadBytes = 0;
	for (size_t v = 0; v < original.variants.size(); ++v) {
		const ShaderVariant& expected = original.variants[v];
		const ShaderVariant& actual = restored.variants[v];
		EXPECT_EQ(actual.keywordHash, expected.keywordHash) << "variant " << v;
		ASSERT_EQ(actual.keywords.size(), expected.keywords.size()) << "variant " << v;
		for (size_t k = 0; k < expected.keywords.size(); ++k) {
			EXPECT_EQ(actual.keywords[k], expected.keywords[k]) << "variant " << v;
		}
		ASSERT_EQ(actual.vertexSpirv.size(), expected.vertexSpirv.size()) << "variant " << v;
		EXPECT_EQ(std::memcmp(actual.vertexSpirv.data(), expected.vertexSpirv.data(), expected.vertexSpirv.size()), 0) << "variant " << v;
		ASSERT_EQ(actual.pixelSpirv.size(), expected.pixelSpirv.size()) << "variant " << v;
		EXPECT_EQ(std::memcmp(actual.pixelSpirv.data(), expected.pixelSpirv.data(), expected.pixelSpirv.size()), 0) << "variant " << v;
		EXPECT_EQ(actual.depthVertexSpirv, expected.depthVertexSpirv) << "variant " << v;
		EXPECT_EQ(actual.depthPixelSpirv, expected.depthPixelSpirv) << "variant " << v;
		EXPECT_EQ(actual.depthClipPixelSpirv, expected.depthClipPixelSpirv) << "variant " << v;
		EXPECT_EQ(actual.depthValuePixelSpirv, expected.depthValuePixelSpirv) << "variant " << v;
		EXPECT_TRUE(actual.computeSpirv.empty()) << "variant " << v;
		payloadBytes += expected.vertexSpirv.size() + expected.pixelSpirv.size() +
						expected.depthVertexSpirv.size() + expected.depthPixelSpirv.size() + expected.depthClipPixelSpirv.size() +
						expected.depthValuePixelSpirv.size();
	}
	EXPECT_FALSE(restored.IsKernel());
	EXPECT_EQ(cooked.source.payload.size(), payloadBytes);

	ASSERT_EQ(restored.keywords.size(), original.keywords.size());
	EXPECT_EQ(restored.keywords[0], original.keywords[0]);

	EXPECT_EQ(restored.pushConstantSize, original.pushConstantSize);
	EXPECT_EQ(restored.renderState.blend, original.renderState.blend);
	EXPECT_EQ(restored.renderState.depth, original.renderState.depth);
	EXPECT_EQ(restored.renderState.cull, original.renderState.cull);
	EXPECT_EQ(restored.renderState.topology, original.renderState.topology);
	EXPECT_EQ(restored.renderState.fill, original.renderState.fill);
	EXPECT_EQ(restored.renderState.depthBiasConstant, original.renderState.depthBiasConstant);
	EXPECT_EQ(restored.renderState.depthBiasSlope, original.renderState.depthBiasSlope);
	EXPECT_EQ(restored.renderState.depthCompare, original.renderState.depthCompare);

	ASSERT_EQ(restored.bindings.size(), original.bindings.size());
	for (size_t i = 0; i < original.bindings.size(); ++i) {
		EXPECT_EQ(restored.bindings[i].set, original.bindings[i].set) << "binding " << i;
		EXPECT_EQ(restored.bindings[i].binding, original.bindings[i].binding) << "binding " << i;
		EXPECT_EQ(restored.bindings[i].type, original.bindings[i].type) << "binding " << i;
		EXPECT_EQ(restored.bindings[i].stages, original.bindings[i].stages) << "binding " << i;
	}

	ASSERT_EQ(restored.uniformBlocks.size(), original.uniformBlocks.size());
	for (size_t b = 0; b < original.uniformBlocks.size(); ++b) {
		const UniformBlockLayout& expected = original.uniformBlocks[b];
		const UniformBlockLayout& actual = restored.uniformBlocks[b];
		EXPECT_EQ(actual.set, expected.set);
		EXPECT_EQ(actual.binding, expected.binding);
		EXPECT_EQ(actual.sizeBytes, expected.sizeBytes);
		ASSERT_EQ(actual.members.size(), expected.members.size());
		for (size_t m = 0; m < expected.members.size(); ++m) {
			EXPECT_EQ(actual.members[m].name, expected.members[m].name) << "member " << m;
			EXPECT_EQ(actual.members[m].type, expected.members[m].type) << "member " << m;
			EXPECT_EQ(actual.members[m].offset, expected.members[m].offset) << "member " << m;
			EXPECT_EQ(actual.members[m].sizeBytes, expected.members[m].sizeBytes) << "member " << m;
		}
	}

	ASSERT_EQ(restored.resources.size(), original.resources.size());
	for (size_t i = 0; i < original.resources.size(); ++i) {
		EXPECT_EQ(restored.resources[i].set, original.resources[i].set) << "resource " << i;
		EXPECT_EQ(restored.resources[i].binding, original.resources[i].binding) << "resource " << i;
		EXPECT_EQ(restored.resources[i].type, original.resources[i].type) << "resource " << i;
		EXPECT_EQ(restored.resources[i].name, original.resources[i].name) << "resource " << i;
	}

	ASSERT_EQ(restored.memberDefaults.size(), 1u);
	EXPECT_EQ(restored.memberDefaults[0].name, "_BaseColor");
	EXPECT_EQ(restored.memberDefaults[0].count, 4u);
	EXPECT_FLOAT_EQ(restored.memberDefaults[0].numbers[0], 1.0f);
	EXPECT_FLOAT_EQ(restored.memberDefaults[0].numbers[3], 0.5f);
	EXPECT_EQ(restored.GetTextureFallback("_AlbedoMap"), ShaderTextureFallback::Normal);

	const UniformBlockLayout* layout = restored.GetMaterialParamLayout();
	ASSERT_NE(layout, nullptr);
	EXPECT_EQ(layout->members.size(), 3u);
	Vector<str> slots{GetMemoryResource(MemTag::Asset)};
	restored.GetMaterialTextureSlots(slots);
	ASSERT_EQ(slots.size(), 1u);
	EXPECT_EQ(slots[0], "_AlbedoMap");

	EXPECT_EQ(cooked.stringCount, 7u);
}

TEST_F(AssetCookFixture, KernelRoundTripsStageAndGroupSize) {
	ShaderAsset original;
	original.assetId = Guid(0xEEEE, 0xAAAA);
	original.virtualPath = "engine://Shaders/Kernel.hlsl";
	ShaderVariant base;
	base.keywordHash = ShaderAsset::KeywordSetHash({});
	for (u32 i = 0; i < 300; ++i) {
		base.computeSpirv.push_back(static_cast<u8>(i * 13));
	}
	original.variants.push_back(std::move(base));
	original.threadGroupSize = Vec3I{64, 2, 1};
	original.pushConstantSize = 16;
	original.bindings.push_back({0, 0, RHI::DescriptorType::StorageBuffer, RHI::ShaderStage::Compute});
	original.bindings.push_back({0, 1, RHI::DescriptorType::UniformBuffer, RHI::ShaderStage::Compute});

	ShaderAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	ASSERT_EQ(restored.variants.size(), 1u);
	EXPECT_EQ(restored.variants[0].computeSpirv, original.variants[0].computeSpirv);
	EXPECT_TRUE(restored.variants[0].vertexSpirv.empty());
	EXPECT_TRUE(restored.IsKernel());
	EXPECT_EQ(restored.threadGroupSize, original.threadGroupSize);
	EXPECT_EQ(cooked.source.payload.size(), original.variants[0].computeSpirv.size());
	ASSERT_EQ(restored.bindings.size(), 2u);
	EXPECT_EQ(restored.bindings[0].type, RHI::DescriptorType::StorageBuffer);
	EXPECT_EQ(restored.bindings[0].stages, RHI::ShaderStage::Compute);
}

TEST_F(AssetCookFixture, UIDocumentRoundTripsAsText) {
	UIDocumentAsset original;
	original.assetId = Guid(0x0101, 0x0202);
	original.virtualPath = "project://UI/Hud.slxml";
	str error;
	ASSERT_TRUE(original.Parse(R"(<ui version="1">
	<panel id="root" class="hud dark" style="flex-grow: 1;">
		<row>Score</row>
		<row id="value">0</row>
	</panel>
</ui>)",
							   error))
		<< error;

	UIDocumentAsset restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	ExpectNodesEqual(original.root, restored.root, str("root"));
	EXPECT_TRUE(restored.sheets.empty());
	EXPECT_TRUE(restored.templates.empty());

	EXPECT_EQ(cooked.source.descriptor.size(), 2 * sizeof(u32));
	EXPECT_EQ(cooked.source.payload.size(), original.Write().size());
}

TEST_F(AssetCookFixture, UIDocumentRefusesUnresolvedReference) {
	UIDocumentAsset original;
	original.assetId = Guid(0x0303, 0x0404);
	original.virtualPath = "project://UI/Hud.slxml";
	str error;
	ASSERT_TRUE(original.Parse(R"(<ui version="1">
	<sheet src="project://UI/Main.slss"/>
	<panel id="root"/>
</ui>)",
							   error))
		<< error;
	ASSERT_EQ(original.sheets.size(), 1u);
	ASSERT_FALSE(original.sheets[0].sheet.IsValid());

	PakStringPool strings;
	CookOps::PakAssetSource source;
	str cookError;
	EXPECT_FALSE(CookOps::CookAsset(original, strings, source, cookError));
}

TEST_F(AssetCookFixture, ObjectPassRoundTripsPlacementAndFilter) {
	ObjectPass original;
	original.assetId = Guid(0x0505, 0x0606);
	original.virtualPath = "project://Rendering/Outline.slpass";
	original.point = PassPoint::BeforeTransparent;
	original.position = FrameStagePosition::Late;
	original.renderingLayerMask = 0x0000000Cu;

	ObjectPass restored;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, restored, cooked));

	EXPECT_EQ(restored.point, PassPoint::BeforeTransparent);
	EXPECT_EQ(restored.position, FrameStagePosition::Late);
	EXPECT_EQ(restored.renderingLayerMask, 0x0000000Cu);
	EXPECT_EQ(restored.assetId, original.assetId);
	EXPECT_TRUE(cooked.source.payload.empty());

	EXPECT_EQ(original.GetAssetTypeId(), StringHash("ObjectPass"));
}

class AssetCookBindingFixture : public AssetCookFixture {
protected:
	void SetUp() override {
		AssetCookFixture::SetUp();
		FileSystem::Mount("project", Path(_dir.generic_string()));
		AssetDatabase::Initialize();
	}

	void TearDown() override {
		AssetDatabase::Terminate();
		FileSystem::Unmount("project");
		AssetCookFixture::TearDown();
	}
};

TEST_F(AssetCookBindingFixture, CookedRowBindsMaterialRecordNames) {
	ASSERT_TRUE(FileSystem::WriteTextFile(
		Path("project://Override.mat"),
		str(R"({"shader": "0000000000000001-0000000000000002", "floats": {}, "colors": {}, "textures": {}})")));
	Asset* material = AssetDatabase::LoadAsset(Path("project://Override.mat"));
	ASSERT_NE(material, nullptr);

	ObjectPass original;
	original.assetId = Guid(0x0707, 0x0808);
	original.virtualPath = "project://Rendering/Bound.slpass";
	original.overrideMaterial = Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));

	ObjectPass staged;
	Cooked cooked;
	ASSERT_TRUE(RoundTrip(original, staged, cooked));

	str error;
	ASSERT_TRUE(AssetDatabase::MountPackage(FilePath("Round.slpak"), error)) << error;
	auto* restored = static_cast<ObjectPass*>(AssetDatabase::LoadAssetByGuid(original.assetId));
	ASSERT_NE(restored, nullptr);
	ASSERT_TRUE(restored->overrideMaterial.IsValid());

	AssetDatabase::UnloadAsset(material);
	AssetDatabase::CollectGarbage();
	EXPECT_TRUE(restored->overrideMaterial.IsValid());
}

class CookPackageFixture : public ::testing::Test {
protected:
	void SetUp() override {
		static std::atomic<u64> counter{0};
		_root = std::filesystem::temp_directory_path() /
				("shalltear_cookpkg_" + std::to_string(counter.fetch_add(1)));
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
		std::filesystem::create_directories(_root, ec);
		FileSystem::Mount("project", Path(_root.generic_string()));
		AssetDatabase::Initialize();
	}

	void TearDown() override {
		AssetDatabase::Terminate();
		FileSystem::Unmount("project");
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
	}

	void StageText(const char* relative, std::string_view text) const {
		std::filesystem::path at = _root / relative;
		std::error_code ec;
		std::filesystem::create_directories(at.parent_path(), ec);
		ASSERT_TRUE(FileSystem::WriteTextFile(Path(at.generic_string()), str(text)));
	}

	void StageBytes(const char* relative, size_t count) const {
		std::filesystem::path at = _root / relative;
		std::error_code ec;
		std::filesystem::create_directories(at.parent_path(), ec);
		Buffer buffer(count, MemTag::Editor);
		for (size_t i = 0; i < count; ++i) {
			buffer.data[i] = static_cast<u8>(i * 13 + 5);
		}
		ASSERT_TRUE(FileSystem::WriteFile(Path(at.generic_string()), buffer));
	}

	std::filesystem::path _root;
};

TEST_F(CookPackageFixture, CooksListOfGuidsIntoReadablePackage) {
	StageText("Assets/Readme.txt", "hello from a cooked package");
	StageText("Assets/UI/Main.slss", ".row { flex-direction: row; }\n");
	StageText("Assets/Brick.mat", R"({
	"shader": "0000000000000000-0000000000000000",
	"floats": { "_Metallic": 0.5 },
	"colors": { "_BaseColor": [1, 0, 0, 1] },
	"textures": {}
})");
	StageText("Assets/Stone.mat", R"({
	"shader": "0000000000000000-0000000000000000",
	"floats": { "_Metallic": 0.9 },
	"colors": { "_BaseColor": [0, 1, 0, 1] },
	"textures": {}
})");
	StageBytes("Assets/Fonts/Inter.ttf", 3000);
	AssetDatabase::ScanProjectDirectory();

	const char* paths[] = {"project://Assets/Readme.txt", "project://Assets/UI/Main.slss",
						   "project://Assets/Brick.mat", "project://Assets/Stone.mat",
						   "project://Assets/Fonts/Inter.ttf"};
	Vector<Guid> guids{GetMemoryResource(MemTag::Editor)};
	for (const char* path : paths) {
		Guid guid = AssetDatabase::LookupGuid(Path(path));
		ASSERT_TRUE(guid.IsValid()) << path;
		guids.push_back(guid);
	}

	CookOps::PakWriteDesc desc;
	desc.targetId = 3;
	desc.strings.push_back(str("this table is the cook's, not the caller's"));

	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::CookPackage(CookOps::CookClosure::Of(guids), desc, package, error)) << error;

	std::filesystem::path file = _root / "Content.slpak";
	Buffer buffer(package.size(), MemTag::Editor);
	std::memcpy(buffer.data, package.data(), package.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(file.generic_string()), buffer));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(Path(file.generic_string()), error)) << error;
	EXPECT_EQ(reader.GetAssetCount(), 5u);
	EXPECT_EQ(reader.GetTypeCount(), 4u);
	EXPECT_EQ(reader.GetSuperblock().targetId, 3u);

	ASSERT_EQ(reader.GetStringCount(), 2u);
	EXPECT_EQ(reader.GetString(0), "_Metallic");
	EXPECT_EQ(reader.GetString(1), "_BaseColor");

	for (const Guid& guid : guids) {
		EXPECT_NE(reader.FindAsset(guid), PAK_INVALID_INDEX) << guid.ToString();
	}

	u32 index = reader.FindAsset(guids[0]);
	ASSERT_NE(index, PAK_INVALID_INDEX);
	TextAsset restored;
	CookReader descriptor = reader.GetDescriptor(index);
	CookReader payload;
	ASSERT_TRUE(restored.LoadCooked(descriptor, payload));
	EXPECT_EQ(restored.content, "hello from a cooked package");
}

TEST_F(CookPackageFixture, CookOrderDoesNotReachTheFile) {
	StageText("Assets/A.mat", R"({ "shader": "0000000000000000-0000000000000000",
		"floats": { "_Metallic": 0.5 }, "colors": {}, "textures": {} })");
	StageText("Assets/B.mat", R"({ "shader": "0000000000000000-0000000000000000",
		"floats": { "_Roughness": 0.5 }, "colors": {}, "textures": {} })");
	StageText("Assets/C.mat", R"({ "shader": "0000000000000000-0000000000000000",
		"floats": { "_Emissive": 0.5 }, "colors": {}, "textures": {} })");
	AssetDatabase::ScanProjectDirectory();

	Vector<Guid> forward{GetMemoryResource(MemTag::Editor)};
	for (const char* path : {"project://Assets/A.mat", "project://Assets/B.mat", "project://Assets/C.mat"}) {
		Guid guid = AssetDatabase::LookupGuid(Path(path));
		ASSERT_TRUE(guid.IsValid()) << path;
		forward.push_back(guid);
	}
	Vector<Guid> reversed{GetMemoryResource(MemTag::Editor)};
	reversed.assign(forward.rbegin(), forward.rend());

	CookOps::PakWriteDesc desc;
	Vector<u8> first{GetMemoryResource(MemTag::Editor)};
	Vector<u8> second{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::CookPackage(CookOps::CookClosure::Of(forward), desc, first, error)) << error;
	ASSERT_TRUE(CookOps::CookPackage(CookOps::CookClosure::Of(reversed), desc, second, error)) << error;

	ASSERT_EQ(first.size(), second.size());
	EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size()), 0);
}

namespace {

	class AcStatsComponent : public Component {
	public:
		f32 health = 100.0f;
		i32 damage = 10;
		bool shielded = false;
		str title = "default";
		Vec3 aim{};
		Guid loot{};

		SLT_REFLECT_COMPONENT(AcStatsComponent, Component)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(damage)
		SLT_REFLECT_FIELD(shielded)
		SLT_REFLECT_FIELD(title)
		SLT_REFLECT_FIELD(aim)
		SLT_REFLECT_FIELD(loot)
		SLT_END_REFLECT()

		AcStatsComponent() = default;
	};

	class AcRefComponent : public Component {
	public:
		Handle<SceneObject> target;
		Handle<AcStatsComponent> stats;
		Handle<TextAsset> asset;

		SLT_REFLECT_COMPONENT(AcRefComponent, Component)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(stats)
		SLT_REFLECT_FIELD(asset)
		SLT_END_REFLECT()

		AcRefComponent() = default;
	};

	class AcFragileComponent : public Component {
	public:
		i32 charges = 0;
		str sigil;

		SLT_REFLECT_COMPONENT(AcFragileComponent, Component)
		SLT_REFLECT_FIELD(charges)
		SLT_REFLECT_FIELD(sigil)
		SLT_END_REFLECT()

		AcFragileComponent() = default;
	};

	struct ScopedFragileType {
		ScopedFragileType() { TypeRegistry::Register(const_cast<TypeInfo*>(AcFragileComponent::GetReflectType())); }
		~ScopedFragileType() { TypeRegistry::Unregister(AcFragileComponent::GetReflectType()->name); }
	};

	SLT_REGISTER_TYPE(AcStatsComponent)
	SLT_REGISTER_TYPE(AcRefComponent)

	class DocumentCookFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_doccook_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
			std::filesystem::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Path Absolute(const char* virtualPath) const { return FileSystem::Resolve(Path(virtualPath)); }

		::testing::AssertionResult ExpectTextDocument(const char* virtualPath) const {
			Buffer bytes = FileSystem::ReadFile(Absolute(virtualPath), MemTag::Editor);
			if (!bytes || bytes.size == 0) {
				return ::testing::AssertionFailure() << virtualPath << " wasn't written";
			}
			if (bytes.data[0] != static_cast<u8>('{')) {
				return ::testing::AssertionFailure() << virtualPath << " is not a text document";
			}
			return ::testing::AssertionSuccess();
		}

		::testing::AssertionResult CookThrough(const Asset& original, Asset& restored, Buffer& outPayload) {
			PakStringPool strings;
			CookOps::PakAssetSource source;
			str error;
			if (!CookOps::CookAsset(original, strings, source, error)) {
				return ::testing::AssertionFailure() << "CookAsset: " << error;
			}
			CookOps::PakWriteDesc desc;
			Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> declared{GetMemoryResource(MemTag::Editor)};
			declared.assign(source.dependencies.begin(), source.dependencies.end());
			sources.push_back(std::move(source));
			for (const Guid& dependency : declared) {
				Asset* companion = AssetDatabase::LoadAssetByGuid(dependency);
				if (!companion) {
					return ::testing::AssertionFailure()
						   << "the cook declared " << dependency.ToString() << ", which nothing resolves";
				}
				CookOps::PakAssetSource companionSource;
				bool cooked = CookOps::CookAsset(*companion, strings, companionSource, error);
				AssetDatabase::UnloadAsset(companion);
				if (!cooked) {
					return ::testing::AssertionFailure() << "CookAsset (companion): " << error;
				}
				sources.push_back(std::move(companionSource));
			}
			desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
			Vector<u8> package{GetMemoryResource(MemTag::Editor)};
			if (!CookOps::BuildPackage(sources, desc, package, error)) {
				return ::testing::AssertionFailure() << "BuildPackage: " << error;
			}

			Path path((_root / "Round.slpak").generic_string());
			Buffer file(package.size(), MemTag::Editor);
			std::memcpy(file.data, package.data(), package.size());
			if (!FileSystem::WriteFile(path, file)) {
				return ::testing::AssertionFailure() << "couldn't stage the package";
			}

			PakReader reader;
			if (!reader.Mount(path, error)) {
				return ::testing::AssertionFailure() << "Mount: " << error;
			}
			u32 index = reader.FindAsset(original.assetId);
			if (index == PAK_INVALID_INDEX) {
				return ::testing::AssertionFailure() << "cooked asset isn't in the package";
			}
			const PakAssetEntry* entry = reader.GetAsset(index);
			if (!entry || entry->regionIndex == PAK_NO_REGION) {
				return ::testing::AssertionFailure() << "a cooked document names no payload region";
			}
			if (!reader.ReadRegion(entry->regionIndex, outPayload, error)) {
				return ::testing::AssertionFailure() << "ReadRegion: " << error;
			}
			CookReader descriptor = reader.GetDescriptor(index);
			if (descriptor.Size() != 0) {
				return ::testing::AssertionFailure();
			}
			CookReader payload(outPayload.data, outPayload.size);
			if (!restored.LoadCooked(descriptor, payload)) {
				return ::testing::AssertionFailure() << "LoadCooked refused what Cook wrote";
			}
			return ::testing::AssertionSuccess();
		}

		::testing::AssertionResult ExpectBinaryPayload(const Buffer& payload) const {
			if (!payload || payload.size < 4) {
				return ::testing::AssertionFailure() << "cooked payload is empty";
			}
			if (payload.data[0] == static_cast<u8>('{')) {
				return ::testing::AssertionFailure();
			}
			if (std::memcmp(payload.data, "SLTS", 4) != 0) {
				return ::testing::AssertionFailure() << "cooked payload isn't an SLTS document";
			}
			return ::testing::AssertionSuccess();
		}

		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;
	};

	Handle<Scene> BuildReferenceScene() {
		Handle<Scene> scene = SceneSystem::CreateScene("Level1");

		Handle<SceneObject> player = SceneSystem::CreateObject("Player", scene);
		player->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
		player->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));

		Handle<SceneObject> arm = SceneSystem::CreateObject("Arm", scene, player);
		Handle<SceneObject> shield = SceneSystem::CreateObject("Shield", scene, player);
		(void)shield;

		Handle<SceneObject> hand = SceneSystem::CreateObject("Hand", scene, arm);
		hand->SetActive(false);

		Handle<AcStatsComponent> stats = hand->AddComponent<AcStatsComponent>();
		stats->health = 42.5f;
		stats->damage = -3;
		stats->shielded = true;
		stats->title = "left hand";
		stats->aim = Vec3(0.5f, 0.25f, 0.125f);
		stats->loot = Guid{0x1111, 0x2222};

		FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/loot.txt")), "gold");
		TextAsset* text = static_cast<TextAsset*>(AssetDatabase::LoadAsset(Path("project://Assets/loot.txt")));
		Handle<AcRefComponent> refs = player->AddComponent<AcRefComponent>();
		refs->target = hand;
		refs->stats = stats;
		refs->asset = Handle<TextAsset>(text);
		return scene;
	}

	void VerifyReferenceScene(Handle<Scene> scene, const Guid& lootGuid) {
		ASSERT_TRUE(scene.IsValid());
		ASSERT_EQ(scene->GetRootCount(), 1u);
		EXPECT_EQ(scene->GetObjectCount(), 4u);

		Handle<SceneObject> player = scene->GetRoot(0);
		EXPECT_EQ(player->name, "Player");
		EXPECT_TRUE(Vec3(1.0f, 2.0f, 3.0f) == player->GetLocalPosition());
		EXPECT_TRUE(Vec3(2.0f, 2.0f, 2.0f) == player->GetLocalScale());

		ASSERT_EQ(player->GetChildCount(), 2u);
		Handle<SceneObject> arm = player->GetChild(0);
		EXPECT_EQ(arm->name, "Arm");
		EXPECT_EQ(player->GetChild(1)->name, "Shield");

		Handle<SceneObject> hand = arm->FindChildByName("Hand");
		ASSERT_TRUE(hand.IsValid());
		EXPECT_FALSE(hand->IsActiveSelf());

		Handle<AcStatsComponent> stats = hand->GetComponent<AcStatsComponent>();
		ASSERT_TRUE(stats.IsValid());
		EXPECT_FLOAT_EQ(stats->health, 42.5f);
		EXPECT_EQ(stats->damage, -3);
		EXPECT_TRUE(stats->shielded);
		EXPECT_EQ(stats->title, "left hand");
		EXPECT_TRUE(stats->aim == Vec3(0.5f, 0.25f, 0.125f));
		EXPECT_EQ(stats->loot, (Guid{0x1111, 0x2222}));

		Handle<AcRefComponent> refs = player->GetComponent<AcRefComponent>();
		ASSERT_TRUE(refs.IsValid());
		ASSERT_TRUE(refs->target.IsValid());
		EXPECT_EQ(refs->target.GetInstanceId(), hand.GetInstanceId());
		ASSERT_TRUE(refs->stats.IsValid());
		EXPECT_EQ(refs->stats.GetInstanceId(), stats.GetInstanceId());
		ASSERT_TRUE(refs->asset.IsValid());
		EXPECT_EQ(refs->asset->assetId, lootGuid);
	}

} // anonymous namespace

TEST_F(DocumentCookFixture, SceneCooksToBinary) {
	Handle<Scene> scene = BuildReferenceScene();
	Guid lootGuid = AssetDatabase::LookupGuid(Path("project://Assets/loot.txt"));
	ASSERT_TRUE(lootGuid.IsValid());

	const char* scenePath = "project://Assets/Level1.scene";
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path(scenePath), false));
	ASSERT_TRUE(ExpectTextDocument(scenePath));

	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path(scenePath));
	ASSERT_TRUE(guid.IsValid());
	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);

	SceneAsset restored;
	Buffer payload;
	ASSERT_TRUE(CookThrough(*loaded, restored, payload));
	EXPECT_TRUE(ExpectBinaryPayload(payload));

	restored.assetId = loaded->assetId;
	ASSERT_TRUE(restored.Export(Absolute("project://Exported.scene")));
	Buffer exported = FileSystem::ReadFile(Absolute("project://Exported.scene"), MemTag::Editor);
	ASSERT_TRUE(exported);
	ASSERT_EQ(exported.size, payload.size);
	EXPECT_EQ(std::memcmp(exported.data, payload.data, payload.size), 0);

	ASSERT_TRUE(FileSystem::WriteFile(Absolute("project://Assets/Cooked.scene"), payload));
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://Assets/Cooked.scene"));
	VerifyReferenceScene(reloaded, lootGuid);

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(DocumentCookFixture, PrefabCooksToBinary) {
	Handle<Scene> scene = SceneSystem::CreateScene("Stage");
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", scene);
	unit->SetLocalPosition(Vec3(3.0f, 4.0f, 5.0f));
	Handle<AcStatsComponent> stats = unit->AddComponent<AcStatsComponent>();
	stats->health = 12.5f;
	stats->damage = 7;
	stats->title = "unit";
	Handle<SceneObject> muzzle = SceneSystem::CreateObject("Muzzle", scene, unit);
	muzzle->SetLocalPosition(Vec3(0.0f, 0.0f, 1.5f));

	const char* prefabPath = "project://Assets/Unit.prefab";
	Guid guid = SceneSystem::SavePrefab(unit, Path(prefabPath));
	ASSERT_TRUE(guid.IsValid());
	ASSERT_TRUE(ExpectTextDocument(prefabPath));

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);

	PrefabAsset restored;
	Buffer payload;
	ASSERT_TRUE(CookThrough(*loaded, restored, payload));
	EXPECT_TRUE(ExpectBinaryPayload(payload));
	ASSERT_NE(restored.GetFragment(), nullptr);
	EXPECT_FALSE(restored.GetBasePrefab().IsValid());

	ASSERT_TRUE(FileSystem::WriteFile(Absolute("project://Assets/Cooked.prefab"), payload));
	AssetDatabase::ScanProjectDirectory();
	Guid cookedGuid = AssetDatabase::LookupGuid(Path("project://Assets/Cooked.prefab"));
	ASSERT_TRUE(cookedGuid.IsValid());

	Handle<SceneObject> instance = SceneSystem::Instantiate(cookedGuid, scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->name, "Unit");
	EXPECT_TRUE(Vec3(3.0f, 4.0f, 5.0f) == instance->GetLocalPosition());
	Handle<AcStatsComponent> restoredStats = instance->GetComponent<AcStatsComponent>();
	ASSERT_TRUE(restoredStats.IsValid());
	EXPECT_FLOAT_EQ(restoredStats->health, 12.5f);
	EXPECT_EQ(restoredStats->damage, 7);
	EXPECT_EQ(restoredStats->title, "unit");
	ASSERT_EQ(instance->GetChildCount(), 1u);
	EXPECT_EQ(instance->GetChild(0)->name, "Muzzle");
	EXPECT_TRUE(Vec3(0.0f, 0.0f, 1.5f) == instance->GetChild(0)->GetLocalPosition());

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(DocumentCookFixture, CookedSceneCarriesPrefabInstancesAddedComponent) {
	Handle<Scene> scene = SceneSystem::CreateScene("Level1");
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", scene);
	Guid prefabGuid = SceneSystem::SavePrefab(unit, Path("project://Assets/Unit.prefab"));
	ASSERT_TRUE(prefabGuid.IsValid());

	Handle<AcStatsComponent> added = unit->AddComponent<AcStatsComponent>();
	added->health = 3.5f;
	added->title = "instance only";

	const char* scenePath = "project://Assets/Level1.scene";
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path(scenePath), false));
	ASSERT_TRUE(ExpectTextDocument(scenePath));

	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path(scenePath));
	ASSERT_TRUE(guid.IsValid());
	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);

	SceneAsset restored;
	Buffer payload;
	ASSERT_TRUE(CookThrough(*loaded, restored, payload));
	EXPECT_TRUE(ExpectBinaryPayload(payload));

	ASSERT_TRUE(FileSystem::WriteFile(Absolute("project://Assets/Cooked.scene"), payload));
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://Assets/Cooked.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	ASSERT_EQ(reloaded->GetRootCount(), 1u);
	Handle<SceneObject> instance = reloaded->GetRoot(0);
	Handle<AcStatsComponent> restoredAdded = instance->GetComponent<AcStatsComponent>();
	ASSERT_TRUE(restoredAdded.IsValid());
	EXPECT_FLOAT_EQ(restoredAdded->health, 3.5f);
	EXPECT_EQ(restoredAdded->title, "instance only");

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(DocumentCookFixture, CookKeepsComponentOfAbsentType) {
	const char* scenePath = "project://Assets/Level1.scene";
	{
		ScopedFragileType present;
		Handle<Scene> scene = SceneSystem::CreateScene("Level1");
		Handle<SceneObject> altar = SceneSystem::CreateObject("Altar", scene);
		Handle<AcFragileComponent> fragile = altar->AddComponent<AcFragileComponent>();
		fragile->charges = 9;
		fragile->sigil = "ward";
		ASSERT_TRUE(SceneSystem::SaveScene(scene, Path(scenePath), false));
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
	ASSERT_TRUE(ExpectTextDocument(scenePath));

	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path(scenePath));
	ASSERT_TRUE(guid.IsValid());
	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);

	SceneAsset restored;
	Buffer payload;
	ASSERT_TRUE(CookThrough(*loaded, restored, payload));
	EXPECT_TRUE(ExpectBinaryPayload(payload));
	AssetDatabase::UnloadAsset(loaded);

	ASSERT_TRUE(FileSystem::WriteFile(Absolute("project://Assets/Cooked.scene"), payload));
	{
		ScopedFragileType back;
		Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://Assets/Cooked.scene"));
		ASSERT_TRUE(reloaded.IsValid());
		ASSERT_EQ(reloaded->GetRootCount(), 1u);
		Handle<AcFragileComponent> fragile = reloaded->GetRoot(0)->GetComponent<AcFragileComponent>();
		ASSERT_TRUE(fragile.IsValid());
		EXPECT_EQ(fragile->charges, 9);
		EXPECT_EQ(fragile->sigil, "ward");
	}
}
