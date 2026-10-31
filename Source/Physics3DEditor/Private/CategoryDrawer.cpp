#include "Physics3DEditor.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "Shalltear/Physics3D/Colliders.h"

#include "PhysicsCategories.h"
#include "PhysicsSettings.h"
#include "PhysicsWorld.h"

#include <memory>

namespace Shalltear::Physics3DEditor {

	namespace {

		constexpr const char* UNDECLARED_SUFFIX = " (undeclared)";

		constexpr const char* COLLIDER_TYPE = "Collider";

		struct CategoryItems {
			Vector<str> names{GetMemoryResource(MemTag::Editor)};
			Vector<str> labels{GetMemoryResource(MemTag::Editor)};

			// views into labels (must not change after Build)
			Vector<std::string_view> labelViews{GetMemoryResource(MemTag::Editor)};

			std::span<const std::string_view> LabelSpan() const { return {labelViews.data(), labelViews.size()}; }

			i32 IndexOf(std::string_view name) const {
				for (size_t i = 0; i < names.size(); ++i) {
					if (names[i] == name) {
						return static_cast<i32>(i);
					}
				}
				return -1;
			}

			u64 MaskOf(std::string_view value) const {
				u64 mask = 0;
				ForEachCategoryName(value, [&](std::string_view name) {
					const i32 index = IndexOf(name);
					if (index >= 0) {
						mask |= 1ull << static_cast<u32>(index);
					}
				});
				return mask;
			}
		};

		using SharedItems = std::shared_ptr<CategoryItems>;

		SharedItems BuildItems(std::string_view declaration, std::string_view value) {
			auto items = std::make_shared<CategoryItems>();
			ForEachCategoryName(declaration, [&](std::string_view name) {
				if (items->IndexOf(name) >= 0) {
					return;
				}
				items->names.emplace_back(name);
				items->labels.emplace_back(name);
			});
			ForEachCategoryName(value, [&](std::string_view name) {
				if (items->IndexOf(name) >= 0) {
					return;
				}
				items->names.emplace_back(name);
				str label(name);
				label += UNDECLARED_SUFFIX;
				items->labels.emplace_back(std::move(label));
			});
			// after both loops (growing labels would move the views)
			items->labelViews.reserve(items->labels.size());
			for (const str& label : items->labels) {
				items->labelViews.push_back(label);
			}
			return items;
		}

		std::string_view DeclaredVocabulary() {
			const PhysicsSettings* settings = GetPhysicsWorld().GetSettings();
			return settings ? std::string_view(settings->categories) : std::string_view();
		}

		const str* ReadField(EditTarget target, FieldRef& ref) {
			Object* object = target.Resolve();
			const FieldInfo* field = ref.Resolve();
			if (!object || !field || !field->addrFn) {
				return nullptr;
			}
			if (!TypeRegistry::IsDerivedFrom(object->GetType(), field->ownerType)) {
				return nullptr;
			}
			return static_cast<const str*>(field->addrFn(object));
		}

		str ComposeValue(const CategoryItems& items, u64 mask, std::string_view live) {
			Vector<str> chosen{GetMemoryResource(MemTag::Editor)};
			for (size_t i = 0; i < items.names.size(); ++i) {
				if ((mask & (1ull << i)) != 0) {
					chosen.push_back(items.names[i]);
				}
			}
			ForEachCategoryName(live, [&](std::string_view name) {
				if (items.IndexOf(name) < 0) {
					chosen.emplace_back(name);
				}
			});
			return JoinCategoryNames(chosen);
		}

		void DrawCategorySet(UI::Context& context, UI::ElementHandle row,
							 FieldRef field, EditTarget target, const void* owner) {
			const str* value = ReadField(target, field);
			if (!value) {
				return;
			}

			SharedItems items = BuildItems(DeclaredVocabulary(), *value);

			UI::Widgets::MaskDropdownDesc desc;
			desc.items = items->LabelSpan();
			desc.mask = items->MaskOf(*value);
			desc.font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
			desc.fontSizePx = EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody);
			desc.onChange = [target, field, items](u64 mask) mutable {
				const str* live = ReadField(target, field);
				if (!live) {
					return;
				}
				str next = ComposeValue(*items, mask, *live);
				if (next == *live) {
					return;
				}
				UndoSystem::Push(MakeUnique<PropertySetCommand>(
					target, field.field, Variant(*live), Variant(next)));
			};

			UI::Widgets::DropdownParts parts = UI::Widgets::MaskDropdown(context, row, desc);
			context.AddClass(parts.root, StringHash("slt-fill"));

			PropertyDrawers::SetRowRefresh(context, row, [parts, items, target, field](UI::Context& ctx) mutable {
											   const str* live = ReadField(target, field);
											   if (!live) {
												   return;
											   }
											   UI::Widgets::SetMaskDropdownValue(ctx, parts, items->LabelSpan(),
																				 items->MaskOf(*live)); }, owner);
		}

	} // anonymous namespace

	void RegisterCategoryDrawers(const void* owner) {
		const bool categories = PropertyDrawers::RegisterFieldDrawerFor(
			StringHash(COLLIDER_TYPE), StringHash("_categories"),
			[owner](UI::Context& context, UI::ElementHandle row, FieldRef field, EditTarget target) {
				DrawCategorySet(context, row, field, target, owner);
			},
			owner);
		const bool collidesWith = PropertyDrawers::RegisterFieldDrawerFor(
			StringHash(COLLIDER_TYPE), StringHash("_collidesWith"),
			[owner](UI::Context& context, UI::ElementHandle row, FieldRef field, EditTarget target) {
				DrawCategorySet(context, row, field, target, owner);
			},
			owner);
		if (!categories || !collidesWith) {
			SLT_LOG_WARN("Physics", "couldn't register the collider category drawers");
		}
	}

} // namespace Shalltear::Physics3DEditor
