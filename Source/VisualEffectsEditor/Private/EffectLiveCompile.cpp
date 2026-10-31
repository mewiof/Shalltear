#include "EffectLiveCompile.h"

#include "EffectAsset.h"
#include "EffectGraphBuild.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Editor/GraphDocument.h"

#include "Shalltear/VisualEffects/VisualEffectAsset.h"

#include <algorithm>

namespace Shalltear::VisualEffectsEditor::EffectLiveCompiles {

	using namespace VisualEffectsImpl;

	namespace {

		constexpr std::string_view LIVE_LEAF = "Live";

		struct Entry {
			Guid asset;
			str graph;
			EffectBuild build;
			Vector<ShaderCompileTicket> tickets{GetMemoryResource(MemTag::Editor)};
			Vector<ShaderAsset*> collected{GetMemoryResource(MemTag::Editor)};
			bool running = false;
			bool refused = false;
			f64 started = 0.0;
			str waiting;
			GraphEffectCompileOptions waitingOptions;
			bool failed = false;
			u32 adopted = 0;
			u64 holds = 0;
		};

		struct Table {
			Vector<Entry*> entries{GetMemoryResource(MemTag::Editor)};
		};
		Table* _table = nullptr;

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

		Entry& Make(const Guid& asset) {
			if (Entry* found = Find(asset)) {
				return *found;
			}
			if (!_table) {
				_table = New<Table>(MemTag::Editor);
			}
			Entry* entry = New<Entry>(MemTag::Editor);
			entry->asset = asset;
			_table->entries.push_back(entry);
			return *entry;
		}

		void FreeCollected(Entry& entry) {
			for (ShaderAsset* shader : entry.collected) {
				if (shader) {
					Delete(shader);
				}
			}
			entry.collected.clear();
		}

		void CancelTickets(Entry& entry) {
			for (ShaderCompileTicket ticket : entry.tickets) {
				if (ticket != 0) {
					ShaderCompileQueue::Cancel(ticket);
				}
			}
			entry.tickets.clear();
		}

		bool Start(Entry& entry, const GraphDocument& document, Vector<GraphDiagnostic>& diagnostics, const GraphEffectCompileOptions& options) {
			const Path path = AssetDatabase::LookupPath(entry.asset);
			entry.graph = path.ToString();
			const str directory = GeneratedDirectory(FileSystem::Resolve(path), path, LIVE_LEAF);
			if (directory.empty()) {
				return false;
			}
			EffectBuild build;
			if (!Prepare(document, entry.graph, directory, build, &diagnostics, options)) {
				return false;
			}
			const u64 fingerprint = Fingerprint(build.effect, entry.graph);
			if (fingerprint == entry.holds && !entry.failed) {
				return true;
			}
			entry.holds = fingerprint;
			if (!WriteShaders(build, entry.graph)) {
				entry.failed = true;
				return true;
			}
			entry.build = std::move(build);
			entry.tickets.clear();
			for (const EffectShaderText& shader : entry.build.shaders) {
				entry.tickets.push_back(ShaderCompileQueue::Submit(Path(shader.file)));
			}
			entry.collected.assign(entry.build.shaders.size(), nullptr);
			entry.running = true;
			entry.refused = false;
			entry.started = Time::GetRealtimeSinceStartup();
			return true;
		}

		void Adopt(Entry& entry) {
			VisualEffectAsset* compiled = Assemble(entry.build, entry.graph, entry.collected);
			entry.collected.clear();
			if (!compiled) {
				entry.failed = true;
				return;
			}
			Asset* loaded = AssetDatabase::FindLoadedByGuid(entry.asset);
			if (loaded && TypeRegistry::IsDerivedFrom(loaded->GetType(), VisualEffectAsset::GetReflectType())) {
				EffectAssetAccess::Replace(static_cast<VisualEffectAsset&>(*loaded), *compiled);
				++entry.adopted;
			}
			Delete(compiled);
			entry.failed = false;
		}

	} // anonymous namespace

	bool Request(const Guid& asset, const GraphDocument& document, Vector<GraphDiagnostic>& diagnostics, const GraphEffectCompileOptions& options) {
		diagnostics.clear();
		Entry& entry = Make(asset);
		if (entry.running) {
			GraphEffect check;
			if (!GraphEffectTarget::Compile(document, AssetDatabase::LookupPath(asset).ToString(), check, diagnostics, nullptr, options)) {
				entry.waiting.clear();
				return false;
			}
			entry.waiting = GraphDocumentOps::Write(document);
			entry.waitingOptions = options;
			return true;
		}
		return Start(entry, document, diagnostics, options);
	}

	void Tick() {
		if (!_table) {
			return;
		}
		for (Entry* entry : _table->entries) {
			if (entry->running) {
				bool pending = false;
				for (size_t i = 0; i < entry->tickets.size(); ++i) {
					if (entry->tickets[i] == 0) {
						continue;
					}
					ShaderAsset* shader = nullptr;
					const ShaderCompileStatus status = ShaderCompileQueue::Collect(entry->tickets[i], shader);
					if (status == ShaderCompileStatus::Pending) {
						pending = true;
						continue;
					}
					entry->tickets[i] = 0;
					entry->collected[i] = status == ShaderCompileStatus::Succeeded ? shader : nullptr;
					entry->refused = entry->refused || status != ShaderCompileStatus::Succeeded;
				}
				if (pending) {
					continue;
				}
				entry->running = false;
				entry->tickets.clear();
				if (entry->refused) {
					FreeCollected(*entry);
					entry->failed = true;
					SLT_LOG_WARN("VisualEffects", "'{}' didn't compile (its effects play on as last compiled)", entry->graph);
				} else {
					Adopt(*entry);
				}
			}
			if (!entry->running && !entry->waiting.empty()) {
				GraphDocument document;
				str error;
				const str text = std::move(entry->waiting);
				entry->waiting.clear();
				Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
				if (GraphDocumentOps::Read(text, document, error)) {
					Start(*entry, document, diagnostics, entry->waitingOptions);
				}
			}
		}
	}

	void Seed(const Guid& asset, const GraphDocument& document) {
		Entry& entry = Make(asset);
		if (entry.running || entry.holds != 0) {
			return;
		}
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		const str graph = AssetDatabase::LookupPath(asset).ToString();
		if (GraphEffectTarget::Compile(document, graph, effect, diagnostics)) {
			entry.holds = Fingerprint(effect, graph);
		}
	}

	void Forget(const Guid& asset) {
		if (Entry* entry = Find(asset)) {
			entry->holds = 0;
		}
	}

	Status GetStatus(const Guid& asset) {
		Status status;
		if (const Entry* entry = Find(asset)) {
			status.compiling = entry->running || !entry->waiting.empty();
			status.seconds = entry->running ? Time::GetRealtimeSinceStartup() - entry->started : 0.0;
			status.failed = entry->failed;
			status.adopted = entry->adopted;
		}
		return status;
	}

	void Terminate() {
		if (!_table) {
			return;
		}
		for (Entry* entry : _table->entries) {
			CancelTickets(*entry);
			FreeCollected(*entry);
			Delete(entry);
		}
		Delete(_table);
		_table = nullptr;
	}

} // namespace Shalltear::VisualEffectsEditor::EffectLiveCompiles
