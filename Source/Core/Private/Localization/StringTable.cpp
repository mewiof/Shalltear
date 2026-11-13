#include "Shalltear/Core/Localization.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace Shalltear {

	namespace {

		constexpr std::string_view NewTableText = "key,en\ngreeting,Hello\n";

		std::string_view Trim(std::string_view text) {
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
				text.remove_prefix(1);
			}
			while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
				text.remove_suffix(1);
			}
			return text;
		}

		bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
					   const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
					   return lower(x) == lower(y);
				   });
		}

		bool ReadRows(std::string_view text, Vector<Vector<str>>& rows, str& error) {
			if (text.starts_with("\xEF\xBB\xBF")) {
				text.remove_prefix(3);
			}
			u32 line = 1;
			size_t at = 0;
			while (at < text.size()) {
				Vector<str>& row = rows.emplace_back();
				const u32 rowLine = line;
				for (;;) {
					str& field = row.emplace_back();
					if (at < text.size() && text[at] == '"') {
						++at;
						bool closed = false;
						while (at < text.size()) {
							const char c = text[at++];
							if (c == '"') {
								if (at < text.size() && text[at] == '"') {
									field.push_back('"');
									++at;
									continue;
								}
								closed = true;
								break;
							}
							line += c == '\n' ? 1u : 0u;
							field.push_back(c);
						}
						if (!closed) {
							error = std::format("row {}: unclosed quoted field", rowLine);
							return false;
						}
						if (at < text.size() && text[at] != ',' && text[at] != '\n' && text[at] != '\r') {
							error = std::format("row {}: text after closing quote", rowLine);
							return false;
						}
					} else {
						while (at < text.size() && text[at] != ',' && text[at] != '\n' && text[at] != '\r') {
							field.push_back(text[at++]);
						}
					}
					if (at < text.size() && text[at] == ',') {
						++at;
						continue;
					}
					if (at < text.size() && text[at] == '\r') {
						++at;
					}
					if (at < text.size() && text[at] == '\n') {
						++at;
					}
					++line;
					break;
				}
			}
			return true;
		}

		void WriteText(CookWriter& payload, std::string_view text) {
			payload.Write(static_cast<u32>(text.size()));
			payload.WriteBytes(text.data(), text.size());
		}

		bool ReadText(CookReader& payload, str& out) {
			u32 size = 0;
			if (!payload.Read(size) || size > payload.Remaining()) {
				return false;
			}
			const u8* bytes = payload.ReadSpan(size);
			if (size > 0 && !bytes) {
				return false;
			}
			out.assign(reinterpret_cast<const char*>(bytes), size);
			return true;
		}

		bool ReadTexts(CookReader& payload, Vector<str>& out, size_t count) {
			out.clear();
			if (count > payload.Remaining() / sizeof(u32)) {
				return false;
			}
			out.resize(count);
			for (str& text : out) {
				if (!ReadText(payload, text)) {
					return false;
				}
			}
			return true;
		}

	} // anonymous namespace

	StringTableAsset::~StringTableAsset() = default;

	bool StringTableAsset::Parse(std::string_view csv, str& error) {
		Vector<Vector<str>> rows{GetMemoryResource(MemTag::Asset)};
		if (!ReadRows(csv, rows, error)) {
			return false;
		}
		if (rows.empty()) {
			error = "empty (expected 'key,<language>' header)";
			return false;
		}
		const Vector<str>& header = rows.front();
		if (!EqualsIgnoreCase(Trim(header.front()), "key")) {
			error = std::format("1st header cell is '{}' (expected 'key')", Trim(header.front()));
			return false;
		}

		Vector<str> parsedLanguages{GetMemoryResource(MemTag::Asset)};
		for (size_t column = 1; column < header.size(); ++column) {
			const str code = Localization::NormalizeLanguage(Trim(header[column]));
			if (code.empty()) {
				error = std::format("header column {} has no language", column + 1);
				return false;
			}
			if (std::find(parsedLanguages.begin(), parsedLanguages.end(), code) != parsedLanguages.end()) {
				error = std::format("header has '{}' twice", code);
				return false;
			}
			parsedLanguages.push_back(code);
		}

		Vector<str> parsedKeys{GetMemoryResource(MemTag::Asset)};
		Vector<str> parsedCells{GetMemoryResource(MemTag::Asset)};
		HashMap<str, u32, KeyHash, std::equal_to<>> seen{GetMemoryResource(MemTag::Asset)};
		for (size_t r = 1; r < rows.size(); ++r) {
			const Vector<str>& row = rows[r];
			const std::string_view key = Trim(row.front());
			if (key.empty()) {
				if (std::any_of(row.begin(), row.end(), [](const str& cell) { return !Trim(cell).empty(); })) {
					SLT_LOG_WARN("Localization", "'{}' row {} has text but no key (skipped)", virtualPath, r + 1);
				}
				continue;
			}
			if (seen.find(key) != seen.end()) {
				SLT_LOG_WARN("Localization", "'{}' row {} has duplicate key '{}' (1st used)", virtualPath, r + 1, key);
				continue;
			}
			if (row.size() > parsedLanguages.size() + 1) {
				SLT_LOG_WARN("Localization", "'{}' row {} has {} cell(s) past header (ignored)", virtualPath, r + 1,
							 row.size() - parsedLanguages.size() - 1);
			}
			seen.emplace(str(key), static_cast<u32>(parsedKeys.size()));
			parsedKeys.emplace_back(key);
			for (size_t column = 0; column < parsedLanguages.size(); ++column) {
				parsedCells.push_back(column + 1 < row.size() ? row[column + 1] : str{});
			}
		}

		languages = std::move(parsedLanguages);
		keys = std::move(parsedKeys);
		cells = std::move(parsedCells);
		IndexRows();
		return true;
	}

	void StringTableAsset::IndexRows() {
		_rows.clear();
		for (u32 row = 0; row < keys.size(); ++row) {
			_rows.emplace(keys[row], row);
		}
	}

	i32 StringTableAsset::FindLanguage(std::string_view code) const {
		for (size_t i = 0; i < languages.size(); ++i) {
			if (languages[i] == code) {
				return static_cast<i32>(i);
			}
		}
		return -1;
	}

	std::string_view StringTableAsset::Find(std::string_view key, u32 language) const {
		if (language >= languages.size()) {
			return {};
		}
		const auto found = _rows.find(key);
		if (found == _rows.end()) {
			return {};
		}
		const size_t cell = static_cast<size_t>(found->second) * languages.size() + language;
		return cell < cells.size() ? std::string_view(cells[cell]) : std::string_view{};
	}

	bool StringTableAsset::Import(const Path& sourcePath) {
		if (!FileSystem::Exists(sourcePath)) {
			SLT_LOG_ERROR("Localization", "string table '{}' doesn't exist", sourcePath.ToString());
			return false;
		}
		const str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		str error;
		if (!Parse(text, error)) {
			SLT_LOG_ERROR("Localization", "'{}' isn't a string table: {}", sourcePath.ToString(), error);
			return false;
		}
		return true;
	}

	void StringTableAsset::AdoptReimport(Asset& fresh) {
		StringTableAsset& imported = static_cast<StringTableAsset&>(fresh);
		if (imported.languages == languages && imported.keys == keys && imported.cells == cells) {
			return;
		}
		languages = std::move(imported.languages);
		keys = std::move(imported.keys);
		cells = std::move(imported.cells);
		IndexRows();
		++version;
	}

	bool StringTableAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.Write(static_cast<u32>(languages.size()));
		descriptor.Write(static_cast<u32>(keys.size()));
		for (const str& language : languages) {
			WriteText(payload, language);
		}
		for (const str& key : keys) {
			WriteText(payload, key);
		}
		for (const str& cell : cells) {
			WriteText(payload, cell);
		}
		return true;
	}

	bool StringTableAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		u32 languageCount = 0;
		u32 keyCount = 0;
		if (!descriptor.Read(languageCount) || !descriptor.Read(keyCount)) {
			return false;
		}
		const size_t cellCount = static_cast<size_t>(languageCount) * keyCount;
		if (!ReadTexts(payload, languages, languageCount) || !ReadTexts(payload, keys, keyCount) ||
			!ReadTexts(payload, cells, cellCount)) {
			return false;
		}
		IndexRows();
		return true;
	}

	Asset* StringTableImporter::Import(const Path& sourcePath) {
		StringTableAsset* asset = New<StringTableAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	bool StringTableImporter::CreateAssetSource(Buffer& out) {
		out.Allocate(NewTableText.size(), MemTag::Asset);
		if (!out) {
			return false;
		}
		std::memcpy(out.data, NewTableText.data(), NewTableText.size());
		return true;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::StringTableAsset)
