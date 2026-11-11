#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/BuildTarget.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	using Texel = std::array<u8, 4>;

	constexpr Texel RED{255, 0, 0, 255};
	constexpr Texel CLEAR{0, 0, 0, 0};

	struct IcoEntry {
		u32 size = 0;
		u32 bitCount = 0;
		str payload;
	};

	struct DocumentImage {
		u32 width = 0;
		u32 height = 0;
		str rgba;
	};

	u32 Read16(const str& bytes, size_t at) {
		return static_cast<u32>(static_cast<u8>(bytes[at])) | (static_cast<u32>(static_cast<u8>(bytes[at + 1])) << 8);
	}

	u32 Read32(const str& bytes, size_t at) {
		return Read16(bytes, at) | (Read16(bytes, at + 2) << 16);
	}

	bool ParseIco(const str& bytes, std::vector<IcoEntry>& out) {
		if (bytes.size() < 6 || Read16(bytes, 0) != 0 || Read16(bytes, 2) != 1) {
			return false;
		}
		const u32 count = Read16(bytes, 4);
		if (bytes.size() < 6 + 16 * static_cast<size_t>(count)) {
			return false;
		}
		for (u32 i = 0; i < count; ++i) {
			const size_t entry = 6 + 16 * static_cast<size_t>(i);
			const u32 width = static_cast<u8>(bytes[entry]);
			const u32 height = static_cast<u8>(bytes[entry + 1]);
			const u32 bytesInEntry = Read32(bytes, entry + 8);
			const u32 offset = Read32(bytes, entry + 12);
			if (width != height || static_cast<size_t>(offset) + bytesInEntry > bytes.size()) {
				return false;
			}
			IcoEntry parsed;
			parsed.size = width == 0 ? 256 : width;
			parsed.bitCount = Read16(bytes, entry + 6);
			parsed.payload = bytes.substr(offset, bytesInEntry);
			out.push_back(std::move(parsed));
		}
		return true;
	}

	size_t MaskStride(u32 size) {
		return (static_cast<size_t>(size) + 31) / 32 * 4;
	}

	Texel DibTexel(const IcoEntry& entry, u32 x, u32 y) {
		const size_t at = 40 + (static_cast<size_t>(entry.size - 1 - y) * entry.size + x) * 4;
		return {static_cast<u8>(entry.payload[at + 2]), static_cast<u8>(entry.payload[at + 1]),
				static_cast<u8>(entry.payload[at]), static_cast<u8>(entry.payload[at + 3])};
	}

	bool DibMasked(const IcoEntry& entry, u32 x, u32 y) {
		const size_t at = 40 + static_cast<size_t>(entry.size) * entry.size * 4 +
						  static_cast<size_t>(entry.size - 1 - y) * MaskStride(entry.size) + x / 8;
		return (static_cast<u8>(entry.payload[at]) & (0x80u >> (x % 8))) != 0;
	}

	bool ParseDocument(const str& bytes, std::vector<DocumentImage>& out) {
		size_t at = 0;
		while (at < bytes.size()) {
			PakIconImage header{};
			if (bytes.size() - at < sizeof(header)) {
				return false;
			}
			std::memcpy(&header, bytes.data() + at, sizeof(header));
			at += sizeof(header);
			const size_t texels = static_cast<size_t>(header.width) * header.height * 4;
			if (bytes.size() - at < texels) {
				return false;
			}
			out.push_back({header.width, header.height, bytes.substr(at, texels)});
			at += texels;
		}
		return true;
	}

	Texel DocumentTexel(const DocumentImage& image, u32 x, u32 y) {
		const size_t at = (static_cast<size_t>(y) * image.width + x) * 4;
		return {static_cast<u8>(image.rgba[at]), static_cast<u8>(image.rgba[at + 1]), static_cast<u8>(image.rgba[at + 2]),
				static_cast<u8>(image.rgba[at + 3])};
	}

	std::function<Texel(u32, u32)> Solid(Texel color) {
		return [color](u32, u32) { return color; };
	}

	class BuildIconFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_icon_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root / "Assets");
			WriteBytes(_root / "Project.ini", "[Project]\nname = IconProbe\n");
		}

		void TearDown() override {
			BuildStepRegistry::Shutdown();
			BuildTargetRegistry::Shutdown();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		str Root() const { return _root.generic_string(); }

		static void WriteBytes(const fs::path& path, const str& bytes) {
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << bytes;
		}

		static str ReadBytes(const fs::path& path) {
			std::ifstream file(path, std::ios::binary);
			return str(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		void WriteTga(const str& relative, u32 width, u32 height, const std::function<Texel(u32, u32)>& texel) const {
			str bytes(18, '\0');
			bytes[2] = 2;
			bytes[12] = static_cast<char>(width & 0xFF);
			bytes[13] = static_cast<char>(width >> 8);
			bytes[14] = static_cast<char>(height & 0xFF);
			bytes[15] = static_cast<char>(height >> 8);
			bytes[16] = 32;
			bytes[17] = 0x28;
			for (u32 y = 0; y < height; ++y) {
				for (u32 x = 0; x < width; ++x) {
					const Texel value = texel(x, y);
					bytes += static_cast<char>(value[2]);
					bytes += static_cast<char>(value[1]);
					bytes += static_cast<char>(value[0]);
					bytes += static_cast<char>(value[3]);
				}
			}
			WriteBytes(_root / relative, bytes);
		}

		BuildStepStatus RunStep(std::string_view icon, std::initializer_list<u32> executableSizes,
								std::initializer_list<u32> windowSizes) {
			_profile = ResolvedProfile{};
			_profile.name = "Default";
			if (!icon.empty()) {
				ResolvedSetting setting;
				setting.key = "icon";
				setting.type = BuildSettingType::Path;
				setting.value = str(icon);
				setting.rawValue = str(icon);
				_profile.settings.push_back(std::move(setting));
			}
			_context = std::make_unique<BuildContext>();
			_context->profile = &_profile;
			_context->projectRoot = Root();
			_context->intermediateDir = (_root / "Library" / "Player" / "Default").generic_string();
			_context->executableIconSizes = executableSizes;
			_context->windowIconSizes = windowSizes;

			Vector<BuildStepDesc> steps;
			BuildStepRegistry::GetSteps(steps);
			for (const BuildStepDesc& step : steps) {
				if (step.name == "icon") {
					return step.fn(*_context);
				}
			}
			ADD_FAILURE();
			return BuildStepStatus::Failed;
		}

		bool HasIssue(BuildIssueSeverity severity) const {
			for (const BuildIssue& issue : _context->GetIssues()) {
				if (issue.severity == severity && issue.key == "icon") {
					return true;
				}
			}
			return false;
		}

		std::vector<IcoEntry> ExecutableIcon() const {
			std::vector<IcoEntry> entries;
			EXPECT_FALSE(_context->executableIconFile.empty());
			EXPECT_TRUE(ParseIco(ReadBytes(fs::path{_context->executableIconFile}), entries));
			return entries;
		}

		std::vector<DocumentImage> WindowIcon() const {
			std::vector<DocumentImage> images;
			EXPECT_FALSE(_context->windowIconDocument.empty());
			EXPECT_TRUE(ParseDocument(_context->windowIconDocument, images));
			return images;
		}

		bool DecodePng(const str& png, TextureAsset& out) const {
			const fs::path file = _root / "Decoded.png";
			WriteBytes(file, png);
			out.settings.generateMips = false;
			out.settings.shape = TextureShape::Texture2D;
			out.settings.compression = TextureCompression::None;
			return out.Import(Path(file.generic_string()));
		}

		void WriteProfile(const str& name, const str& target, const str& icon) const {
			BuildProfile profile;
			profile.name = name;
			profile.target = target;
			profile.SetString("icon", icon);
			ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile));
		}

		void ValidateWith(const str& targetId, const str& profileName, Vector<BuildIssue>& out) const {
			Vector<BuildSettingDesc> schema;
			BuildTargetRegistry::GetSchemaFor(targetId, schema);
			ResolvedProfile resolved;
			BuildProfileOps::ResolveProfile(Root(), profileName, schema, resolved);
			const IBuildTarget* target = BuildTargetRegistry::Find(targetId);
			ASSERT_NE(target, nullptr);
			target->Validate(Root(), resolved, out);
		}

		fs::path _root;
		ResolvedProfile _profile;
		std::unique_ptr<BuildContext> _context;
	};

	bool CountsIconIssue(const Vector<BuildIssue>& issues, BuildIssueSeverity severity) {
		for (const BuildIssue& issue : issues) {
			if (issue.key == "icon" && issue.severity == severity) {
				return true;
			}
		}
		return false;
	}

} // anonymous namespace

TEST_F(BuildIconFixture, NoIconMakesNothing) {
	EXPECT_EQ(RunStep("", {16, 256}, {16}), BuildStepStatus::Succeeded);
	EXPECT_TRUE(_context->executableIconFile.empty());
	EXPECT_TRUE(_context->windowIconDocument.empty());

	WriteTga("Assets/Icon.tga", 32, 32, Solid(RED));
	EXPECT_EQ(RunStep("Assets/Icon.tga", {}, {}), BuildStepStatus::Succeeded);
	EXPECT_TRUE(_context->executableIconFile.empty());
	EXPECT_TRUE(_context->windowIconDocument.empty());
	EXPECT_FALSE(fs::exists(_root / "Library"));
}

TEST_F(BuildIconFixture, IcoHoldsEachRequestedSize) {
	WriteTga("Assets/Icon.tga", 512, 512, Solid(RED));
	ASSERT_EQ(RunStep("Assets/Icon.tga", {16, 32, 256}, {}), BuildStepStatus::Succeeded);
	EXPECT_TRUE(_context->windowIconDocument.empty());

	const std::vector<IcoEntry> entries = ExecutableIcon();
	ASSERT_EQ(entries.size(), 3u);
	for (size_t i = 0; i < 2; ++i) {
		const IcoEntry& entry = entries[i];
		EXPECT_EQ(entry.size, i == 0 ? 16u : 32u);
		EXPECT_EQ(entry.bitCount, 32u);
		ASSERT_EQ(entry.payload.size(), 40 + static_cast<size_t>(entry.size) * entry.size * 4 + MaskStride(entry.size) * entry.size);
		EXPECT_EQ(Read32(entry.payload, 0), 40u);
		EXPECT_EQ(Read32(entry.payload, 4), entry.size);
		EXPECT_EQ(Read32(entry.payload, 8), entry.size * 2);
		EXPECT_EQ(Read16(entry.payload, 14), 32u);
		EXPECT_EQ(DibTexel(entry, 0, 0), RED);
		EXPECT_EQ(DibTexel(entry, entry.size - 1, entry.size - 1), RED);
		EXPECT_FALSE(DibMasked(entry, 0, 0));
	}
	EXPECT_EQ(entries[2].size, 256u);
	EXPECT_EQ(entries[2].payload.substr(0, 8), str("\x89PNG\r\n\x1A\n", 8));
}

TEST_F(BuildIconFixture, PngEntryMatchesSource) {
	u32 state = 12345;
	std::vector<Texel> source(256 * 256);
	for (Texel& texel : source) {
		for (u8& channel : texel) {
			state = state * 1664525u + 1013904223u;
			channel = static_cast<u8>(state >> 24);
		}
		if (texel[3] < 32) {
			texel[3] = 0;
		}
	}
	WriteTga("Assets/Noise.tga", 256, 256, [&](u32 x, u32 y) { return source[y * 256 + x]; });
	ASSERT_EQ(RunStep("Assets/Noise.tga", {256}, {}), BuildStepStatus::Succeeded);

	const std::vector<IcoEntry> entries = ExecutableIcon();
	ASSERT_EQ(entries.size(), 1u);
	TextureAsset decoded;
	ASSERT_TRUE(DecodePng(entries[0].payload, decoded));
	ASSERT_EQ(decoded.size, Vec2I(256, 256));
	ASSERT_EQ(decoded.pixels.size(), source.size() * 4);
	u32 mismatches = 0;
	for (size_t i = 0; i < source.size(); ++i) {
		const Texel expected = source[i][3] == 0 ? CLEAR : source[i];
		for (u32 channel = 0; channel < 4; ++channel) {
			mismatches += decoded.pixels[i * 4 + channel] != expected[channel] ? 1 : 0;
		}
	}
	EXPECT_EQ(mismatches, 0u);
}

TEST_F(BuildIconFixture, FlatPngCompresses) {
	WriteTga("Assets/Flat.tga", 256, 256, Solid({30, 60, 200, 255}));
	ASSERT_EQ(RunStep("Assets/Flat.tga", {256}, {}), BuildStepStatus::Succeeded);

	const std::vector<IcoEntry> entries = ExecutableIcon();
	ASSERT_EQ(entries.size(), 1u);
	EXPECT_LT(entries[0].payload.size(), 256u * 256u * 4u / 50u);
	TextureAsset decoded;
	ASSERT_TRUE(DecodePng(entries[0].payload, decoded));
	ASSERT_EQ(decoded.pixels.size(), 256u * 256u * 4u);
	EXPECT_EQ(decoded.pixels[0], 30);
	EXPECT_EQ(decoded.pixels[decoded.pixels.size() - 2], 200);
}

TEST_F(BuildIconFixture, CheckerAveragesInLinearLight) {
	WriteTga("Assets/Checker.tga", 64, 64, [](u32 x, u32 y) {
		return (x + y) % 2 == 0 ? Texel{0, 0, 0, 255} : Texel{255, 255, 255, 255};
	});
	ASSERT_EQ(RunStep("Assets/Checker.tga", {}, {16}), BuildStepStatus::Succeeded);

	const std::vector<DocumentImage> images = WindowIcon();
	ASSERT_EQ(images.size(), 1u);
	for (u32 y = 0; y < 16; ++y) {
		for (u32 x = 0; x < 16; ++x) {
			const Texel texel = DocumentTexel(images[0], x, y);
			EXPECT_NEAR(texel[0], 188, 2);
			EXPECT_EQ(texel[3], 255);
		}
	}
}

TEST_F(BuildIconFixture, TransparencyDoesNotDarkenEdges) {
	WriteTga("Assets/Half.tga", 64, 64, [](u32 x, u32) { return x < 30 ? RED : CLEAR; });
	ASSERT_EQ(RunStep("Assets/Half.tga", {}, {16}), BuildStepStatus::Succeeded);

	const std::vector<DocumentImage> images = WindowIcon();
	ASSERT_EQ(images.size(), 1u);
	EXPECT_EQ(DocumentTexel(images[0], 6, 8), RED);
	EXPECT_EQ(DocumentTexel(images[0], 7, 8), (Texel{255, 0, 0, 128}));
	EXPECT_EQ(DocumentTexel(images[0], 8, 8), CLEAR);
}

TEST_F(BuildIconFixture, NonSquareSourceIsCentered) {
	const Texel green{20, 220, 40, 255};
	WriteTga("Assets/Wide.tga", 64, 32, Solid(green));
	ASSERT_EQ(RunStep("Assets/Wide.tga", {16}, {16}), BuildStepStatus::Succeeded);

	const std::vector<DocumentImage> images = WindowIcon();
	ASSERT_EQ(images.size(), 1u);
	for (u32 y = 0; y < 16; ++y) {
		EXPECT_EQ(DocumentTexel(images[0], 8, y), y >= 4 && y < 12 ? green : CLEAR) << y;
	}

	const std::vector<IcoEntry> entries = ExecutableIcon();
	ASSERT_EQ(entries.size(), 1u);
	EXPECT_EQ(DibTexel(entries[0], 8, 2), CLEAR);
	EXPECT_TRUE(DibMasked(entries[0], 8, 2));
	EXPECT_EQ(DibTexel(entries[0], 8, 8), green);
	EXPECT_FALSE(DibMasked(entries[0], 8, 8));
}

TEST_F(BuildIconFixture, WindowDocumentHoldsEachSize) {
	const Texel color{10, 200, 30, 255};
	WriteTga("Assets/Icon.tga", 96, 96, Solid(color));
	ASSERT_EQ(RunStep("Assets/Icon.tga", {}, {16, 48}), BuildStepStatus::Succeeded);
	EXPECT_TRUE(_context->executableIconFile.empty());

	EXPECT_EQ(_context->windowIconDocument.size(), 2 * sizeof(PakIconImage) + 16 * 16 * 4 + 48 * 48 * 4);
	const std::vector<DocumentImage> images = WindowIcon();
	ASSERT_EQ(images.size(), 2u);
	EXPECT_EQ(images[0].width, 16u);
	EXPECT_EQ(images[0].height, 16u);
	EXPECT_EQ(images[1].width, 48u);
	EXPECT_EQ(images[1].height, 48u);
	EXPECT_EQ(DocumentTexel(images[0], 8, 8), color);
	EXPECT_EQ(DocumentTexel(images[1], 47, 47), color);
	EXPECT_FALSE(HasIssue(BuildIssueSeverity::Warning));
}

TEST_F(BuildIconFixture, SmallSourceWarns) {
	WriteTga("Assets/Small.tga", 32, 32, Solid(RED));
	ASSERT_EQ(RunStep("Assets/Small.tga", {16, 256}, {}), BuildStepStatus::Succeeded);
	EXPECT_TRUE(HasIssue(BuildIssueSeverity::Warning));
	EXPECT_EQ(ExecutableIcon().size(), 2u);
}

TEST_F(BuildIconFixture, HdrSourceFails) {
	str hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
	for (int i = 0; i < 4; ++i) {
		hdr += str("\x80\x80\x80\x81", 4);
	}
	WriteBytes(_root / "Assets" / "Sky.hdr", hdr);
	EXPECT_EQ(RunStep("Assets/Sky.hdr", {16}, {16}), BuildStepStatus::Failed);
	EXPECT_TRUE(HasIssue(BuildIssueSeverity::Error));
	EXPECT_TRUE(_context->executableIconFile.empty());
	EXPECT_TRUE(_context->windowIconDocument.empty());
}

TEST_F(BuildIconFixture, UnreadableSourceFails) {
	WriteBytes(_root / "Assets" / "Icon.png", "not an image");
	EXPECT_EQ(RunStep("Assets/Icon.png", {16}, {}), BuildStepStatus::Failed);
	EXPECT_TRUE(HasIssue(BuildIssueSeverity::Error));
	EXPECT_TRUE(_context->executableIconFile.empty());
}

TEST_F(BuildIconFixture, ValidationFlagsMissingAndAbsolute) {
	WriteTga("Assets/Icon.tga", 16, 16, Solid(RED));
	WriteProfile("Missing", "Windows", "Assets/Gone.png");
	WriteProfile("Absolute", "Linux", (_root / "Assets" / "Icon.tga").generic_string());
	WriteProfile("Present", "Windows", "Assets/Icon.tga");

	Vector<BuildIssue> missing;
	ValidateWith("Windows", "Missing", missing);
	EXPECT_TRUE(CountsIconIssue(missing, BuildIssueSeverity::Error));

	Vector<BuildIssue> absolute;
	ValidateWith("Linux", "Absolute", absolute);
	EXPECT_TRUE(CountsIconIssue(absolute, BuildIssueSeverity::Warning));
	EXPECT_FALSE(CountsIconIssue(absolute, BuildIssueSeverity::Error));

	Vector<BuildIssue> present;
	ValidateWith("Windows", "Present", present);
	EXPECT_FALSE(CountsIconIssue(present, BuildIssueSeverity::Error));
	EXPECT_FALSE(CountsIconIssue(present, BuildIssueSeverity::Warning));
}

TEST_F(BuildIconFixture, TargetsWithWindowsDeclareIcon) {
	auto declares = [](std::string_view id) {
		Vector<BuildSettingDesc> schema;
		BuildTargetRegistry::GetSchemaFor(id, schema);
		for (const BuildSettingDesc& desc : schema) {
			if (desc.key == "icon") {
				return desc.type == BuildSettingType::Path;
			}
		}
		return false;
	};
	EXPECT_TRUE(declares("Windows"));
	EXPECT_TRUE(declares("WindowsServer"));
	EXPECT_TRUE(declares("Linux"));
	EXPECT_FALSE(declares("LinuxServer"));

	auto configured = [](std::string_view id) {
		ResolvedProfile profile;
		profile.name = "Default";
		BuildContext context;
		context.profile = &profile;
		const IBuildTarget* target = BuildTargetRegistry::Find(id);
		EXPECT_NE(target, nullptr);
		if (target) {
			target->ConfigurePipeline(context);
		}
		return std::make_pair(Vector<u32>(context.executableIconSizes.begin(), context.executableIconSizes.end()),
							  Vector<u32>(context.windowIconSizes.begin(), context.windowIconSizes.end()));
	};
	for (std::string_view id : {"Windows", "WindowsServer"}) {
		const auto [executable, window] = configured(id);
		ASSERT_FALSE(executable.empty()) << id;
		EXPECT_EQ(executable.front(), 16u) << id;
		EXPECT_EQ(executable.back(), 256u) << id;
		EXPECT_TRUE(window.empty()) << id;
	}
	const auto [linuxExecutable, linuxWindow] = configured("Linux");
	EXPECT_TRUE(linuxExecutable.empty());
	ASSERT_FALSE(linuxWindow.empty());
	EXPECT_EQ(linuxWindow.front(), 16u);
	EXPECT_EQ(linuxWindow.back(), 256u);
	const auto [serverExecutable, serverWindow] = configured("LinuxServer");
	EXPECT_TRUE(serverExecutable.empty());
	EXPECT_TRUE(serverWindow.empty());
}
