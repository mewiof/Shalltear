#include "Graphics/ShaderCache.h"

#include "Shalltear/Core/BuildInfo.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringHash.h"

#include <filesystem>
#include <format>
#include <functional>
#include <thread>

namespace Shalltear {

	namespace {

		struct KeyHasher {
			u64 hash = 1469598103934665603ull;

			void Mix(u64 value) {
				for (u32 i = 0; i < 8; ++i) {
					hash ^= (value >> (i * 8)) & 0xFF;
					hash *= 1099511628211ull;
				}
			}
			void Mix(std::string_view text) {
				for (char c : text) {
					hash ^= static_cast<u8>(c);
					hash *= 1099511628211ull;
				}
			}
		};

		u64 BlobKey(u64 entryKey, std::span<const ShaderIncludeDependency> includes) {
			KeyHasher hasher;
			hasher.Mix(entryKey);
			for (const ShaderIncludeDependency& include : includes) {
				hasher.Mix(include.contentHash);
			}
			return hasher.hash;
		}

		bool WriteAtomically(const Path& dir, const str& name, const Buffer& bytes) {
			const u64 writer = std::hash<std::thread::id>{}(std::this_thread::get_id());
			const Path file = dir / name;
			const Path temporary = dir / std::format("{}.{:016x}.tmp", name, writer);
			if (!FileSystem::WriteFile(temporary, bytes)) {
				SLT_LOG_WARN("Graphics", "shader cache write failed for '{}'", temporary.ToString());
				return false;
			}
			std::error_code ec;
			std::filesystem::rename(temporary.ToString(), file.ToString(), ec);
			if (ec) {
				std::filesystem::remove(temporary.ToString(), ec);
			}
			return true;
		}

	} // anonymous namespace

	u64 ShaderCache::ComputeEntryKey(
		u64 bufferHash,
		u64 assetHash,
		std::string_view assetDirectory,
		std::span<const str> defines,
		ShaderStageKind stage,
		ShaderCompileMode mode) {

		KeyHasher hasher;
		hasher.Mix(bufferHash);
		hasher.Mix(assetHash);
		hasher.Mix(assetDirectory);
		hasher.Mix(u64{0x1F});
		for (const str& define : defines) {
			hasher.Mix(define);
			hasher.Mix(u64{0x1F});
		}
		hasher.Mix(static_cast<u64>(stage));
		hasher.Mix(static_cast<u64>(mode));
		hasher.Mix(ShaderCompiler::GetCompilerStamp());
		hasher.Mix(BuildInfo::GetEngineHash());
		return hasher.hash;
	}

	Path ShaderCache::GetCacheDirectory() {
		Path projectCache = FileSystem::Resolve(Path("project://Library/ShaderCache"));
		if (projectCache.ToString().find("://") == str::npos) {
			return projectCache;
		}
		return Path("Library/ShaderCache");
	}

	bool ShaderCache::TryLoad(u64 entryKey, Vector<u8>& outSpirv, Vector<ShaderIncludeDependency>& outIncludes) {
		outIncludes.clear();
		const Path dir = GetCacheDirectory();
		const Path manifestFile = dir / std::format("{:016x}.inc", entryKey);
		if (!FileSystem::Exists(manifestFile)) {
			return false;
		}
		const str manifest = FileSystem::ReadTextFile(manifestFile, MemTag::Asset);
		for (size_t begin = 0; begin < manifest.size();) {
			size_t newline = manifest.find('\n', begin);
			if (newline == str::npos) {
				newline = manifest.size();
			}
			if (newline > begin) {
				ShaderIncludeDependency include;
				include.physicalPath = manifest.substr(begin, newline - begin);
				if (!FileSystem::Exists(Path(include.physicalPath))) {
					outIncludes.clear();
					return false;
				}
				str content = FileSystem::ReadTextFile(Path(include.physicalPath), MemTag::Asset);
				include.contentHash = StringHash(std::string_view(content)).GetValue();
				outIncludes.push_back(std::move(include));
			}
			begin = newline + 1;
		}

		const Path file = dir / std::format("{:016x}.spv", BlobKey(entryKey, outIncludes));
		if (!FileSystem::Exists(file)) {
			outIncludes.clear();
			return false;
		}
		Buffer buffer = FileSystem::ReadFile(file, MemTag::Asset);
		if (!buffer || buffer.size == 0 || buffer.size % 4 != 0) {
			outIncludes.clear();
			return false;
		}
		outSpirv.assign(buffer.data, buffer.data + buffer.size);
		return true;
	}

	void ShaderCache::Store(u64 entryKey, std::span<const ShaderIncludeDependency> includes, std::span<const u8> spirv) {
		Path dir = GetCacheDirectory();
		std::error_code ec;
		std::filesystem::create_directories(dir.ToString(), ec);
		if (ec) {
			SLT_LOG_WARN("Graphics", "shader cache directory '{}' couldn't be created", dir.ToString());
			return;
		}

		// blob before the manifest that leads to it
		Buffer blob(spirv.size(), MemTag::Asset);
		std::memcpy(blob.data, spirv.data(), spirv.size());
		if (!WriteAtomically(dir, std::format("{:016x}.spv", BlobKey(entryKey, includes)), blob)) {
			return;
		}
		str manifest;
		for (const ShaderIncludeDependency& include : includes) {
			manifest += include.physicalPath;
			manifest += '\n';
		}
		Buffer manifestBytes(manifest.size(), MemTag::Asset);
		if (!manifest.empty()) {
			std::memcpy(manifestBytes.data, manifest.data(), manifest.size());
		}
		WriteAtomically(dir, std::format("{:016x}.inc", entryKey), manifestBytes);
	}

} // namespace Shalltear
