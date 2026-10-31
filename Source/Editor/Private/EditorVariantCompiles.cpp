#include "Shalltear/Editor/EditorVariantCompiles.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Editor/EditorPreviews.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct Pending {
			ShaderCompileTicket ticket = 0;
			Guid shader;
			u32 version = 0;
			u64 keywordHash = 0;
			Vector<str> keywords{GetMemoryResource(MemTag::Editor)};
		};

		struct Table {
			Vector<Pending> pending{GetMemoryResource(MemTag::Editor)};
		};
		Table* _table = nullptr;

	} // anonymous namespace

	namespace EditorVariantCompiles {

		void Request(ShaderAsset& shader, std::span<const str> enabled) {
			if (enabled.empty() || shader.dependencies.empty()) {
				return;
			}
			const u64 hash = ShaderAsset::KeywordSetHash(enabled);
			if (shader.FindVariant(hash) != SHADER_BASE_VARIANT) {
				return;
			}
			if (_table && std::any_of(_table->pending.begin(), _table->pending.end(), [&](const Pending& entry) {
					return entry.shader == shader.assetId && entry.keywordHash == hash;
				})) {
				return;
			}
			if (!_table) {
				_table = New<Table>(MemTag::Editor);
			}
			Pending entry;
			entry.ticket = ShaderCompileQueue::Submit(Path(shader.dependencies[0].physicalPath), enabled);
			entry.shader = shader.assetId;
			entry.version = shader.version;
			entry.keywordHash = hash;
			entry.keywords.assign(enabled.begin(), enabled.end());
			_table->pending.push_back(std::move(entry));
		}

		void Drain() {
			if (!_table) {
				return;
			}
			Vector<Pending> stale{GetMemoryResource(MemTag::Editor)};
			std::erase_if(_table->pending, [&stale](Pending& entry) {
				ShaderAsset* compiled = nullptr;
				const ShaderCompileStatus status = ShaderCompileQueue::Collect(entry.ticket, compiled);
				if (status == ShaderCompileStatus::Pending) {
					return false;
				}
				Asset* live = AssetDatabase::FindLoadedByGuid(entry.shader);
				ShaderAsset* shader = live && live->GetAssetTypeId() == StringHash("ShaderAsset") ? static_cast<ShaderAsset*>(live) : nullptr;
				if (shader && shader->version != entry.version) {
					stale.push_back(std::move(entry));
				} else if (shader && compiled) {
					shader->AdoptVariant(*compiled, entry.keywordHash);
					EditorPreviewStore::Invalidate(entry.shader);
				}
				if (compiled) {
					Delete(compiled);
				}
				return true;
			});
			for (const Pending& entry : stale) {
				if (Asset* live = AssetDatabase::FindLoadedByGuid(entry.shader)) {
					Request(*static_cast<ShaderAsset*>(live), {entry.keywords.data(), entry.keywords.size()});
				}
			}
			if (_table->pending.empty()) {
				Delete(_table);
				_table = nullptr;
			}
		}

		void Terminate() {
			if (!_table) {
				return;
			}
			for (const Pending& entry : _table->pending) {
				ShaderCompileQueue::Cancel(entry.ticket);
			}
			Delete(_table);
			_table = nullptr;
		}

	} // namespace EditorVariantCompiles

} // namespace Shalltear
