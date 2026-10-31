#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <cmath>
#include <cstring>

using namespace Shalltear;
using namespace ShadingTest;

namespace {

	class TextureCompressionImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_bcn_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Stage(const char* name, Vec2I size, const std::function<void(i32, i32, u8*)>& texel,
				   std::initializer_list<std::pair<const char*, const char*>> settings = {}) {
			Vector<u8> rgba{GetMemoryResource(MemTag::Asset)};
			rgba.resize(static_cast<size_t>(size.x) * size.y * 4);
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					texel(x, y, rgba.data() + (static_cast<size_t>(y) * size.x + x) * 4);
				}
			}
			TestImages::WriteTga(_root / name, static_cast<u16>(size.x), static_cast<u16>(size.y), rgba.data());
			const Path path(str("project://") + name);
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(path).IsValid());
			if (settings.size() == 0) {
				return;
			}
			JSONValue block = JSONValue::MakeObject();
			for (const auto& [key, value] : settings) {
				block.AddMember(key, JSONValue::MakeString(value));
			}
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(path, block));
		}

		TextureAsset* Load(const char* name) {
			Asset* asset = AssetDatabase::LoadAsset(Path(str("project://") + name));
			return asset && asset->GetAssetTypeId() == StringHash("TextureAsset") ? static_cast<TextureAsset*>(asset) : nullptr;
		}
	};

	void Fill(u8 r, u8 g, u8 b, u8* out) {
		out[0] = r;
		out[1] = g;
		out[2] = b;
		out[3] = 255;
	}

	Vector<u8> Decode(const TextureAsset& texture, u32 mip, u32 slice) {
		Vector<u8> out{GetMemoryResource(MemTag::Asset)};
		const TextureCodec* codec = TextureCodecs::Find(texture.format);
		if (!codec) {
			return out;
		}
		const u32 layers = texture.isCube ? 6u : texture.slices;
		size_t offset = 0;
		for (u32 level = 0; level < mip; ++level) {
			const Vec2I size(std::max(1, texture.size.x >> level), std::max(1, texture.size.y >> level));
			offset += static_cast<size_t>(RHI::FormatLevelBytes(texture.format, size)) * layers;
		}
		const Vec2I size(std::max(1, texture.size.x >> mip), std::max(1, texture.size.y >> mip));
		const size_t bytes = static_cast<size_t>(RHI::FormatLevelBytes(texture.format, size));
		offset += bytes * slice;
		out.resize(static_cast<size_t>(RHI::FormatLevelBytes(TextureCodecs::DecodedFormat(texture.format), size)));
		if (offset + bytes > texture.pixels.size() || !codec->decode(texture.format, {texture.pixels.data() + offset, bytes}, size, out)) {
			out.clear();
		}
		return out;
	}

} // anonymous namespace

TEST_F(TextureCompressionImportFixture, DefaultIsBC7FullChain) {
	Stage("Color.tga", {16, 16}, [](i32 x, i32 y, u8* out) { Fill(static_cast<u8>(x * 16), static_cast<u8>(y * 16), 180, out); });
	TextureAsset* texture = Load("Color.tga");
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(texture->format, RHI::Format::BC7SRGB);
	EXPECT_EQ(texture->storedMipLevels, 5u);
	EXPECT_EQ(texture->pixels.size(), static_cast<size_t>((16 + 4 + 1 + 1 + 1) * 16));

	Vector<u8> top = Decode(*texture, 0, 0);
	ASSERT_EQ(top.size(), static_cast<size_t>(16 * 16 * 4));
	const u8* corner = top.data() + (15 * 16 + 15) * 4;
	EXPECT_NEAR(corner[0], 240, 4);
	EXPECT_NEAR(corner[1], 240, 4);
	EXPECT_NEAR(corner[2], 180, 4);
	AssetDatabase::UnloadAsset(texture);

	const str meta = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Color.tga"))), MemTag::Asset);
	EXPECT_NE(meta.find("\"compression\""), str::npos);
	EXPECT_NE(meta.find("\"highQuality\""), str::npos);
	EXPECT_NE(meta.find("\"textureType\""), str::npos);
}

TEST_F(TextureCompressionImportFixture, NoneKeepsTexelsOneLevel) {
	Stage("Raw.tga", {16, 16}, [](i32 x, i32, u8* out) { Fill(static_cast<u8>(x * 16), 7, 9, out); }, {{"compression", "none"}});
	TextureAsset* texture = Load("Raw.tga");
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(texture->format, RHI::Format::RGBA8SRGB);
	EXPECT_EQ(texture->storedMipLevels, 1u);
	ASSERT_EQ(texture->pixels.size(), static_cast<size_t>(16 * 16 * 4));
	EXPECT_EQ(texture->pixels[15 * 4], 240);
	EXPECT_EQ(texture->pixels[15 * 4 + 1], 7);
	AssetDatabase::UnloadAsset(texture);
}

TEST_F(TextureCompressionImportFixture, NormalMapIsLinearBC5) {
	auto tilted = [](i32, i32, u8* out) { Fill(204, 89, 222, out); };
	Stage("Bumps.tga", {8, 8}, tilted, {{"textureType", "normalMap"}});
	Stage("BumpsRaw.tga", {8, 8}, tilted, {{"textureType", "normalMap"}, {"compression", "none"}});

	TextureAsset* compressed = Load("Bumps.tga");
	ASSERT_NE(compressed, nullptr);
	EXPECT_EQ(compressed->format, RHI::Format::BC5Unorm);
	EXPECT_EQ(compressed->storedMipLevels, 4u);
	Vector<u8> top = Decode(*compressed, 0, 0);
	ASSERT_FALSE(top.empty());
	EXPECT_NEAR(top[0], 204, 1);
	EXPECT_NEAR(top[1], 89, 1);
	AssetDatabase::UnloadAsset(compressed);

	TextureAsset* raw = Load("BumpsRaw.tga");
	ASSERT_NE(raw, nullptr);
	EXPECT_EQ(raw->format, RHI::Format::RGBA8Unorm);
	AssetDatabase::UnloadAsset(raw);
}

TEST_F(TextureCompressionImportFixture, PartialBlocksImportUncompressed) {
	auto grey = [](i32, i32, u8* out) { Fill(128, 128, 128, out); };
	Stage("Odd.tga", {6, 6}, grey);
	Stage("Even.tga", {8, 8}, grey);
	TextureAsset* odd = Load("Odd.tga");
	TextureAsset* even = Load("Even.tga");
	ASSERT_NE(odd, nullptr);
	ASSERT_NE(even, nullptr);
	EXPECT_EQ(odd->format, RHI::Format::RGBA8SRGB);
	EXPECT_EQ(odd->storedMipLevels, 1u);
	EXPECT_EQ(odd->pixels.size(), static_cast<size_t>(6 * 6 * 4));
	EXPECT_EQ(even->format, RHI::Format::BC7SRGB);
	AssetDatabase::UnloadAsset(odd);
	AssetDatabase::UnloadAsset(even);
}

TEST_F(TextureCompressionImportFixture, FlipbookCompressesEachSliceInPlace) {
	Stage("Flip.tga", {8, 16}, [](i32, i32 y, u8* out) { y < 8 ? Fill(230, 30, 20, out) : Fill(20, 60, 210, out); });
	JSONValue block = JSONValue::MakeObject();
	block.AddMember("shape", JSONValue::MakeString("2darray"));
	block.AddMember("rows", JSONValue::MakeNumber(2.0));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Flip.tga"), block));

	TextureAsset* texture = Load("Flip.tga");
	ASSERT_NE(texture, nullptr);
	ASSERT_EQ(texture->slices, 2u);
	EXPECT_EQ(texture->format, RHI::Format::BC7SRGB);
	EXPECT_EQ(texture->storedMipLevels, 4u);
	EXPECT_EQ(texture->pixels.size(), static_cast<size_t>((4 + 1 + 1 + 1) * 2 * 16));

	Vector<u8> bottom = Decode(*texture, 0, 1);
	ASSERT_FALSE(bottom.empty());
	EXPECT_NEAR(bottom[0], 20, 3);
	EXPECT_NEAR(bottom[2], 210, 3);
	Vector<u8> topTail = Decode(*texture, 3, 0);
	ASSERT_EQ(topTail.size(), 4u);
	EXPECT_NEAR(topTail[0], 230, 3);
	EXPECT_NEAR(topTail[2], 20, 3);
	AssetDatabase::UnloadAsset(texture);
}

TEST_F(TextureCompressionImportFixture, CompressedRecordRoundTrips) {
	Stage("Cooked.tga", {8, 8}, [](i32 x, i32 y, u8* out) { Fill(static_cast<u8>(x * 30), static_cast<u8>(y * 30), 90, out); });
	TextureAsset* texture = Load("Cooked.tga");
	ASSERT_NE(texture, nullptr);
	ASSERT_EQ(texture->format, RHI::Format::BC7SRGB);

	PakStringPool strings;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	{
		CookWriter descriptorWriter(descriptor, strings);
		CookWriter payloadWriter(payload, strings);
		ASSERT_TRUE(texture->Cook(descriptorWriter, payloadWriter));
	}
	EXPECT_EQ(payload.size(), texture->pixels.size());

	TextureAsset restored;
	CookReader descriptorReader(descriptor.data(), descriptor.size());
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(restored.LoadCooked(descriptorReader, payloadReader));
	EXPECT_EQ(restored.format, RHI::Format::BC7SRGB);
	EXPECT_EQ(restored.storedMipLevels, texture->storedMipLevels);
	EXPECT_TRUE(restored.pixels == texture->pixels);
	EXPECT_EQ(restored.settings.compression, TextureCompression::HighQuality);

	Vector<u8> shortDescriptor = descriptor;
	const u32 claimed = static_cast<u32>(texture->pixels.size() - 16);
	std::memcpy(shortDescriptor.data() + 12, &claimed, 4);
	TextureAsset truncated;
	CookReader shortDescriptorReader(shortDescriptor.data(), shortDescriptor.size());
	CookReader shortPayloadReader(payload.data(), payload.size() - 16);
	EXPECT_FALSE(truncated.LoadCooked(shortDescriptorReader, shortPayloadReader));

	Vector<u8> badFormat = descriptor;
	const u32 noSuchFormat = static_cast<u32>(RHI::Format::Count);
	std::memcpy(badFormat.data() + 8, &noSuchFormat, 4);
	TextureAsset lying;
	CookReader badFormatReader(badFormat.data(), badFormat.size());
	CookReader fullPayloadReader(payload.data(), payload.size());
	EXPECT_FALSE(lying.LoadCooked(badFormatReader, fullPayloadReader));
	AssetDatabase::UnloadAsset(texture);
}

namespace {

	Vec3 HalfTexel(const Vector<u8>& texels, size_t index) {
		u16 half[3];
		std::memcpy(half, texels.data() + index * 8, sizeof(half));
		return Vec3(Math::HalfToFloat(half[0]), Math::HalfToFloat(half[1]), Math::HalfToFloat(half[2]));
	}

	Vec3 Ramp(f32 u, f32 v) {
		return Vec3(1.0f, 0.8f, 0.6f) * (0.25f + 7.0f * u + 1.5f * v);
	}

	Vec3 SmoothSky(f32 u, f32 v) {
		return Vec3(1.0f, 0.85f, 0.7f) * (0.3f + 3.0f * (0.5f + 0.5f * std::cos(u * 6.2831853f)) * (1.0f - v) + v);
	}

	void NoCompression(const Path& path) {
		JSONValue block = JSONValue::MakeObject();
		block.AddMember("compression", JSONValue::MakeString("none"));
		ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(path).IsValid());
		ASSERT_TRUE(AssetDatabase::WriteImporterSettings(path, block));
	}

} // anonymous namespace

TEST_F(TextureCompressionImportFixture, HdrCompressesToBC6H) {
	WriteRadianceHDR(_root / "Light.hdr", 16, 16, Ramp);
	WriteRadianceHDR(_root / "LightRaw.hdr", 16, 16, Ramp);
	WriteRadianceHDR(_root / "Odd.hdr", 6, 6, Ramp);
	NoCompression(Path("project://LightRaw.hdr"));

	TextureAsset* light = Load("Light.hdr");
	ASSERT_NE(light, nullptr);
	EXPECT_FALSE(light->isCube);
	EXPECT_EQ(light->format, RHI::Format::BC6HUFloat);
	EXPECT_EQ(light->storedMipLevels, 5u);
	EXPECT_EQ(light->pixels.size(), static_cast<size_t>((16 + 4 + 1 + 1 + 1) * 16));
	TextureAsset* raw = Load("LightRaw.hdr");
	ASSERT_NE(raw, nullptr);
	EXPECT_EQ(raw->format, RHI::Format::RGBA16Float);
	EXPECT_EQ(raw->storedMipLevels, 1u);
	ASSERT_EQ(raw->pixels.size(), static_cast<size_t>(16 * 16 * 8));

	const Vector<u8> top = Decode(*light, 0, 0);
	ASSERT_EQ(top.size(), raw->pixels.size());
	f64 relative = 0.0;
	f32 worst = 0.0f;
	for (size_t texel = 0; texel < 16 * 16; ++texel) {
		const Vec3 a = HalfTexel(top, texel);
		const Vec3 b = HalfTexel(raw->pixels, texel);
		for (u32 c = 0; c < 3; ++c) {
			const f32 error = std::fabs(a[c] - b[c]) / b[c];
			relative += error;
			worst = std::max(worst, error);
		}
	}
	EXPECT_LT(relative / (16 * 16 * 3), 0.02);
	EXPECT_LT(worst, 0.15f);
	EXPECT_GT(relative, 0.0);
	ASSERT_GT(HalfTexel(raw->pixels, 15 * 16 + 15).x, 7.0f);
	EXPECT_GT(HalfTexel(top, 15 * 16 + 15).x, 7.0f);

	TextureAsset* odd = Load("Odd.hdr");
	ASSERT_NE(odd, nullptr);
	EXPECT_EQ(odd->format, RHI::Format::RGBA16Float);
	EXPECT_EQ(odd->pixels.size(), static_cast<size_t>(6 * 6 * 8));
	AssetDatabase::UnloadAsset(light);
	AssetDatabase::UnloadAsset(raw);
	AssetDatabase::UnloadAsset(odd);
}

TEST_F(TextureCompressionImportFixture, CubeIsBC6HWithIrradiance) {
	WriteRadianceHDR(_root / "Sky.hdr", 64, 32, SmoothSky);
	WriteRadianceHDR(_root / "SkyRaw.hdr", 64, 32, SmoothSky);
	NoCompression(Path("project://SkyRaw.hdr"));

	TextureAsset* cube = Load("Sky.hdr");
	TextureAsset* raw = Load("SkyRaw.hdr");
	ASSERT_NE(cube, nullptr);
	ASSERT_NE(raw, nullptr);
	ASSERT_TRUE(cube->isCube);
	ASSERT_TRUE(raw->isCube);
	EXPECT_EQ(cube->format, RHI::Format::BC6HUFloat);
	EXPECT_EQ(raw->format, RHI::Format::RGBA16Float);
	ASSERT_EQ(cube->size, raw->size);
	ASSERT_EQ(cube->storedMipLevels, raw->storedMipLevels);
	ASSERT_GT(cube->storedMipLevels, 1u);
	ASSERT_EQ(cube->irradianceSH.size(), 9u);
	for (u32 i = 0; i < 9; ++i) {
		EXPECT_EQ(cube->irradianceSH[i], raw->irradianceSH[i]) << "coefficient " << i;
	}

	size_t rawOffset = 0;
	for (u32 mip = 0; mip < cube->storedMipLevels; ++mip) {
		const i32 edge = std::max(1, cube->size.x >> mip);
		const size_t faceBytes = static_cast<size_t>(edge) * edge * 8;
		f64 relative = 0.0;
		f32 worst = 0.0f;
		u32 count = 0;
		for (u32 face = 0; face < 6; ++face) {
			const Vector<u8> decoded = Decode(*cube, mip, face);
			ASSERT_EQ(decoded.size(), faceBytes) << "mip " << mip << " face " << face;
			Vector<u8> expected{GetMemoryResource(MemTag::Asset)};
			expected.assign(raw->pixels.begin() + static_cast<std::ptrdiff_t>(rawOffset),
							raw->pixels.begin() + static_cast<std::ptrdiff_t>(rawOffset + faceBytes));
			for (size_t texel = 0; texel < static_cast<size_t>(edge * edge); ++texel) {
				const Vec3 a = HalfTexel(decoded, texel);
				const Vec3 b = HalfTexel(expected, texel);
				for (u32 c = 0; c < 3; ++c) {
					const f32 error = std::fabs(a[c] - b[c]) / std::max(b[c], 0.05f);
					relative += error;
					worst = std::max(worst, error);
					++count;
				}
			}
			rawOffset += faceBytes;
		}
		EXPECT_LT(relative / count, 0.01) << "mip " << mip;
		EXPECT_LT(worst, 0.12f) << "mip " << mip;
	}

	PakStringPool strings;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	{
		CookWriter descriptorWriter(descriptor, strings);
		CookWriter payloadWriter(payload, strings);
		ASSERT_TRUE(cube->Cook(descriptorWriter, payloadWriter));
	}
	TextureAsset restored;
	CookReader descriptorReader(descriptor.data(), descriptor.size());
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(restored.LoadCooked(descriptorReader, payloadReader));
	EXPECT_TRUE(restored.isCube);
	EXPECT_EQ(restored.format, RHI::Format::BC6HUFloat);
	EXPECT_EQ(restored.storedMipLevels, cube->storedMipLevels);
	EXPECT_TRUE(restored.pixels == cube->pixels);
	EXPECT_EQ(restored.irradianceSH.size(), 9u);
	AssetDatabase::UnloadAsset(cube);
	AssetDatabase::UnloadAsset(raw);
}

namespace {

	class NormalMapReadbackFixture : public MaterialReadbackFixture {
	protected:
		static void Bumps(i32 x, i32 y, u8* out) {
			const f32 nx = 0.6f * std::sin(static_cast<f32>(x) * 0.8f);
			const f32 ny = 0.6f * std::cos(static_cast<f32>(y) * 0.8f);
			const f32 nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
			Fill(static_cast<u8>(nx * 127.5f + 128.0f), static_cast<u8>(ny * 127.5f + 128.0f), static_cast<u8>(nz * 127.5f + 128.0f), out);
		}

		Guid StageNormalMap(const char* name, bool compressed) {
			Vector<u8> rgba{GetMemoryResource(MemTag::Asset)};
			rgba.resize(8 * 8 * 4);
			for (i32 y = 0; y < 8; ++y) {
				for (i32 x = 0; x < 8; ++x) {
					Bumps(x, y, rgba.data() + (y * 8 + x) * 4);
				}
			}
			TestImages::WriteTga(_root / name, 8, 8, rgba.data());
			const Path path(str("project://") + name);
			const Guid guid = AssetDatabase::EnsureAssetIdentity(path);
			JSONValue block = JSONValue::MakeObject();
			block.AddMember("textureType", JSONValue::MakeString("normalMap"));
			block.AddMember("compression", JSONValue::MakeString(compressed ? "highQuality" : "none"));
			block.AddMember("filter", JSONValue::MakeString("nearest"));
			AssetDatabase::WriteImporterSettings(path, block);
			return guid;
		}

		bool Render(MaterialAsset* material, Vector<u8>& pixels) {
			ShadingStage stage;
			if (!BuildStage(material, stage)) {
				return false;
			}
			Vec2I size{};
			const bool rendered = RenderStage(stage, pixels, size);
			TearDownStage(stage);
			return rendered && size == Vec2I(64, 64);
		}
	};

} // anonymous namespace

TEST_F(NormalMapReadbackFixture, BC5NormalMatchesUncompressed) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no device or no dxcompiler";
	}
	const Guid lit = AssetDatabase::EnsureAssetIdentity(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_TRUE(lit.IsValid());
	const Guid bc5 = StageNormalMap("Bumps.tga", true);
	const Guid raw = StageNormalMap("BumpsRaw.tga", false);

	const str surface = "\t\"floats\": { \"metallic\": 0, \"roughness\": 0.6 },\n\t\"colors\": { \"baseColor\": [0.8, 0.8, 0.8, 1] }";
	MaterialAsset* compressedMaterial =
		LoadMaterial("Compressed.mat", lit, surface + ",\n\t\"textures\": { \"normalTexture\": \"" + bc5.ToString() + "\" }");
	MaterialAsset* rawMaterial = LoadMaterial("Raw.mat", lit, surface + ",\n\t\"textures\": { \"normalTexture\": \"" + raw.ToString() + "\" }");
	MaterialAsset* flatMaterial = LoadMaterial("Flat.mat", lit, surface);
	ASSERT_NE(compressedMaterial, nullptr);
	ASSERT_NE(rawMaterial, nullptr);
	ASSERT_NE(flatMaterial, nullptr);

	TextureAsset* bc5Texture = static_cast<TextureAsset*>(AssetDatabase::FindLoadedByGuid(bc5));
	TextureAsset* rawTexture = static_cast<TextureAsset*>(AssetDatabase::FindLoadedByGuid(raw));
	ASSERT_NE(bc5Texture, nullptr);
	ASSERT_NE(rawTexture, nullptr);
	ASSERT_EQ(bc5Texture->format, RHI::Format::BC5Unorm);
	ASSERT_EQ(rawTexture->format, RHI::Format::RGBA8Unorm);
	ASSERT_TRUE(bc5Texture->texture.IsValid());

	Vector<u8> compressed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> uncompressed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> flat{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(Render(compressedMaterial, compressed));
	ASSERT_TRUE(Render(rawMaterial, uncompressed));
	ASSERT_TRUE(Render(flatMaterial, flat));

	i32 worst = 0;
	u64 total = 0;
	u32 covered = 0;
	for (size_t i = 0; i < compressed.size(); i += 4) {
		const bool quad = flat[i] != 0 || flat[i + 1] != 0 || flat[i + 2] != 0;
		covered += quad ? 1u : 0u;
		for (u32 c = 0; c < 3; ++c) {
			const i32 difference = std::abs(static_cast<i32>(compressed[i + c]) - static_cast<i32>(uncompressed[i + c]));
			worst = std::max(worst, difference);
			total += quad ? static_cast<u64>(difference) : 0u;
		}
	}
	ASSERT_GE(covered, 200u);
	const f64 mean = static_cast<f64>(total) / static_cast<f64>(covered * 3);
	EXPECT_LE(worst, 6) << worst << " of 255";
	EXPECT_LT(mean, 1.0) << mean;
	i32 bumped = 0;
	for (size_t i = 0; i < uncompressed.size(); i += 4) {
		for (u32 c = 0; c < 3; ++c) {
			bumped = std::max(bumped, std::abs(static_cast<i32>(uncompressed[i + c]) - static_cast<i32>(flat[i + c])));
		}
	}
	EXPECT_GE(bumped, 20);

	AssetDatabase::UnloadAsset(compressedMaterial);
	AssetDatabase::UnloadAsset(rawMaterial);
	AssetDatabase::UnloadAsset(flatMaterial);
}
