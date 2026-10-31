#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <string_view>
#include <vector>

namespace Shalltear {

	class SHALLTEAR_CORE_API Config {
	public:
		void Parse(std::string_view text);

		str Serialize() const;

		bool Load(const Path& path);
		bool Save(const Path& path) const;

		str GetString(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
		bool GetBool(std::string_view section, std::string_view key, bool fallback = false) const;
		i64 GetI64(std::string_view section, std::string_view key, i64 fallback = 0) const;
		f64 GetF64(std::string_view section, std::string_view key, f64 fallback = 0.0) const;

		void SetString(std::string_view section, std::string_view key, std::string_view value);
		void SetBool(std::string_view section, std::string_view key, bool value);
		void SetI64(std::string_view section, std::string_view key, i64 value);
		void SetF64(std::string_view section, std::string_view key, f64 value);

		bool Has(std::string_view section, std::string_view key) const;
		bool HasSection(std::string_view section) const;
		bool Remove(std::string_view section, std::string_view key);
		bool RemoveSection(std::string_view section);
		void Clear();
	private:
		struct Entry {
			str key;
			str value;
		};
		struct Section {
			str name;
			std::vector<Entry> entries;
		};

		const Section* FindSection(std::string_view name) const;
		Section& GetOrAddSection(std::string_view name);
		const str* FindValue(std::string_view section, std::string_view key) const;

		std::vector<Section> _sections;
	};

} // namespace Shalltear
