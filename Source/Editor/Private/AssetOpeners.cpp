#include "Shalltear/Editor/AssetOpeners.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>

namespace Shalltear::AssetOpeners {

	namespace {

		struct Entry {
			str extension;
			OpenFn open;
			const void* owner = nullptr;
		};

		struct Registry {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
		};

		Registry* _registry = nullptr;

		Registry& Get() {
			if (!_registry) {
				_registry = New<Registry>(MemTag::Editor);
			}
			return *_registry;
		}

		Entry* Find(std::string_view lowered) {
			if (!_registry) {
				return nullptr;
			}
			for (Entry& entry : _registry->entries) {
				if (entry.extension == lowered) {
					return &entry;
				}
			}
			return nullptr;
		}

	} // anonymous namespace

	bool Register(std::string_view extension, OpenFn open, const void* owner) {
		if (extension.size() < 2 || extension.front() != '.' || !open) {
			return false;
		}
		const str lowered = StringUtil::ToLower(extension);
		if (Entry* held = Find(lowered)) {
			if (held->owner != owner) {
				SLT_LOG_WARN("Editor", "'{}' already opens in another module's editor (2nd registration ignored)", lowered);
				return false;
			}
			held->open = std::move(open);
			return true;
		}
		Get().entries.push_back(Entry{lowered, std::move(open), owner});
		return true;
	}

	bool Has(std::string_view extension) {
		return Find(StringUtil::ToLower(extension)) != nullptr;
	}

	bool Open(const Path& virtualPath) {
		const Entry* entry = Find(StringUtil::ToLower(virtualPath.GetExtension()));
		if (!entry) {
			return false;
		}
		const Guid asset = AssetDatabase::LookupGuid(virtualPath);
		if (!asset.IsValid()) {
			return false;
		}
		const OpenFn open = entry->open;
		open(asset);
		return true;
	}

	u32 UnregisterOwnedBy(const void* owner) {
		if (!owner || !_registry) {
			return 0;
		}
		return static_cast<u32>(std::erase_if(_registry->entries, [owner](const Entry& entry) { return entry.owner == owner; }));
	}

	void Terminate() {
		if (_registry) {
			Delete(_registry);
			_registry = nullptr;
		}
	}

} // namespace Shalltear::AssetOpeners
