#include "Shalltear/Editor/SettingsOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection.h"

#include "Shalltear/Editor/PropertyRows.h"
#include "Shalltear/Editor/SettingsPresentation.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace Shalltear {

	namespace SettingsOps {

		namespace {

			bool ContainsFold(std::string_view haystack, std::string_view needle) {
				if (needle.empty()) {
					return true;
				}
				if (needle.size() > haystack.size()) {
					return false;
				}
				auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
				for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
					size_t offset = 0;
					while (offset < needle.size() && lower(haystack[start + offset]) == lower(needle[offset])) {
						++offset;
					}
					if (offset == needle.size()) {
						return true;
					}
				}
				return false;
			}

			str LastToken(std::string_view path) {
				size_t slash = path.rfind('/');
				return str(slash == std::string_view::npos ? path : path.substr(slash + 1));
			}

			str CategoryLabel(std::string_view path) {
				const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(path);
				if (presentation && !presentation->label.empty()) {
					return presentation->label;
				}
				return LastToken(path);
			}

			i32 CategoryOrder(std::string_view path) {
				const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(path);
				return presentation ? presentation->order : 0;
			}

			i32 EffectiveOrder(std::string_view path, const Vector<SettingsCategoryInfo>& categories) {
				i32 best = CategoryOrder(path);
				bool found = SettingsPresentation::FindCategory(path) != nullptr;
				for (const SettingsCategoryInfo& info : categories) {
					if (info.path.size() <= path.size() || info.path.substr(0, path.size()) != path || info.path[path.size()] != '/') {
						continue;
					}
					i32 order = CategoryOrder(info.path);
					best = found ? std::min(best, order) : order;
					found = true;
				}
				return best;
			}

		} // namespace

		std::string_view ScopeName(SettingScope scope) {
			switch (scope) {
			case SettingScope::Project:
				return "Project";
			case SettingScope::User:
				return "This machine";
			case SettingScope::UserProject:
				return "Machine + project";
			}
			return "?";
		}

		std::string_view ScopeDescription(SettingScope scope) {
			switch (scope) {
			case SettingScope::Project:
				return "Saved in the project and shared through version control.";
			case SettingScope::User:
				return "Saved on this computer. Applies to all projects opened on it.";
			case SettingScope::UserProject:
				return "Saved on this computer, for this project only.";
			}
			return {};
		}

		std::string_view SourceBadge(const SettingFieldState& state) {
			return state.isOverride ? ScopeName(state.resolvedFrom) : std::string_view("Default");
		}

		std::string_view SourceBadgeDescription(const SettingFieldState& state) {
			if (!state.isOverride) {
				return "The built-in default. Not saved anywhere.";
			}
			return ScopeDescription(state.resolvedFrom);
		}

		std::string_view ScopePrecedenceHint() {
			return "\"Machine + project\" overrides \"This machine\" (which overrides \"Project\").";
		}

		std::string_view AudienceLine(std::string_view categoryPath) {
			const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(categoryPath);
			if (!presentation) {
				return {};
			}
			switch (presentation->audience) {
			case SettingsAudience::EditorOnly:
				return "Used by the editor only.";
			case SettingsAudience::EditorAndPlayer:
				return "Used by the editor and by built games.";
			case SettingsAudience::PlayerOnly:
				return "Used by built games only.";
			case SettingsAudience::Unspecified:
				break;
			}
			return {};
		}

		std::string_view RestartLine(std::string_view categoryPath) {
			const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(categoryPath);
			if (presentation && presentation->audience == SettingsAudience::PlayerOnly) {
				return "Takes effect the next time the game starts.";
			}
			return MutabilityLine(MutabilityClass::Restart);
		}

		bool AnyFieldTakesEffectOnRestart(const SettingsCategoryInfo& category) {
			if (!category.type) {
				return false;
			}
			for (u32 index = 0; index < category.type->fieldCount; ++index) {
				MutabilityClass mutability = MutabilityClass::Hot;
				if (Settings::GetFieldMutability(&category.type->fields[index], mutability) && mutability == MutabilityClass::Restart) {
					return true;
				}
			}
			return false;
		}

		bool IsFieldShown(const SettingsCategoryInfo& category, const FieldInfo& field) {
			if (!(field.flags & FieldFlagEditorVisible)) {
				return false;
			}
			const SettingsFieldPresentation* presentation =
				field.displayName ? SettingsPresentation::FindField(category.path, field.displayName) : nullptr;
			if (!presentation || presentation->shownWhen.empty()) {
				return true;
			}
			for (const SettingsFieldCondition& clause : presentation->shownWhen) {
				const FieldInfo* named = category.type ? FindField(category.type, StringHash(clause.field)) : nullptr;
				if (!named || !named->addrFn || !category.live || named->size > 8) {
					return true;
				}
				u64 bits = PropertyRows::ReadRawBits(*named, named->addrFn(category.live));
				u64 mask = named->size >= 8 ? ~u64{0} : (u64{1} << (named->size * 8)) - 1;
				if ((bits == (static_cast<u64>(clause.value) & mask)) == clause.equals) {
					return true;
				}
			}
			return false;
		}

		std::string_view MutabilityBadge(MutabilityClass mutability) {
			switch (mutability) {
			case MutabilityClass::Hot:
				return {};
			case MutabilityClass::FrameBoundary:
				return "Next frame";
			case MutabilityClass::DeviceIdle:
				return "Device pause";
			case MutabilityClass::Restart:
				return "On restart";
			}
			return {};
		}

		std::string_view MutabilityLine(MutabilityClass mutability) {
			switch (mutability) {
			case MutabilityClass::Hot:
				return {};
			case MutabilityClass::FrameBoundary:
				return "Applies on the next frame.";
			case MutabilityClass::DeviceIdle:
				return "Applies after a brief device pause.";
			case MutabilityClass::Restart:
				return "Takes effect the next time the editor starts.";
			}
			return {};
		}

		std::string_view FieldMutabilityBadge(const FieldInfo& field) {
			MutabilityClass mutability = MutabilityClass::Hot;
			if (!Settings::GetFieldMutability(&field, mutability)) {
				return {};
			}
			return MutabilityBadge(mutability);
		}

		std::string_view FieldMutabilityLine(const FieldInfo& field) {
			MutabilityClass mutability = MutabilityClass::Hot;
			if (!Settings::GetFieldMutability(&field, mutability)) {
				return {};
			}
			return MutabilityLine(mutability);
		}

		str FieldLabelFor(std::string_view categoryPath, const FieldInfo& field) {
			const SettingsFieldPresentation* presentation = SettingsPresentation::FindField(categoryPath, field.displayName ? field.displayName : "");
			if (presentation && !presentation->label.empty()) {
				return presentation->label;
			}
			return PropertyRows::FieldLabel(field);
		}

		str FieldTooltipFor(std::string_view categoryPath, const FieldInfo& field) {
			const SettingsFieldPresentation* presentation = SettingsPresentation::FindField(categoryPath, field.displayName ? field.displayName : "");
			if (presentation && !presentation->tooltip.empty()) {
				return presentation->tooltip;
			}
			if (const AttributeInfo* tooltip = FindFieldAttribute(&field, "Tooltip"_hash)) {
				if (tooltip->value.Is<str>()) {
					return tooltip->value.As<str>();
				}
			}
			return {};
		}

		bool CategoryMatches(std::string_view search, std::string_view categoryPath) {
			if (search.empty()) {
				return true;
			}
			if (ContainsFold(categoryPath, search) || ContainsFold(CategoryLabel(categoryPath), search)) {
				return true;
			}
			const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(categoryPath);
			if (presentation && ContainsFold(presentation->keywords, search)) {
				return true;
			}

			Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
			Settings::GetCategories(categories);
			for (const SettingsCategoryInfo& info : categories) {
				if (info.path != categoryPath || !info.type) {
					continue;
				}
				for (u32 index = 0; index < info.type->fieldCount; ++index) {
					const FieldInfo& field = info.type->fields[index];
					if (!(field.flags & FieldFlagEditorVisible)) {
						continue;
					}
					if (ContainsFold(FieldLabelFor(categoryPath, field), search)) {
						return true;
					}
					const SettingsFieldPresentation* fieldPresentation =
						SettingsPresentation::FindField(categoryPath, field.displayName ? field.displayName : "");
					if (fieldPresentation && ContainsFold(fieldPresentation->keywords, search)) {
						return true;
					}
				}
			}
			return false;
		}

		void BuildTree(std::string_view search, Vector<TreeNode>& out) {
			out.clear();

			Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
			Settings::GetCategories(categories);

			struct Row {
				str path;
				bool declared = false;
			};
			Vector<Row> rows{GetMemoryResource(MemTag::Editor)};
			auto insert = [&rows](std::string_view path, bool isDeclared) {
				for (Row& row : rows) {
					if (row.path == path) {
						row.declared = row.declared || isDeclared;
						return;
					}
				}
				rows.push_back({str(path), isDeclared});
			};

			for (const SettingsCategoryInfo& info : categories) {
				if (!CategoryMatches(search, info.path)) {
					continue;
				}
				for (size_t offset = 0; offset < info.path.size(); ++offset) {
					if (info.path[offset] == '/') {
						insert(info.path.substr(0, offset), false);
					}
				}
				insert(info.path, true);
			}

			for (const Row& row : rows) {
				TreeNode node;
				node.path = row.path;
				node.label = CategoryLabel(node.path);
				node.declared = row.declared;
				node.depth = static_cast<u32>(std::count(node.path.begin(), node.path.end(), '/'));
				if (const SettingsCategoryPresentation* presentation = SettingsPresentation::FindCategory(node.path)) {
					node.tooltip = presentation->tooltip;
				}
				out.push_back(std::move(node));
			}

			std::sort(out.begin(), out.end(), [&categories](const TreeNode& a, const TreeNode& b) {
				size_t cursor = 0;
				while (true) {
					const size_t endA = a.path.find('/', cursor);
					const size_t endB = b.path.find('/', cursor);
					const std::string_view segmentA = std::string_view(a.path).substr(cursor, endA - cursor);
					const std::string_view segmentB = std::string_view(b.path).substr(cursor, endB - cursor);
					if (segmentA != segmentB) {
						const str prefixA = a.path.substr(0, endA);
						const str prefixB = b.path.substr(0, endB);
						const i32 orderA = EffectiveOrder(prefixA, categories);
						const i32 orderB = EffectiveOrder(prefixB, categories);
						return orderA != orderB ? orderA < orderB : segmentA < segmentB;
					}
					if (endA == str::npos || endB == str::npos) {
						return endA != str::npos ? false : endB != str::npos;
					}
					cursor = endA + 1;
				}
			});
		}

		namespace {

			struct LegacyGraphicsKey {
				const char* legacy;
				const char* field;
			};

			constexpr LegacyGraphicsKey LegacyGraphicsKeys[] = {
				{"VSync", "vsync"},
				{"Adapter", "adapter"},
				{"Validation", "validation"},
			};

			constexpr const char* GRAPHICS_SECTION = "Graphics";

		} // namespace

		u32 MigrateLegacyEngineIni(const Path& legacyFile, const Path& projectDocument) {
			if (legacyFile.ToString().empty() || projectDocument.ToString().empty()) {
				return 0;
			}
			if (!FileSystem::Exists(legacyFile)) {
				return 0;
			}

			Config legacy;
			if (!legacy.Load(legacyFile)) {
				SLT_LOG_WARN("Settings", "'{}' exists but couldn't be read ([Graphics] values left alone)",
							 legacyFile.ToString());
				return 0;
			}

			Config document;
			document.Load(projectDocument);

			u32 moved = 0;
			for (const LegacyGraphicsKey& mapping : LegacyGraphicsKeys) {
				if (!legacy.Has(GRAPHICS_SECTION, mapping.legacy)) {
					continue;
				}
				if (document.Has(GRAPHICS_SECTION, mapping.field)) {
					continue;
				}
				if (mapping.legacy == std::string_view("VSync")) {
					document.SetString(GRAPHICS_SECTION, mapping.field,
									   legacy.GetBool(GRAPHICS_SECTION, mapping.legacy) ? "On" : "Off");
				} else if (mapping.legacy == std::string_view("Adapter")) {
					document.SetI64(GRAPHICS_SECTION, mapping.field, legacy.GetI64(GRAPHICS_SECTION, mapping.legacy, -1));
				} else {
					document.SetBool(GRAPHICS_SECTION, mapping.field, legacy.GetBool(GRAPHICS_SECTION, mapping.legacy));
				}
				++moved;
			}

			if (moved > 0 && !document.Save(projectDocument)) {
				SLT_LOG_ERROR("Settings", "couldn't write '{}'. '{}' was left alone (retried next launch)",
							  projectDocument.ToString(), legacyFile.ToString());
				return 0;
			}

			legacy.RemoveSection(GRAPHICS_SECTION);
			str remainder = legacy.Serialize();
			const bool empty = remainder.find_first_not_of(" \t\r\n") == str::npos;
			if (empty) {
				std::error_code ec;
				std::filesystem::remove(std::filesystem::path{legacyFile.CStr()}, ec);
			} else {
				legacy.Save(legacyFile);
			}

			if (moved > 0) {
				SLT_LOG_INFO("Settings", "moved {} [Graphics] key(s) out of '{}' into the settings store's Project scope",
							 moved, legacyFile.ToString());
			} else {
				SLT_LOG_INFO("Settings", "'{}' had no [Graphics] key the settings store didn't already have. Retired it",
							 legacyFile.ToString());
			}
			return moved;
		}

		u32 ListProjectQualityProfiles() {
			str* packedField = nullptr;
			Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
			Settings::GetCategories(categories);
			for (const SettingsCategoryInfo& info : categories) {
				if (info.path != "Graphics" || !info.type || !info.live) {
					continue;
				}
				for (u32 index = 0; index < info.type->fieldCount; ++index) {
					const FieldInfo& field = info.type->fields[index];
					if (field.displayName && std::string_view(field.displayName) == "qualityProfiles" && field.addrFn) {
						packedField = static_cast<str*>(field.addrFn(info.live));
					}
				}
			}
			if (!packedField) {
				return 0;
			}
			if (!packedField->empty()) {
				return 0;
			}

			const Path assets = FileSystem::Resolve(Path("project://Assets"));
			if (assets.ToString().empty() || assets.ToString().find("://") != str::npos) {
				return 0;
			}

			Vector<str> found{GetMemoryResource(MemTag::Editor)};
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(std::filesystem::path{assets.CStr()}, ec), end;
				 it != end && !ec; it.increment(ec)) {
				if (!it->is_regular_file(ec) || it->path().extension() != ".slprofile") {
					continue;
				}
				found.push_back(str(it->path().generic_string()));
			}
			if (found.empty()) {
				return 0;
			}
			std::sort(found.begin(), found.end());

			str packed;
			u32 listed = 0;
			for (const str& physical : found) {
				const Guid id = AssetDatabase::EnsureAssetIdentity(Path(physical));
				if (!id.IsValid()) {
					continue;
				}
				if (!packed.empty()) {
					packed += ';';
				}
				packed += id.ToString();
				++listed;
			}
			if (listed == 0) {
				return 0;
			}

			*packedField = std::move(packed);
			Settings::NotifyChanged("Graphics", "qualityProfiles");
			Settings::Save();
			SLT_LOG_INFO("Settings", "listed {} graphics profile(s) as this project's quality levels", listed);
			return listed;
		}

		u32 CollectSettingsAssetRoots(Vector<Guid>& out) {
			Vector<Guid> found{GetMemoryResource(MemTag::Editor)};

			struct Walk {
				Vector<Guid>* found;
			};
			Walk walk{&found};
			TypeRegistry::ForEachType(
				[](const TypeInfo* type, void* user) {
					Vector<Guid>& found = *static_cast<Walk*>(user)->found;
					const AttributeInfo* category = FindTypeAttribute(type, StringHash("SettingsCategory"));
					if (!category || !category->value.Is<str>()) {
						return;
					}
					const str& path = category->value.As<str>();

					for (u32 index = 0; index < type->fieldCount; ++index) {
						const FieldInfo& field = type->fields[index];
						if (!FindFieldAttribute(&field, StringHash("AssetRefs")) || !field.displayName) {
							continue;
						}
						if (field.fieldType != GetTypeInfoFor<str>::Get()) {
							SLT_LOG_WARN("Settings",
										 "[{}] {} has SLT_ASSET_REFS but isn't a string "
										 "(its assets won't be cooked)",
										 path, field.displayName);
							continue;
						}

						str packed;
						if (!Settings::TryGetStoredValue(path, field.displayName, packed)) {
							continue;
						}

						std::string_view rest = packed;
						while (!rest.empty()) {
							const size_t separator = rest.find(';');
							std::string_view entry = rest.substr(0, separator);
							rest = separator == std::string_view::npos ? std::string_view{} : rest.substr(separator + 1);

							if (entry.empty()) {
								continue;
							}
							const Guid guid = Guid::FromString(entry);
							if (!guid.IsValid()) {
								SLT_LOG_WARN("Settings", "[{}] {} has '{}' (not an asset guid). Not cooked",
											 path, field.displayName, entry);
								continue;
							}
							found.push_back(guid);
						}
					}
				},
				&walk);

			std::sort(found.begin(), found.end());
			found.erase(std::unique(found.begin(), found.end()), found.end());

			u32 appended = 0;
			for (const Guid& guid : found) {
				if (std::find(out.begin(), out.end(), guid) != out.end()) {
					continue;
				}
				out.push_back(guid);
				++appended;
			}
			return appended;
		}

	} // namespace SettingsOps

} // namespace Shalltear
