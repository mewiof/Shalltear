#include "Shalltear/Core/Config.h"

#include "Shalltear/Core/Log.h"

#include <charconv>

namespace Shalltear {

	namespace {

		std::string_view TrimView(std::string_view text) {
			size_t begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string_view::npos) {
				return {};
			}
			size_t end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}

		std::string_view Unquote(std::string_view text) {
			if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
				return text.substr(1, text.size() - 2);
			}
			return text;
		}

		bool NeedsQuoting(std::string_view value) {
			if (value.empty()) {
				return false;
			}
			return value.front() == ' ' || value.front() == '\t' || value.front() == '"' ||
				   value.back() == ' ' || value.back() == '\t';
		}

	} // anonymous namespace

	void Config::Parse(std::string_view text) {
		Clear();

		size_t lineNumber = 0;
		str currentSection;
		while (!text.empty()) {
			size_t lineEnd = text.find('\n');
			std::string_view line = text.substr(0, lineEnd);
			text.remove_prefix(lineEnd == std::string_view::npos ? text.size() : lineEnd + 1);
			++lineNumber;

			line = TrimView(line);
			if (line.empty() || line.front() == ';' || line.front() == '#') {
				continue;
			}

			if (line.front() == '[') {
				size_t close = line.find(']');
				if (close == std::string_view::npos) {
					SLT_LOG_WARN("Core", "Config: unterminated section header at line {0}. Skipping", lineNumber);
					continue;
				}
				currentSection = str(TrimView(line.substr(1, close - 1)));
				GetOrAddSection(currentSection);
				continue;
			}

			size_t equals = line.find('=');
			if (equals == std::string_view::npos) {
				SLT_LOG_WARN("Core", "Config: no '=' at line {0}. Skipping", lineNumber);
				continue;
			}

			std::string_view key = TrimView(line.substr(0, equals));
			if (key.empty()) {
				SLT_LOG_WARN("Core", "Config: empty key at line {0}. Skipping", lineNumber);
				continue;
			}
			std::string_view value = Unquote(TrimView(line.substr(equals + 1)));
			SetString(currentSection, key, value);
		}
	}

	str Config::Serialize() const {
		str out;
		bool first = true;
		for (const Section& section : _sections) {
			if (section.name.empty() && section.entries.empty()) {
				continue;
			}
			if (!first) {
				out += '\n';
			}
			first = false;
			if (!section.name.empty()) {
				out += '[';
				out += section.name;
				out += "]\n";
			}
			for (const Entry& entry : section.entries) {
				out += entry.key;
				out += " = ";
				if (NeedsQuoting(entry.value)) {
					out += '"';
					out += entry.value;
					out += '"';
				} else {
					out += entry.value;
				}
				out += '\n';
			}
		}
		return out;
	}

	bool Config::Load(const Path& path) {
		if (!FileSystem::Exists(path)) {
			return false;
		}
		Parse(FileSystem::ReadTextFile(path));
		return true;
	}

	bool Config::Save(const Path& path) const {
		return FileSystem::WriteTextFile(path, Serialize());
	}

	str Config::GetString(std::string_view section, std::string_view key, std::string_view fallback) const {
		const str* value = FindValue(section, key);
		return value ? *value : str(fallback);
	}

	bool Config::GetBool(std::string_view section, std::string_view key, bool fallback) const {
		const str* value = FindValue(section, key);
		if (!value) {
			return fallback;
		}
		str lowered;
		lowered.reserve(value->size());
		for (char c : *value) {
			lowered += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
		}
		if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
			return true;
		}
		if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
			return false;
		}
		return fallback;
	}

	i64 Config::GetI64(std::string_view section, std::string_view key, i64 fallback) const {
		const str* value = FindValue(section, key);
		if (!value) {
			return fallback;
		}
		i64 parsed = 0;
		const char* begin = value->data();
		const char* end = begin + value->size();
		auto result = std::from_chars(begin, end, parsed);
		if (result.ec != std::errc{} || result.ptr != end) {
			return fallback;
		}
		return parsed;
	}

	f64 Config::GetF64(std::string_view section, std::string_view key, f64 fallback) const {
		const str* value = FindValue(section, key);
		if (!value) {
			return fallback;
		}
		f64 parsed = 0.0;
		const char* begin = value->data();
		const char* end = begin + value->size();
		auto result = std::from_chars(begin, end, parsed);
		if (result.ec != std::errc{} || result.ptr != end) {
			return fallback;
		}
		return parsed;
	}

	void Config::SetString(std::string_view section, std::string_view key, std::string_view value) {
		Section& target = GetOrAddSection(section);
		for (Entry& entry : target.entries) {
			if (entry.key == key) {
				entry.value = str(value);
				return;
			}
		}
		target.entries.push_back({str(key), str(value)});
	}

	void Config::SetBool(std::string_view section, std::string_view key, bool value) {
		SetString(section, key, value ? "true" : "false");
	}

	void Config::SetI64(std::string_view section, std::string_view key, i64 value) {
		SetString(section, key, std::format("{}", value));
	}

	void Config::SetF64(std::string_view section, std::string_view key, f64 value) {
		SetString(section, key, std::format("{}", value));
	}

	bool Config::Has(std::string_view section, std::string_view key) const {
		return FindValue(section, key) != nullptr;
	}

	bool Config::HasSection(std::string_view section) const {
		return FindSection(section) != nullptr;
	}

	bool Config::Remove(std::string_view section, std::string_view key) {
		for (Section& s : _sections) {
			if (s.name != section) {
				continue;
			}
			for (size_t i = 0; i < s.entries.size(); ++i) {
				if (s.entries[i].key == key) {
					s.entries.erase(s.entries.begin() + static_cast<ptrdiff_t>(i));
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool Config::RemoveSection(std::string_view section) {
		for (size_t i = 0; i < _sections.size(); ++i) {
			if (_sections[i].name == section) {
				_sections.erase(_sections.begin() + static_cast<ptrdiff_t>(i));
				return true;
			}
		}
		return false;
	}

	void Config::Clear() {
		_sections.clear();
	}

	const Config::Section* Config::FindSection(std::string_view name) const {
		for (const Section& section : _sections) {
			if (section.name == name) {
				return &section;
			}
		}
		return nullptr;
	}

	Config::Section& Config::GetOrAddSection(std::string_view name) {
		for (Section& section : _sections) {
			if (section.name == name) {
				return section;
			}
		}
		if (!name.empty() && _sections.empty()) {
			_sections.push_back({str{}, {}});
		}
		_sections.push_back({str(name), {}});
		return _sections.back();
	}

	const str* Config::FindValue(std::string_view section, std::string_view key) const {
		const Section* s = FindSection(section);
		if (!s) {
			return nullptr;
		}
		for (const Entry& entry : s->entries) {
			if (entry.key == key) {
				return &entry.value;
			}
		}
		return nullptr;
	}

} // namespace Shalltear
