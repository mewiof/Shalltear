#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class SceneObject;
	class Component;
	class PrefabAsset;
	struct SceneDocument;
	struct TypeInfo;

	namespace SceneInternal {

		constexpr u64 FRAGMENT_ROW_HANDLE_TAG = 1ull << 63;

		class SHALLTEAR_CORE_API FragmentBuilder {
		public:
			FragmentBuilder(std::string_view name, PrefabKind kind);
			~FragmentBuilder();

			FragmentBuilder(const FragmentBuilder&) = delete;
			FragmentBuilder& operator=(const FragmentBuilder&) = delete;

			SceneObject& BeginObject();

			Component* AddComponent(const TypeInfo* type);

			void EndObject(u64 rowId, u64 parentRowId, std::span<const u64> componentRowIds);

			template <typename T>
			void SetHandleToRow(Handle<T>& field, u64 rowId) {
				SetHandleToRowRaw(&field, rowId, T::GetReflectType());
			}
			void SetHandleToRowRaw(void* handleField, u64 rowId, const TypeInfo* pointeeType);

			PrefabAsset* Release();
		private:
			static bool RowScopeToGuid(void* user, u64 instanceId, Guid* outGuid, StringHash* outPointeeType);

			SceneDocument* _document = nullptr;
			SceneObject* _current = nullptr;
			u64 _maxRowId = 0;
			PrefabKind _kind = PrefabKind::Regular;
			Vector<std::pair<u64, u64>> _rowHandleTypes{GetMemoryResource(MemTag::Scene)};
		};

	} // namespace SceneInternal

} // namespace Shalltear
