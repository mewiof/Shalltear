#pragma once

#include "../Serialization/SerializerInternal.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <chrono>
#include <deque>
#include <span>
#include <unordered_set>
#include <vector>

namespace Shalltear {

	class Scene;
	class SceneObject;
	class Component;
	class UnresolvedComponent;
	class CookWriter;
	class CookReader;
	struct TypeInfo;

	constexpr u64 SLT_OPAQUE_SENTINEL = 0x534C544F50415145ULL;

	struct SceneDocumentBlob {
		JSONValue json;
		std::vector<u8> binary;

		bool isOpaque = false;
		bool opaqueIsBinary = false;
		std::vector<u8> opaque;
	};

	struct SceneDocumentComponentRow {
		u64 id = 0;
		StringHash type;
		SceneDocumentBlob fields;
	};

	struct SceneDocumentRow {
		enum class Kind : u8 {
			Object,
			Instance,
		};

		u64 id = 0;
		u64 parent = 0;
		Kind kind = Kind::Object;

		StringHash type;
		SceneDocumentBlob fields;
		std::vector<SceneDocumentComponentRow> components;

		Guid prefab{};
		OverrideLayer overrides;
	};

	struct SceneDocument {
		u16 formatVersion = 1;
		u64 engineHash = 0;
		bool binary = false;
		str name;
		u64 nextLocalId = 1;

		bool isPrefab = false;
		Guid basePrefab{};
		OverrideLayer prefabOverrides;

		std::vector<SceneDocumentRow> rows;
		std::vector<SceneDocumentComponentRow> looseComponents;

		str sourceText;
		LinearAllocator* arena = nullptr;
		std::deque<str> pool;

		u32 preservedRows = 0;

		SceneDocument() = default;
		~SceneDocument() {
			if (arena) {
				Delete(arena);
				arena = nullptr;
			}
		}
		SceneDocument(const SceneDocument&) = delete;
		SceneDocument& operator=(const SceneDocument&) = delete;
	};

	namespace SceneInternal {

		str SceneNameFromPath(const Path& path);

		SceneDocument* ParseSceneDocument(Buffer&& fileBytes);

		str WriteSceneDocumentText(const SceneDocument& document);
		Buffer WriteSceneDocumentBinary(const SceneDocument& document);

		enum class BlobRow : u8 {
			Object,
			Component,
		};

		bool EncodeBlobBinary(const SceneDocument& document, const SceneDocumentBlob& blob, StringHash rowType,
							  BlobRow rowKind, std::vector<u8>& scratch, std::span<const u8>& out);

		void NormalizeOpaqueBlob(SceneDocumentBlob& blob, bool binaryDocument);

		bool CookDocumentAsset(const SceneDocument* document, std::string_view virtualPath, CookWriter& payload);
		SceneDocument* LoadCookedDocument(CookReader& payload, std::string_view virtualPath);

		SceneDocument* BuildDocumentFromScene(Scene& scene, bool binary);

		SceneDocument* BuildDocumentFromSubtree(SceneObject& root, bool binary);

		SceneDocument* BuildVariantDocument(const Guid& basePrefab, const Path& path);

		bool InstantiateDocument(const SceneDocument& document, Scene& scene);

		std::unordered_set<u64> ComputeLayerOwnedRows(const SceneDocument& document);

		bool IsPreservedComponentType(const TypeInfo* type);

		UnresolvedComponent* MakePreservedComponent(StringHash declaredType, std::span<const u8> payload,
													bool payloadIsBinary);

		UnresolvedComponent* MakePreservedComponent(const SceneDocument& document, const SceneDocumentComponentRow& row);

		SceneDocumentComponentRow BuildComponentRow(SceneDocument& document, Component& component,
													const TypeInfo* type, u64 rowId);

		void ApplyPreservedBlob(std::span<const u8> payload, bool payloadIsBinary, void* instance,
								const TypeInfo* type, std::vector<SerializerInternal::HandleFixupJob>& fixups);

		void ReadBlob(const SceneDocument& document, const SceneDocumentBlob& blob, void* instance,
					  const TypeInfo* type, std::vector<SerializerInternal::HandleFixupJob>& fixups);

		// document + scene must outlive the integration
		struct SceneIntegration;

		SceneIntegration* CreateSceneIntegration(const SceneDocument& document, Scene& scene);

		bool StepSceneIntegration(SceneIntegration& integration, std::chrono::steady_clock::time_point deadline);

		f32 GetSceneIntegrationProgress(const SceneIntegration& integration);

		void DestroySceneIntegration(SceneIntegration* integration);

		void RefreshInstanceOverrides(Scene& scene);

		SceneObject* ComposeInstanceRow(const SceneDocument& document, const SceneDocumentRow& row,
										Scene& scene, SceneObject* parent);

		void EmitInstanceRow(SceneDocument& document, Scene& scene, SceneObject& instanceRoot, u64 parentId);

		JSONValue WriteOverrideLayerJSON(const OverrideLayer& layer, SceneDocument& document);
		bool ParseOverrideLayerJSON(const JSONValue& json, OverrideLayer& out, SceneDocument& document);
		bool WriteOverrideLayerBinary(SerializerInternal::BinaryWriter& writer, const OverrideLayer& layer, const SceneDocument& document);
		bool ParseOverrideLayerBinary(SerializerInternal::BinaryReader& reader, OverrideLayer& out, SceneDocument& document);

	} // namespace SceneInternal

} // namespace Shalltear
