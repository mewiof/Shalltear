#include "Shalltear/Core/Scene/SceneAsset.h"

#include "SceneDocument.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"

namespace Shalltear {

	namespace SceneInternal {

		namespace {

			void NoteLayerAssetRefs(const OverrideLayer& layer, CookWriter& payload) {
				for (const PropertyOverrideOp& op : layer.properties) {
					if (op.value.isHandle && IsAssetGuid(op.value.handleGuid)) {
						payload.NoteAssetRef(op.value.handleGuid);
					}
				}
			}

		} // namespace

		bool CookDocumentAsset(const SceneDocument* document, std::string_view virtualPath, CookWriter& payload) {
			if (!document) {
				SLT_LOG_ERROR("Scene", "cooking '{}': the asset holds no document", virtualPath);
				return false;
			}
			Buffer bytes = SceneInternal::WriteSceneDocumentBinary(*document);
			if (!bytes) {
				SLT_LOG_ERROR("Scene", "cooking '{}': a row's fields couldn't be re-encoded", virtualPath);
				return false;
			}
			payload.WriteBytes(bytes.data, bytes.size);

			if (document->basePrefab.IsValid()) {
				payload.NoteAssetRef(document->basePrefab);
			}
			NoteLayerAssetRefs(document->prefabOverrides, payload);
			for (const SceneDocumentRow& row : document->rows) {
				if (row.kind == SceneDocumentRow::Kind::Instance) {
					payload.NoteAssetRef(row.prefab);
					NoteLayerAssetRefs(row.overrides, payload);
				}
			}
			return true;
		}

		SceneDocument* LoadCookedDocument(CookReader& payload, std::string_view virtualPath) {
			size_t size = payload.Remaining();
			const u8* bytes = payload.ReadSpan(size);
			if (payload.Failed() || !bytes) {
				return nullptr;
			}
			Buffer owned(size, MemTag::Scene);
			std::memcpy(owned.data, bytes, size);
			SceneDocument* document = SceneInternal::ParseSceneDocument(std::move(owned));
			if (!document) {
				SLT_LOG_ERROR("Scene", "cooked '{}' is a malformed document", virtualPath);
			}
			return document;
		}

	} // namespace SceneInternal

	SceneAsset::SceneAsset() = default;

	SceneAsset::~SceneAsset() {
		if (_document) {
			Delete(_document);
			_document = nullptr;
		}
	}

	bool SceneAsset::Import(const Path& sourcePath) {
		Buffer bytes = FileSystem::ReadFile(sourcePath, MemTag::Scene);
		if (!bytes) {
			SLT_LOG_ERROR("Scene", "SceneAsset: can't read '{}'", sourcePath.ToString());
			return false;
		}
		SceneDocument* document = SceneInternal::ParseSceneDocument(std::move(bytes));
		if (!document) {
			SLT_LOG_ERROR("Scene", "SceneAsset: malformed document '{}'", sourcePath.ToString());
			return false;
		}
		if (_document) {
			Delete(_document);
		}
		_document = document;
		return true;
	}

	bool SceneAsset::Export(const Path& destPath) const {
		if (!_document) {
			return false;
		}
		if (_document->binary) {
			Buffer bytes = SceneInternal::WriteSceneDocumentBinary(*_document);
			return bytes && FileSystem::WriteFile(destPath, bytes);
		}
		str text = SceneInternal::WriteSceneDocumentText(*_document);
		return !text.empty() && FileSystem::WriteTextFile(destPath, text);
	}

	bool SceneAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)descriptor;
		return SceneInternal::CookDocumentAsset(_document, virtualPath, payload);
	}

	bool SceneAsset::CookImportState(CookWriter& importState) const {
		(void)importState;
		return false;
	}

	bool SceneAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)descriptor;
		SceneDocument* document = SceneInternal::LoadCookedDocument(payload, virtualPath);
		if (!document) {
			return false;
		}
		if (_document) {
			Delete(_document);
		}
		_document = document;
		return true;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SceneAsset)
