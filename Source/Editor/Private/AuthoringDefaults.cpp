#include "Shalltear/Editor/AuthoringDefaults.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <algorithm>
#include <format>

namespace Shalltear::AuthoringDefaults {

	namespace {

		struct Entry {
			StringHash type;
			StringHash field;
			str value;
			const void* owner = nullptr;
		};

		struct State {
			Vector<Entry> entries{GetMemoryResource(MemTag::Editor)};
			bool seeded = false;
		};
		State* _state = nullptr;

		State& Table() {
			if (!_state) {
				_state = New<State>(MemTag::Editor);
			}
			return *_state;
		}

		constexpr std::string_view DEFAULT_SKYBOX =
			R"({"_handle": true, "pointeeType": "MaterialAsset", "assetId": "6a68ff70f33bd8c6-4820ba95ba856f68"})";

		void Plant(StringHash type, StringHash field, std::string_view value, bool onlyAbsent) {
			if (onlyAbsent && std::any_of(Table().entries.begin(), Table().entries.end(), [&](const Entry& entry) {
					return entry.type == type && entry.field == field;
				})) {
				return;
			}
			Register(type, field, value);
		}

		void SeedBuiltIns(bool onlyAbsent = false) {
			State& table = Table();
			if (table.seeded && !onlyAbsent) {
				return;
			}
			table.seeded = true;
			const StringHash environment = Environment::GetReflectType()->name;
			Plant(environment, StringHash("_skyMode"), R"("Material")", onlyAbsent);
			Plant(environment, StringHash("_skyMaterial"), DEFAULT_SKYBOX, onlyAbsent);
		}

		bool LoadingToHandle(void* user, const Guid& guid, StringHash pointeeType, u64* outId) {
			(void)user;
			if (!IsAssetGuid(guid)) {
				return false;
			}
			Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
			const TypeInfo* pointee = TypeRegistry::FindType(pointeeType);
			if (!asset || (pointee && !TypeRegistry::IsDerivedFrom(asset->GetType(), pointee))) {
				SLT_LOG_WARN("Editor", "an authoring default uses an asset that didn't load as a {}: {}",
							 pointee && pointee->displayName ? pointee->displayName : "handle's pointee", guid.ToString());
				return false;
			}
			*outId = asset->GetInstanceId();
			return true;
		}

	} // anonymous namespace

	void RegisterBuiltIns() {
		SeedBuiltIns();
	}

	bool Register(StringHash componentType, StringHash field, std::string_view valueJSON, const void* owner) {
		SeedBuiltIns();
		const TypeInfo* type = TypeRegistry::FindType(componentType);
		const FieldInfo* info = type ? FindField(type, field) : nullptr;
		if (!info || (info->flags & FieldFlagTransient) != 0) {
			SLT_LOG_WARN("Editor", "ignoring an authoring default: {:#x} is no serialized field of {}",
						 field.GetValue(), type && type->displayName ? type->displayName : "a reflected type");
			return false;
		}
		const str probe = str(R"({"value": )") + str(valueJSON) + "}";
		LinearAllocator arena(probe.size() + 64, MemTag::Editor);
		if (valueJSON.empty() || !JSONParse(probe, arena).IsObject()) {
			SLT_LOG_WARN("Editor", "ignoring an authoring default: the value for {}'s field {:#x} isn't JSON",
						 type->displayName ? type->displayName : "a reflected type", field.GetValue());
			return false;
		}
		Vector<Entry>& entries = Table().entries;
		for (Entry& entry : entries) {
			if (entry.type == componentType && entry.field == field) {
				entry.value = str(valueJSON);
				entry.owner = owner;
				return true;
			}
		}
		entries.push_back({componentType, field, str(valueJSON), owner});
		return true;
	}

	u32 UnregisterOwnedBy(const void* owner) {
		if (!owner || !_state) {
			return 0;
		}
		const u32 removed = static_cast<u32>(std::erase_if(_state->entries, [owner](const Entry& entry) { return entry.owner == owner; }));
		if (removed > 0) {
			SeedBuiltIns(true);
		}
		return removed;
	}

	str StateFor(const TypeInfo* type) {
		if (!type) {
			return {};
		}
		SeedBuiltIns();
		const Vector<Entry>& entries = Table().entries;
		Vector<StringHash> written{GetMemoryResource(MemTag::Editor)};
		str state;
		for (const TypeInfo* walk = type; walk != nullptr; walk = walk->parent) {
			for (const Entry& entry : entries) {
				if (entry.type != walk->name || std::find(written.begin(), written.end(), entry.field) != written.end()) {
					continue;
				}
				written.push_back(entry.field);
				state += state.empty() ? "{" : ", ";
				state += std::format("\"{:016x}\": ", entry.field.GetValue());
				state += entry.value;
			}
		}
		if (!state.empty()) {
			state += "}";
		}
		return state;
	}

	bool Write(Object& object, std::string_view state) {
		const TypeInfo* type = object.GetType();
		if (!type) {
			return false;
		}
		if (state.empty()) {
			return true;
		}
		ScopedHandleResolver scope({nullptr, &LoadingToHandle, nullptr});
		return TextSerializer::Deserialize(&object, type, state);
	}

	void Shutdown() {
		if (_state) {
			Delete(_state);
			_state = nullptr;
		}
	}

} // namespace Shalltear::AuthoringDefaults
