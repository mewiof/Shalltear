#include "NodeShaderDocuments.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphShading.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Editor/EditorDocuments.h"
#include "Shalltear/Editor/GraphDocument.h"

namespace Shalltear {

	namespace {

		struct Entry {
			Guid asset;
			GraphDocument document;
			str saved;
			NodeShaderDocuments::Upgrade upgrade = nullptr;
			u32 opens = 0;
			u64 revision = 0;
			bool history = false;
			str editStart;
			u32 editDepth = 0;
			u64 measuredRevision = 0;
			bool unsaved = false;
		};

		struct Table {
			Vector<Entry*> entries{GetMemoryResource(MemTag::Editor)};
		};
		Table* _table = nullptr;
		u64 _nextRevision = 0;
		u64 _saveSerial = 0;

		Entry* Find(const Guid& asset) {
			if (!_table) {
				return nullptr;
			}
			for (Entry* entry : _table->entries) {
				if (entry->asset == asset) {
					return entry;
				}
			}
			return nullptr;
		}

		void Bump(Entry& entry) { entry.revision = ++_nextRevision; }

		bool ReadFile(const Path& path, NodeShaderDocuments::Upgrade upgrade, GraphDocument& out, str& text, str& error) {
			text = FileSystem::ReadTextFile(FileSystem::Resolve(path), MemTag::Editor);
			if (text.empty()) {
				error = "'" + path.ToString() + "' is empty or unreadable";
				return false;
			}
			GraphDocument read;
			if (!GraphDocumentOps::Read(text, read, error)) {
				return false;
			}
			const bool subgraph = read.type == GRAPH_SUBGRAPH_DOCUMENT_TYPE;
			const bool effect = IsEffectDocumentType(read.type);
			if (read.type != GRAPH_SHADER_DOCUMENT_TYPE && !subgraph && !effect) {
				error = "'" + path.ToString() + "' is a " + read.type + " document (not a node shader, a subgraph, an effect graph or an effect subgraph)";
				return false;
			}
			if (upgrade) {
				upgrade(read);
			}
			text = GraphDocumentOps::Write(read);
			out = std::move(read);
			return true;
		}

	} // anonymous namespace

	namespace NodeShaderDocuments {

		bool Open(const Guid& asset, const void* owner, str& error, Upgrade upgrade) {
			if (Entry* open = Find(asset)) {
				++open->opens;
				return true;
			}
			const Path path = AssetDatabase::LookupPath(asset);
			if (!asset.IsValid() || path.ToString().empty()) {
				error = "the graph has no path in the project";
				return false;
			}
			GraphDocument document;
			str text;
			if (!ReadFile(path, upgrade, document, text, error)) {
				return false;
			}
			if (!_table) {
				_table = New<Table>(MemTag::Editor);
			}
			Entry* entry = New<Entry>(MemTag::Editor);
			entry->asset = asset;
			entry->document = std::move(document);
			entry->saved = std::move(text);
			entry->upgrade = upgrade;
			entry->opens = 1;
			Bump(*entry);
			_table->entries.push_back(entry);

			EditorDocumentDesc desc;
			desc.asset = asset;
			desc.owner = owner;
			desc.capture = [asset](str& out) {
				if (const Entry* held = Find(asset)) {
					out = GraphDocumentOps::Write(held->document);
				}
			};
			desc.restore = [asset](std::string_view bytes) {
				Entry* held = Find(asset);
				if (!held) {
					return false;
				}
				GraphDocument restored;
				str ignored;
				if (!GraphDocumentOps::Read(bytes, restored, ignored)) {
					return false;
				}
				held->document = std::move(restored);
				Bump(*held);
				return true;
			};
			entry->history = EditorDocuments::Open(std::move(desc));
			return true;
		}

		void Close(const Guid& asset) {
			Entry* entry = Find(asset);
			if (!entry || --entry->opens > 0) {
				return;
			}
			if (entry->history) {
				EditorDocuments::Close(asset);
			}
			std::erase(_table->entries, entry);
			Delete(entry);
			if (_table->entries.empty()) {
				Delete(_table);
				_table = nullptr;
			}
		}

		bool IsOpen(const Guid& asset) { return Find(asset) != nullptr; }

		GraphDocument* Get(const Guid& asset) {
			Entry* entry = Find(asset);
			return entry ? &entry->document : nullptr;
		}

		void BeginEdit(const Guid& asset, std::string_view label) {
			Entry* entry = Find(asset);
			if (!entry) {
				return;
			}
			if (entry->editDepth++ == 0) {
				entry->editStart = GraphDocumentOps::Write(entry->document);
			}
			if (entry->history) {
				EditorDocuments::BeginEdit(asset, label);
			}
		}

		void EndEdit(const Guid& asset) {
			Entry* entry = Find(asset);
			if (!entry || entry->editDepth == 0) {
				return;
			}
			const u64 before = entry->revision;
			if (entry->history) {
				EditorDocuments::EndEdit(asset);
			}
			if (--entry->editDepth > 0) {
				return;
			}
			if (entry->revision == before && GraphDocumentOps::Write(entry->document) != entry->editStart) {
				Bump(*entry);
			}
			entry->editStart.clear();
		}

		u64 GetRevision(const Guid& asset) {
			const Entry* entry = Find(asset);
			return entry ? entry->revision : 0;
		}

		bool HasUnsavedChanges(const Guid& asset) {
			Entry* entry = Find(asset);
			if (!entry) {
				return false;
			}
			if (entry->measuredRevision != entry->revision) {
				entry->measuredRevision = entry->revision;
				entry->unsaved = GraphDocumentOps::Write(entry->document) != entry->saved;
			}
			return entry->unsaved;
		}

		bool Save(const Guid& asset) {
			Entry* entry = Find(asset);
			if (!entry) {
				return false;
			}
			const Path path = AssetDatabase::LookupPath(asset);
			str text = GraphDocumentOps::Write(entry->document);
			if (path.ToString().empty() || !FileSystem::WriteTextFile(FileSystem::Resolve(path), text)) {
				SLT_LOG_ERROR("NodeShader", "couldn't save '{}'", path.ToString());
				return false;
			}
			entry->saved = std::move(text);
			Bump(*entry);
			++_saveSerial;
			return true;
		}

		Guid SaveAs(const Guid& asset, const Path& path) {
			const Entry* entry = Find(asset);
			if (!entry || path.ToString().empty()) {
				return Guid{};
			}
			const str text = GraphDocumentOps::Write(entry->document);
			const Path physical = FileSystem::Resolve(path);
			if (!FileSystem::WriteTextFile(physical, text)) {
				SLT_LOG_ERROR("NodeShader", "couldn't save the graph as '{}'", path.ToString());
				return Guid{};
			}
			++_saveSerial;
			Asset* loaded = AssetDatabase::LoadAsset(path);
			if (!loaded) {
				SLT_LOG_ERROR("NodeShader", "the graph saved as '{}' didn't import", path.ToString());
				return AssetDatabase::LookupGuid(path);
			}
			const Guid created = loaded->assetId;
			AssetDatabase::UnloadAsset(loaded);
			return created;
		}

		bool Discard(const Guid& asset) {
			Entry* entry = Find(asset);
			if (!entry) {
				return false;
			}
			GraphDocument document;
			str text;
			str error;
			if (!ReadFile(AssetDatabase::LookupPath(asset), entry->upgrade, document, text, error)) {
				SLT_LOG_ERROR("NodeShader", "couldn't discard: {}", error);
				return false;
			}
			entry->document = std::move(document);
			entry->saved = std::move(text);
			Bump(*entry);
			return true;
		}

		Path GetPath(const Guid& asset) { return AssetDatabase::LookupPath(asset); }

		u64 GetSaveSerial() { return _saveSerial; }

	} // namespace NodeShaderDocuments

} // namespace Shalltear
