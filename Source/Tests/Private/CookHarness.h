#pragma once

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>

namespace Shalltear::CookHarness {

	constexpr const char* BUILTIN_GUIDS[] = {
		"376e2ff23e456f74-32aa3655589414a0",
		"5d0eee565c0eb506-d813bbcaa86ba3a8",
		"5c1d9a4e7b30f682-a41f6d28c95e0b73",
		"7d41ba60e2c85f39-1ab7e594cd06283f",
		"9c4b17e6a2d05f38-64e18b3cf7a9d201",
		"338097fb9c0c87b0-92e117f93e38c514",
		"ab2785d7cb197a4c-024a0663fec99085",
		"e579e36a1ff6b994-b51481f739564cc8",
		"3f6c02b8ad415e79-c7d9418ba2e60f35",
		"5d280fd701b66993-da09822ec0a9b52b",
		"8f67be9cadb890a4-98b2bb207a41f7e2",
		"de0ec401677a49c2-a4490098cd78380f",
		"6a3f1c9e2b847d05-e1c74b2a90f36d18",
	};

	inline std::filesystem::path FindRepoDir(const char* relative) {
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

	inline void WriteEngineAsset(const std::filesystem::path& engineRoot, const char* relative, const char* guid) {
		std::filesystem::path at = engineRoot / relative;
		std::error_code ec;
		std::filesystem::create_directories(at.parent_path(), ec);
		FileSystem::WriteTextFile(Path(at.generic_string()), str("built-in"));
		FileSystem::WriteTextFile(Path((at.generic_string() + ".meta")),
								  std::format("{{\n\t\"assetId\": \"{}\",\n\t\"type\": \"{:016x}\"\n}}\n", guid,
											  StringHash("TextAsset").GetValue()));
	}

	inline void WriteSyntheticEngineRoots(const std::filesystem::path& engineRoot) {
		for (size_t i = 0; i < std::size(BUILTIN_GUIDS); ++i) {
			WriteEngineAsset(engineRoot, std::format("Builtin{}.txt", i).c_str(), BUILTIN_GUIDS[i]);
		}
		WriteEngineAsset(engineRoot, "Templates/Scaffold.txt", "1111111111111111-2222222222222222");
	}

	inline ::testing::AssertionResult BytesIdentical(const Vector<u8>& a, const Vector<u8>& b) {
		if (a.size() != b.size()) {
			return ::testing::AssertionFailure()
				   << "the two packages are " << a.size() << " and " << b.size() << " bytes";
		}
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i] != b[i]) {
				return ::testing::AssertionFailure()
					   << "the two packages first differ at byte " << i << " of " << a.size() << ": "
					   << static_cast<u32>(a[i]) << " against " << static_cast<u32>(b[i]);
			}
		}
		return ::testing::AssertionSuccess();
	}

	inline PakSuperblock SuperblockOf(const Vector<u8>& package) {
		PakSuperblock sb{};
		if (package.size() >= sizeof(PakSuperblock)) {
			std::memcpy(&sb, package.data(), sizeof(PakSuperblock));
		}
		return sb;
	}

} // namespace Shalltear::CookHarness
