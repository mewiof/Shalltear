#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace Shalltear::StaleSDK {

	inline bool GetCMakeString(const str& text, std::string_view name, str& out) {
		str marker = "set(" + str(name) + " \"";
		size_t at = text.find(marker);
		if (at == str::npos) {
			return false;
		}
		at += marker.size();
		size_t end = text.find('"', at);
		if (end == str::npos) {
			return false;
		}
		out = text.substr(at, end - at);
		return true;
	}

	inline bool SetCMakeString(str& text, std::string_view name, std::string_view value) {
		str previous;
		if (!GetCMakeString(text, name, previous)) {
			return false;
		}
		str marker = "set(" + str(name) + " \"";
		size_t at = text.find(marker) + marker.size();
		text = text.substr(0, at) + str(value) + text.substr(at + previous.size());
		return true;
	}

	inline size_t ReplaceAll(str& text, const str& from, std::string_view to) {
		if (from.empty()) {
			return 0;
		}
		size_t count = 0;
		for (size_t at = text.find(from); at != str::npos; at = text.find(from, at + to.size())) {
			text = text.substr(0, at) + str(to) + text.substr(at + from.size());
			++count;
		}
		return count;
	}

	inline bool Fabricate(const str& sourceSDKRoot, const std::filesystem::path& destination, std::string_view commit,
						  std::string_view version = {}, std::string_view target = {}) {
		namespace fs = std::filesystem;
		std::error_code ec;

		fs::path source(sourceSDKRoot);
		if (!fs::exists(source / "ShalltearConfig.cmake", ec)) {
			return false;
		}
		fs::create_directories(destination, ec);

		for (fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code entryEc;
			str name = it->path().filename().generic_string();
			if (!it->is_regular_file(entryEc) || entryEc || name.rfind("Shalltear", 0) != 0) {
				continue;
			}
			fs::copy_file(it->path(), destination / it->path().filename(), fs::copy_options::overwrite_existing, ec);
		}
		fs::copy_file(source / "BuildStamp.h.in", destination / "BuildStamp.h.in",
					  fs::copy_options::overwrite_existing, ec);

		Path config((destination / "ShalltearConfig.cmake").generic_string());
		str text = FileSystem::ReadTextFile(config, MemTag::Editor);
		str previousVersion;
		if (!GetCMakeString(text, "SHALLTEAR_VERSION", previousVersion)) {
			return false;
		}
		if (!SetCMakeString(text, "SHALLTEAR_GIT_COMMIT", commit)) {
			return false;
		}
		if (!version.empty() && !SetCMakeString(text, "SHALLTEAR_VERSION", version)) {
			return false;
		}
		if (!target.empty() && !SetCMakeString(text, "SHALLTEAR_COMPILER_TARGET", target)) {
			return false;
		}
		if (!FileSystem::WriteTextFile(config, text)) {
			return false;
		}

		if (version.empty()) {
			return true;
		}
		Path versionFile((destination / "ShalltearConfigVersion.cmake").generic_string());
		str versionText = FileSystem::ReadTextFile(versionFile, MemTag::Editor);
		if (ReplaceAll(versionText, previousVersion, version) == 0) {
			return false;
		}
		return FileSystem::WriteTextFile(versionFile, versionText);
	}

} // namespace Shalltear::StaleSDK
