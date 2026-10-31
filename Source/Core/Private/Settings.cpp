#include "Shalltear/Core/Settings.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/Walk.h"

#include "Graphics/GraphicsSettings.h"
#include "ScreenSettings.h"
#include "Module/EngineStateTable.h"
#include "StaticLinkAnchors.h"
#include "Module/ModuleAddress.h"
#include "Serialization/SerializerInternal.h"

#include <cstring>

namespace Shalltear {

	namespace {

		using SerializerInternal::FieldCategory;
		using SerializerInternal::PrimKind;
		using SerializerInternal::ValueDesc;

		struct Category {
			str path;
			const TypeInfo* type = nullptr;
			void* live = nullptr;
			void* defaults = nullptr;
			SettingScope scope = SettingScope::Project;
			bool overridable = false;

			const void* owner = nullptr;

			Vector<const FieldInfo*> fields{GetMemoryResource(MemTag::Core)};

			Vector<i8> resolvedFrom{GetMemoryResource(MemTag::Core)};

			Vector<i8> editScope{GetMemoryResource(MemTag::Core)};
		};

		struct StoreState {
			SettingsPaths paths;
			Config documents[SETTING_SCOPE_COUNT];
			Vector<Category*> categories{GetMemoryResource(MemTag::Core)};

			Config handedOver;
		};

		StoreState* _boundState = nullptr;

		StoreState& State() {
			if (_boundState) {
				return *_boundState;
			}
			static StoreState state;
			return state;
		}

		u32 ScopeIndex(SettingScope scope) {
			return static_cast<u32>(scope);
		}

		bool IsEncodable(const ValueDesc& desc) {
			switch (desc.category) {
			case FieldCategory::Str:
				return true;
			case FieldCategory::Enum:
				return true;
			case FieldCategory::Prim:
				return SerializerInternal::ClassifyPrim(desc.type) != PrimKind::None;
			default:
				return false;
			}
		}

		void WriteValue(Config& document, const str& section, const char* key, const ValueDesc& desc, const void* fieldPtr) {
			switch (desc.category) {
			case FieldCategory::Str:
				document.SetString(section, key, *static_cast<const str*>(fieldPtr));
				return;
			case FieldCategory::Enum: {
				u64 raw = SerializerInternal::ReadEnumBits(fieldPtr, desc.size);
				const EnumConstantInfo* constant = FindEnumConstantByValue(desc.type, raw);
				if (constant && constant->displayName) {
					document.SetString(section, key, constant->displayName);
				} else {
					document.SetI64(section, key, static_cast<i64>(raw));
				}
				return;
			}
			case FieldCategory::Prim:
				switch (SerializerInternal::ClassifyPrim(desc.type)) {
				case PrimKind::Bool:
					document.SetBool(section, key, *static_cast<const bool*>(fieldPtr));
					return;
				case PrimKind::I8:
					document.SetI64(section, key, *static_cast<const i8*>(fieldPtr));
					return;
				case PrimKind::I16:
					document.SetI64(section, key, *static_cast<const i16*>(fieldPtr));
					return;
				case PrimKind::I32:
					document.SetI64(section, key, *static_cast<const i32*>(fieldPtr));
					return;
				case PrimKind::I64:
					document.SetI64(section, key, *static_cast<const i64*>(fieldPtr));
					return;
				case PrimKind::U8:
					document.SetI64(section, key, static_cast<i64>(*static_cast<const u8*>(fieldPtr)));
					return;
				case PrimKind::U16:
					document.SetI64(section, key, static_cast<i64>(*static_cast<const u16*>(fieldPtr)));
					return;
				case PrimKind::U32:
					document.SetI64(section, key, static_cast<i64>(*static_cast<const u32*>(fieldPtr)));
					return;
				case PrimKind::U64:
					document.SetI64(section, key, static_cast<i64>(*static_cast<const u64*>(fieldPtr)));
					return;
				case PrimKind::F32:
					document.SetF64(section, key, static_cast<f64>(*static_cast<const f32*>(fieldPtr)));
					return;
				case PrimKind::F64:
					document.SetF64(section, key, *static_cast<const f64*>(fieldPtr));
					return;
				default:
					return;
				}
			default:
				return;
			}
		}

		void ReadValue(const Config& document, const str& section, const char* key, const ValueDesc& desc, void* fieldPtr) {
			switch (desc.category) {
			case FieldCategory::Str:
				*static_cast<str*>(fieldPtr) = document.GetString(section, key);
				return;
			case FieldCategory::Enum: {
				str text = document.GetString(section, key);
				if (const EnumConstantInfo* constant = FindEnumConstantByName(desc.type, text)) {
					SerializerInternal::WriteEnumBits(fieldPtr, desc.size, EnumValueToBits(desc.type, constant->value));
					return;
				}
				i64 numeric = document.GetI64(section, key);
				SerializerInternal::WriteEnumBits(fieldPtr, desc.size, EnumValueToBits(desc.type, numeric));
				return;
			}
			case FieldCategory::Prim:
				switch (SerializerInternal::ClassifyPrim(desc.type)) {
				case PrimKind::Bool:
					*static_cast<bool*>(fieldPtr) = document.GetBool(section, key);
					return;
				case PrimKind::I8:
					*static_cast<i8*>(fieldPtr) = static_cast<i8>(document.GetI64(section, key));
					return;
				case PrimKind::I16:
					*static_cast<i16*>(fieldPtr) = static_cast<i16>(document.GetI64(section, key));
					return;
				case PrimKind::I32:
					*static_cast<i32*>(fieldPtr) = static_cast<i32>(document.GetI64(section, key));
					return;
				case PrimKind::I64:
					*static_cast<i64*>(fieldPtr) = document.GetI64(section, key);
					return;
				case PrimKind::U8:
					*static_cast<u8*>(fieldPtr) = static_cast<u8>(document.GetI64(section, key));
					return;
				case PrimKind::U16:
					*static_cast<u16*>(fieldPtr) = static_cast<u16>(document.GetI64(section, key));
					return;
				case PrimKind::U32:
					*static_cast<u32*>(fieldPtr) = static_cast<u32>(document.GetI64(section, key));
					return;
				case PrimKind::U64:
					*static_cast<u64*>(fieldPtr) = static_cast<u64>(document.GetI64(section, key));
					return;
				case PrimKind::F32:
					*static_cast<f32*>(fieldPtr) = static_cast<f32>(document.GetF64(section, key));
					return;
				case PrimKind::F64:
					*static_cast<f64*>(fieldPtr) = document.GetF64(section, key);
					return;
				default:
					return;
				}
			default:
				return;
			}
		}

		bool ValuesEqual(const ValueDesc& desc, const void* a, const void* b) {
			if (desc.category == FieldCategory::Str) {
				return *static_cast<const str*>(a) == *static_cast<const str*>(b);
			}
			return std::memcmp(a, b, desc.size) == 0;
		}

		void CopyValue(const ValueDesc& desc, const void* from, void* to) {
			if (desc.category == FieldCategory::Str) {
				*static_cast<str*>(to) = *static_cast<const str*>(from);
				return;
			}
			std::memcpy(to, from, desc.size);
		}

		bool HandedOverHolds(const StoreState& state, const Category& category, const FieldInfo* field,
							 const ValueDesc& desc, const void* liveValue) {
			if (!state.handedOver.Has(category.path, field->displayName)) {
				return false;
			}
			if (desc.category == FieldCategory::Str) {
				str stored;
				ReadValue(state.handedOver, category.path, field->displayName, desc, &stored);
				return stored == *static_cast<const str*>(liveValue);
			}
			alignas(8) u8 stored[8] = {};
			if (desc.size > sizeof(stored)) {
				return false;
			}
			ReadValue(state.handedOver, category.path, field->displayName, desc, stored);
			return std::memcmp(stored, liveValue, desc.size) == 0;
		}

		u32 ReadableScopes(const Category& category, u32 (&out)[SETTING_SCOPE_COUNT]) {
			u32 count = 0;
			if (!category.overridable) {
				out[count++] = ScopeIndex(category.scope);
				return count;
			}
			for (u32 index = 0; index <= ScopeIndex(category.scope); ++index) {
				out[count++] = index;
			}
			return count;
		}

		u32 WriteTargetOf(const Category& category, size_t fieldIndex) {
			if (category.editScope[fieldIndex] >= 0) {
				return static_cast<u32>(category.editScope[fieldIndex]);
			}
			if (category.resolvedFrom[fieldIndex] >= 0) {
				return static_cast<u32>(category.resolvedFrom[fieldIndex]);
			}
			return ScopeIndex(category.scope);
		}

		void ResolveCategory(Category& category) {
			StoreState& state = State();
			u32 scopes[SETTING_SCOPE_COUNT];
			const u32 scopeCount = ReadableScopes(category, scopes);

			for (size_t fieldIndex = 0; fieldIndex < category.fields.size(); ++fieldIndex) {
				const FieldInfo* field = category.fields[fieldIndex];
				const ValueDesc desc = SerializerInternal::DescribeField(field);
				category.resolvedFrom[fieldIndex] = -1;

				CopyValue(desc, field->addrFn(category.defaults), field->addrFn(category.live));

				for (u32 slot = 0; slot < scopeCount; ++slot) {
					const Config& document = state.documents[scopes[slot]];
					if (!document.Has(category.path, field->displayName)) {
						continue;
					}
					ReadValue(document, category.path, field->displayName, desc, field->addrFn(category.live));
					category.resolvedFrom[fieldIndex] = static_cast<i8>(scopes[slot]);
					break;
				}
			}
		}

		Category* FindCategory(std::string_view path) {
			for (Category* category : State().categories) {
				if (category->path == path) {
					return category;
				}
			}
			return nullptr;
		}

		void DestroyInstance(const TypeInfo* type, void*& instance) {
			if (!instance) {
				return;
			}
			if (type->destructor) {
				type->destructor(instance);
			}
			Free(instance);
			instance = nullptr;
		}

	} // namespace

	SettingsPaths Settings::GetDefaultPaths() {
		SettingsPaths paths;

		auto resolveProject = [](const char* virtualPath) -> Path {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			if (resolved.ToString().find("://") != str::npos) {
				return {};
			}
			return resolved;
		};

		paths.project = resolveProject("project://ProjectSettings.ini");
		paths.userProject = resolveProject("project://Library/Editor/UserProjectSettings.ini");
		if (paths.project.ToString().empty()) {
			SLT_LOG_INFO("Settings", "project:// isn't mounted (Project and UserProject settings unread, all at defaults)");
		}

		Path userData = FileSystem::GetUserDataDirectory();
		if (!userData.ToString().empty()) {
			paths.user = userData / "Shalltear" / "UserSettings.ini";
		}
		return paths;
	}

	void Settings::Initialize(const SettingsPaths& paths) {
		SLT_SETTINGS_ANCHORS(SLT_ANCHOR_TYPE)

		StoreState& state = State();
		state.paths = paths;

		const Path* byScope[SETTING_SCOPE_COUNT] = {&state.paths.userProject, &state.paths.user, &state.paths.project};
		for (u32 index = 0; index < SETTING_SCOPE_COUNT; ++index) {
			state.documents[index].Clear();
			const Path& path = *byScope[index];
			if (path.ToString().empty() || !FileSystem::Exists(path)) {
				continue;
			}
			state.documents[index].Load(path);
		}

		state.handedOver.Clear();
		if (!paths.projectText.empty()) {
			if (paths.project.ToString().empty()) {
				state.documents[static_cast<u32>(SettingScope::Project)].Parse(paths.projectText);
				state.handedOver.Parse(paths.projectText);
			} else {
				SLT_LOG_ERROR("Settings", "the Project document was given both as '{}' and as text (text ignored)", paths.project.ToString());
			}
		}

		for (Category* category : state.categories) {
			ResolveCategory(*category);
		}
	}

	void Settings::Terminate() {
		StoreState& state = State();
		for (Category* category : state.categories) {
			DestroyInstance(category->type, category->live);
			DestroyInstance(category->type, category->defaults);
			Delete(category);
		}
		state.categories.clear();
		state.categories.shrink_to_fit();
		for (u32 index = 0; index < SETTING_SCOPE_COUNT; ++index) {
			state.documents[index].Clear();
		}
		state.handedOver.Clear();
		state.paths = {};
	}

	void* Settings::DeclareCategory(std::string_view path, const TypeInfo* type, SettingScope scope, bool overridable) {
		if (path.empty() || !type) {
			SLT_LOG_ERROR("Settings", "category declaration rejected: empty path or null type");
			return nullptr;
		}

		if (Category* existing = FindCategory(path)) {
			return existing->live;
		}

		if (!type->constructor || !type->destructor) {
			SLT_LOG_ERROR("Settings", "category '{}' has no placement trampolines. Add SLT_REFLECT_CONSTRUCTIBLE() to its reflect block", path);
			return nullptr;
		}

		Vector<const FieldInfo*> fields{GetMemoryResource(MemTag::Core)};
		{
			std::vector<const FieldInfo*> collected;
			SerializerInternal::CollectSerializedFields(type, collected);
			for (const FieldInfo* field : collected) {
				if (!field->displayName) {
					SLT_LOG_ERROR("Settings", "category '{}' rejected: a field has no source name", path);
					return nullptr;
				}
				const ValueDesc desc = SerializerInternal::DescribeField(field);
				if (!IsEncodable(desc)) {
					SLT_LOG_ERROR("Settings", "category '{}' rejected: field '{}' isn't a scalar, a str or a reflected enum", path, field->displayName);
					return nullptr;
				}
				fields.push_back(field);
			}
		}

		void* live = Allocate(type->size, MemTag::Core, type->alignment);
		void* defaults = Allocate(type->size, MemTag::Core, type->alignment);
		type->constructor(live);
		type->constructor(defaults);

		Category* category = New<Category>(MemTag::Core);
		category->path = str(path);
		category->type = type;
		category->live = live;
		category->defaults = defaults;
		category->scope = scope;
		category->overridable = overridable;
		category->owner = ModuleInternal::ImageBaseOf(__builtin_return_address(0));
		category->fields = std::move(fields);
		category->resolvedFrom.assign(category->fields.size(), -1);
		category->editScope.assign(category->fields.size(), -1);

		State().categories.push_back(category);
		ResolveCategory(*category);
		return category->live;
	}

	u32 Settings::UnregisterOwnedBy(const void* imageBase) {
		if (!imageBase) {
			return 0;
		}

		StoreState& state = State();
		u32 removed = 0;
		for (size_t i = state.categories.size(); i > 0; --i) {
			Category* category = state.categories[i - 1];
			if (category->owner != imageBase) {
				continue;
			}
			// destroyed through the departing image's trampolines before it unmaps
			DestroyInstance(category->type, category->live);
			DestroyInstance(category->type, category->defaults);
			Delete(category);
			state.categories.erase(state.categories.begin() + static_cast<ptrdiff_t>(i - 1));
			++removed;
		}
		return removed;
	}

	void Settings::GetCategories(Vector<SettingsCategoryInfo>& out) {
		out.clear();
		for (const Category* category : State().categories) {
			SettingsCategoryInfo info;
			info.path = category->path;
			info.type = category->type;
			info.live = category->live;
			info.scope = category->scope;
			info.overridable = category->overridable;
			out.push_back(info);
		}
	}

	bool Settings::GetFieldMutability(const FieldInfo* field, MutabilityClass& out) {
		if (!field) {
			return false;
		}
		const AttributeInfo* attribute = FindFieldAttribute(field, "Mutability"_hash);
		if (!attribute || !attribute->value.Is<i32>()) {
			return false;
		}
		const i32 raw = attribute->value.As<i32>();
		if (raw < static_cast<i32>(MutabilityClass::Hot) || raw > static_cast<i32>(MutabilityClass::Restart)) {
			return false;
		}
		out = static_cast<MutabilityClass>(raw);
		return true;
	}

	bool Settings::GetFieldState(std::string_view categoryPath, std::string_view fieldName, SettingFieldState& out) {
		Category* category = FindCategory(categoryPath);
		if (!category) {
			return false;
		}
		for (size_t fieldIndex = 0; fieldIndex < category->fields.size(); ++fieldIndex) {
			if (fieldName != category->fields[fieldIndex]->displayName) {
				continue;
			}
			const i8 resolved = category->resolvedFrom[fieldIndex];
			out.isOverride = resolved >= 0;
			out.resolvedFrom = out.isOverride ? static_cast<SettingScope>(resolved) : category->scope;
			// must match Save
			out.editsLandIn = static_cast<SettingScope>(WriteTargetOf(*category, fieldIndex));
			return true;
		}
		return false;
	}

	bool Settings::SetFieldEditScope(std::string_view categoryPath, std::string_view fieldName, SettingScope scope) {
		Category* category = FindCategory(categoryPath);
		if (!category) {
			return false;
		}
		u32 scopes[SETTING_SCOPE_COUNT];
		const u32 scopeCount = ReadableScopes(*category, scopes);
		bool readable = false;
		for (u32 slot = 0; slot < scopeCount; ++slot) {
			readable |= scopes[slot] == ScopeIndex(scope);
		}
		if (!readable) {
			return false;
		}
		for (size_t fieldIndex = 0; fieldIndex < category->fields.size(); ++fieldIndex) {
			if (fieldName == category->fields[fieldIndex]->displayName) {
				category->editScope[fieldIndex] = static_cast<i8>(ScopeIndex(scope));
				return true;
			}
		}
		return false;
	}

	void Settings::NotifyChanged(std::string_view categoryPath, std::string_view fieldName) {
		SettingChangedEvent event;
		event.categoryPath = categoryPath;
		event.fieldName = fieldName;
		SettingsEvents::Changed.Publish(EventBus::GetGlobal(), event);
	}

	bool Settings::Save() {
		StoreState& state = State();
		const Path* byScope[SETTING_SCOPE_COUNT] = {&state.paths.userProject, &state.paths.user, &state.paths.project};

		bool storable = true;

		for (Category* category : state.categories) {
			u32 scopes[SETTING_SCOPE_COUNT];
			const u32 scopeCount = ReadableScopes(*category, scopes);

			for (size_t fieldIndex = 0; fieldIndex < category->fields.size(); ++fieldIndex) {
				const FieldInfo* field = category->fields[fieldIndex];
				const ValueDesc desc = SerializerInternal::DescribeField(field);
				const void* liveValue = field->addrFn(category->live);
				const void* defaultValue = field->addrFn(category->defaults);

				if (ValuesEqual(desc, liveValue, defaultValue)) {
					for (u32 slot = 0; slot < scopeCount; ++slot) {
						state.documents[scopes[slot]].Remove(category->path, field->displayName);
					}
					category->resolvedFrom[fieldIndex] = -1;
					continue;
				}

				const u32 target = WriteTargetOf(*category, fieldIndex);
				const bool alreadyHeld = target == ScopeIndex(SettingScope::Project) &&
										 HandedOverHolds(state, *category, field, desc, liveValue);
				if (byScope[target]->ToString().empty() && !alreadyHeld) {
					SLT_LOG_ERROR("Settings", "'{}' field '{}' belongs in a scope with no location here (value can't be stored)", category->path, field->displayName);
					storable = false;
				}
				WriteValue(state.documents[target], category->path, field->displayName, desc, liveValue);

				for (u32 slot = 0; slot < scopeCount; ++slot) {
					if (scopes[slot] < target) {
						state.documents[scopes[slot]].Remove(category->path, field->displayName);
					}
				}
				category->resolvedFrom[fieldIndex] = static_cast<i8>(target);
			}
		}

		bool allWritten = storable;
		for (u32 index = 0; index < SETTING_SCOPE_COUNT; ++index) {
			const Path& path = *byScope[index];
			if (path.ToString().empty()) {
				continue;
			}
			if (const str directory = path.GetDirectory(); !directory.empty()) {
				FileSystem::CreateDirectory(Path(directory));
			}
			if (!state.documents[index].Save(path)) {
				SLT_LOG_ERROR("Settings", "failed to write settings document '{}'", path.ToString());
				allWritten = false;
			}
		}
		return allWritten;
	}

	bool Settings::TryGetStoredValue(std::string_view categoryPath, std::string_view fieldName, str& out) {
		const Config& document = State().documents[static_cast<u32>(SettingScope::Project)];
		if (!document.Has(categoryPath, fieldName)) {
			return false;
		}
		out = document.GetString(categoryPath, fieldName);
		return true;
	}

	bool Settings::RevertField(std::string_view categoryPath, std::string_view fieldName) {
		Category* category = FindCategory(categoryPath);
		if (!category) {
			return false;
		}
		for (size_t fieldIndex = 0; fieldIndex < category->fields.size(); ++fieldIndex) {
			const FieldInfo* field = category->fields[fieldIndex];
			if (fieldName != field->displayName) {
				continue;
			}
			const ValueDesc desc = SerializerInternal::DescribeField(field);
			CopyValue(desc, field->addrFn(category->defaults), field->addrFn(category->live));
			category->resolvedFrom[fieldIndex] = -1;
			return true;
		}
		return false;
	}

} // namespace Shalltear

namespace Shalltear::ModuleInternal {

	void CaptureSettingsEngineState(EngineStateTable& table) {
		table.settings = &State();
	}

	void BindSettingsEngineState(const EngineStateTable& table) {
		_boundState = static_cast<StoreState*>(table.settings);
	}

} // namespace Shalltear::ModuleInternal
