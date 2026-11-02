#pragma once

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>

namespace Shalltear::CookHarness {

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
		const std::span<const Guid> roots = AssetDatabase::GetEngineBuiltinRoots();
		for (size_t i = 0; i < roots.size(); ++i) {
			WriteEngineAsset(engineRoot, std::format("Builtin{}.txt", i).c_str(), roots[i].ToString().c_str());
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
