#include "Shalltear/Editor/EditorDocuments.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include "Shalltear/Editor/UndoSystem.h"

#include <algorithm>
#include <cstdint>

namespace Shalltear {

	namespace {

		struct Entry {
			Guid asset;
			const void* owner = nullptr;
			std::function<void(str&)> capture;
			std::function<bool(std::string_view)> restore;
			u32 opens = 0;
			u64 revision = 0;

			u32 editDepth = 0;
			str label;
			str before;
		};

		struct Registry {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
			u64 lastRevision = 0;
		};

		Registry* _registry = nullptr;

		Registry& Get() {
			if (!_registry) {
				_registry = New<Registry>(MemTag::Editor);
			}
			return *_registry;
		}

		size_t IndexOf(const Guid& asset) {
			if (!_registry || !asset.IsValid()) {
				return SIZE_MAX;
			}
			for (size_t index = 0; index < _registry->entries.size(); ++index) {
				if (_registry->entries[index].asset == asset) {
					return index;
				}
			}
			return SIZE_MAX;
		}

		void Abandon(Entry& entry) {
			if (entry.editDepth == 0) {
				return;
			}
			entry.editDepth = 0;
			entry.before.clear();
			UndoSystem::EndGroup();
		}

		void Capture(size_t index, str& out) {
			const std::function<void(str&)> capture = _registry->entries[index].capture;
			capture(out);
		}

		bool Restore(const Guid& asset, std::string_view bytes) {
			const size_t index = IndexOf(asset);
			if (index == SIZE_MAX) {
				SLT_LOG_WARN("Editor", "undo skipped: the document it edits is no longer open");
				return false;
			}
			const std::function<bool(std::string_view)> restore = _registry->entries[index].restore;
			if (!restore(bytes)) {
				SLT_LOG_WARN("Editor", "undo skipped: the document's owner couldn't read its snapshot");
				return false;
			}
			const size_t after = IndexOf(asset);
			if (after != SIZE_MAX) {
				_registry->entries[after].revision = ++_registry->lastRevision;
			}
			return true;
		}

		class DocumentSnapshotCommand final : public IEditorCommand {
		public:
			DocumentSnapshotCommand(const Guid& asset, str before, str after, str label)
				: _asset(asset), _before(std::move(before)), _after(std::move(after)), _label(std::move(label)) {}

			void Apply() override { Restore(_asset, _after); }
			void Revert() override { Restore(_asset, _before); }
			str GetLabel() const override { return _label.empty() ? str("Edit Graph") : _label; }
		private:
			Guid _asset;
			str _before;
			str _after;
			str _label;
		};

	} // anonymous namespace

	namespace EditorDocuments {

		bool Open(EditorDocumentDesc desc) {
			if (!desc.asset.IsValid() || !desc.capture || !desc.restore) {
				return false;
			}
			Registry& registry = Get();
			const size_t index = IndexOf(desc.asset);
			if (index != SIZE_MAX) {
				Entry& entry = registry.entries[index];
				if (entry.owner != desc.owner) {
					SLT_LOG_WARN("Editor", "a document already open in another module was opened again (edits aren't undoable here)");
					return false;
				}
				++entry.opens;
				return true;
			}
			Entry entry;
			entry.asset = desc.asset;
			entry.owner = desc.owner;
			entry.capture = std::move(desc.capture);
			entry.restore = std::move(desc.restore);
			entry.opens = 1;
			entry.revision = ++registry.lastRevision;
			registry.entries.push_back(std::move(entry));
			return true;
		}

		void Close(const Guid& asset) {
			const size_t index = IndexOf(asset);
			if (index == SIZE_MAX) {
				return;
			}
			Entry& entry = _registry->entries[index];
			if (--entry.opens > 0) {
				return;
			}
			Abandon(entry);
			_registry->entries.erase(_registry->entries.begin() + static_cast<std::ptrdiff_t>(index));
		}

		bool IsOpen(const Guid& asset) {
			return IndexOf(asset) != SIZE_MAX;
		}

		void BeginEdit(const Guid& asset, std::string_view label) {
			const size_t index = IndexOf(asset);
			if (index == SIZE_MAX || _registry->entries[index].editDepth++ > 0) {
				return;
			}
			UndoSystem::BeginGroup(label);
			str before;
			Capture(index, before);
			const size_t at = IndexOf(asset);
			if (at == SIZE_MAX) {
				UndoSystem::EndGroup();
				return;
			}
			_registry->entries[at].label = str(label);
			_registry->entries[at].before = std::move(before);
		}

		void EndEdit(const Guid& asset) {
			const size_t index = IndexOf(asset);
			if (index == SIZE_MAX || _registry->entries[index].editDepth == 0 || --_registry->entries[index].editDepth > 0) {
				return;
			}
			str before = std::move(_registry->entries[index].before);
			str label = std::move(_registry->entries[index].label);
			str after;
			Capture(index, after);
			if (after != before) {
				UndoSystem::Push(MakeUnique<DocumentSnapshotCommand>(asset, std::move(before), std::move(after), std::move(label)));
			}
			UndoSystem::EndGroup();
		}

		u64 GetRevision(const Guid& asset) {
			const size_t index = IndexOf(asset);
			return index == SIZE_MAX ? 0 : _registry->entries[index].revision;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (!owner || !_registry) {
				return 0;
			}
			u32 removed = 0;
			for (Entry& entry : _registry->entries) {
				if (entry.owner == owner) {
					Abandon(entry);
					++removed;
				}
			}
			std::erase_if(_registry->entries, [owner](const Entry& entry) { return entry.owner == owner; });
			return removed;
		}

		void Terminate() {
			if (!_registry) {
				return;
			}
			for (Entry& entry : _registry->entries) {
				Abandon(entry);
			}
			Delete(_registry);
			_registry = nullptr;
		}

	} // namespace EditorDocuments

} // namespace Shalltear
