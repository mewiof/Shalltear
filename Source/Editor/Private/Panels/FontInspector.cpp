#include "InspectorPanel.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/FontInspectorOps.h"
#include "Shalltear/Editor/ObjectField.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include <format>

namespace Shalltear {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct FontDraft {
			Guid asset;
			Vector<Guid> faces{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> fallbacks{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> savedFaces{GetMemoryResource(MemTag::Editor)};
			Vector<Guid> savedFallbacks{GetMemoryResource(MemTag::Editor)};
		};
		FontDraft* _draft = nullptr;

		using DraftList = Vector<Guid> FontDraft::*;

		f32 BodyPx() { return EditorTheme::GetMetric(MetricToken::FontBody); }

		FontDraft& DraftFor(const Guid& guid, const FontAsset& font) {
			if (!_draft) {
				_draft = New<FontDraft>(MemTag::Editor);
			}
			if (_draft->asset != guid) {
				_draft->asset = guid;
				_draft->faces.assign(font.faces.begin(), font.faces.end());
				_draft->savedFaces.assign(font.faces.begin(), font.faces.end());
				_draft->fallbacks.assign(font.fallbacks.begin(), font.fallbacks.end());
				_draft->savedFallbacks.assign(font.fallbacks.begin(), font.fallbacks.end());
			}
			return *_draft;
		}

		str StyleOf(FontAsset& font) {
			UI::FontFaceInfo info;
			UI::IFontSystem* fonts = UISystem::GetFontSystem();
			if (!fonts || !fonts->DescribeFont(Handle<FontAsset>(&font), info)) {
				return {};
			}
			str text = info.weightMin == info.weightMax ? std::format("{}", info.weightMin)
														: std::format("{}-{}", info.weightMin, info.weightMax);
			if (info.variableItalic) {
				text += " Upright/Italic";
			} else if (info.italic) {
				text += " Italic";
			}
			return text;
		}

		str StyleOf(const Guid& guid) {
			Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
			if (!loaded) {
				return {};
			}
			str text = loaded->GetAssetTypeId() == FontAsset::GetReflectType()->name ? StyleOf(static_cast<FontAsset&>(*loaded)) : str{};
			AssetDatabase::UnloadAsset(loaded);
			return text;
		}

		ElementHandle AddRow(Context& context, ElementHandle parent) {
			ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			Style style;
			style.flexDirection = FlexDirection::Row;
			style.alignItems = Align::Center;
			style.width = Length::Percent(1.0f);
			style.padding = {8.0f, 0.0f, 8.0f, 0.0f};
			style.margin = {0.0f, 2.0f, 0.0f, 2.0f};
			context.SetStyle(row, style);
			return row;
		}

		void AddLine(Context& context, ElementHandle parent, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			Style style;
			style.margin = {8.0f, 4.0f, 8.0f, 2.0f};
			context.SetStyle(line, style);
			context.SetHoverable(line, false);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
							EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
		}

		str Caption(const Guid& fallback) {
			const Path path = AssetDatabase::LookupPath(fallback);
			return path.ToString().empty() ? std::format("Missing ({})", fallback.ToString()) : str(path.GetFilename());
		}

		bool TakesFont(const DragPayload& payload, const Guid& self) {
			if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>()) {
				return false;
			}
			const AssetDragData& dragged = payload.data.As<AssetDragData>();
			return StringHash{dragged.assetTypeHash} == FontAsset::GetReflectType()->name && dragged.guid != self;
		}

		void OpenCandidates(Context& context, const Guid& self, DraftList list, std::function<void(const Guid&)> onPick) {
			Vector<AssetBrowserOps::AssignableAsset> candidates{GetMemoryResource(MemTag::Editor)};
			const Vector<Guid>& listed = _draft->*list;
			FontInspectorOps::CollectCandidates(self, {listed.data(), listed.size()}, candidates);
			Vector<Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			for (const AssetBrowserOps::AssignableAsset& candidate : candidates) {
				Widgets::MenuItem item;
				item.label = candidate.display;
				item.onSelect = [onPick, picked = candidate.guid]() { onPick(picked); };
				items.push_back(std::move(item));
			}
			if (items.empty()) {
				Widgets::MenuItem none;
				none.label = "No other fonts";
				none.enabled = false;
				items.push_back(std::move(none));
			}
			Widgets::OpenMenu(context, context.GetPointerPosition(), {items.data(), items.size()}, UISystem::GetDefaultFont(), BodyPx());
		}

		void BuildListRow(Context& context, ElementHandle parent, const Guid& self, DraftList list, size_t index,
						  const std::function<void()>& rebuild) {
			ElementHandle row = AddRow(context, parent);
			const Guid entry = (_draft->*list)[index];
			const str caption = Caption(entry);
			Context* contextPtr = &context;

			ObjectField::Desc field;
			field.caption = caption;
			field.accepts = [self](const DragPayload& payload) { return TakesFont(payload, self); };
			field.onDrop = [self, list, index, rebuild](const DragPayload& payload, Vec2) {
				if (_draft && index < (_draft->*list).size() && TakesFont(payload, self)) {
					(_draft->*list)[index] = payload.data.As<AssetDragData>().guid;
					rebuild();
				}
			};
			field.pickTooltip = "Select Font";
			field.onPick = [contextPtr, self, list, index, rebuild]() {
				OpenCandidates(*contextPtr, self, list, [list, index, rebuild](const Guid& picked) {
					if (_draft && index < (_draft->*list).size()) {
						(_draft->*list)[index] = picked;
						rebuild();
					}
				});
			};
			field.onClear = [list, index, rebuild]() {
				if (_draft && index < (_draft->*list).size()) {
					(_draft->*list).erase((_draft->*list).begin() + static_cast<std::ptrdiff_t>(index));
					rebuild();
				}
			};
			field.resolveTarget = [entry]() -> EditorPingTarget { return {.asset = entry}; };
			ObjectField::Build(context, row, field);

			if (list == &FontDraft::faces) {
				ElementHandle declared = context.CreateElement();
				context.AppendChild(row, declared);
				Style gap;
				gap.margin = {6.0f, 0.0f, 6.0f, 0.0f};
				context.SetStyle(declared, gap);
				context.SetHoverable(declared, false);
				context.SetText(declared, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
								EditorTheme::GetColor(ColorToken::WidgetTextDim), StyleOf(entry));
			}

			auto move = [list, index, rebuild](size_t to) {
				if (_draft && index < (_draft->*list).size() && to < (_draft->*list).size()) {
					std::swap((_draft->*list)[index], (_draft->*list)[to]);
					rebuild();
				}
			};
			if (index > 0) {
				EditorIcons::IconButton(context, row, EditorIcons::Glyph::MoveUp, "^", "Move Up", [move, index]() { move(index - 1); }, BodyPx());
			}
			if (index + 1 < (_draft->*list).size()) {
				EditorIcons::IconButton(context, row, EditorIcons::Glyph::MoveDown, "v", "Move Down", [move, index]() { move(index + 1); }, BodyPx());
			}
		}

		void BuildList(Context& context, ElementHandle parent, const Guid& self, DraftList list, std::string_view addLabel,
					   const std::function<void()>& rebuild) {
			for (size_t index = 0; index < (_draft->*list).size(); ++index) {
				BuildListRow(context, parent, self, list, index, rebuild);
			}

			Context* contextPtr = &context;
			auto append = [list, rebuild](const Guid& picked) {
				if (_draft) {
					(_draft->*list).push_back(picked);
					rebuild();
				}
			};
			ElementHandle addRow = AddRow(context, parent);
			context.Events(addRow).onDragOver = [self](const DragPayload& payload) { return TakesFont(payload, self); };
			context.Events(addRow).onDrop = [self, append](const DragPayload& payload, Vec2) {
				if (TakesFont(payload, self)) {
					append(payload.data.As<AssetDragData>().guid);
				}
			};
			Widgets::ButtonDesc add;
			add.label = addLabel;
			add.font = UISystem::GetDefaultFont();
			add.fontSizePx = BodyPx();
			add.onClick = [contextPtr, self, list, append]() { OpenCandidates(*contextPtr, self, list, append); };
			Widgets::Button(context, addRow, add);
		}

		void DrawFontInspector(Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
			FontAsset& font = static_cast<FontAsset&>(asset);
			const Path path = AssetDatabase::LookupPath(guid);
			const std::function<void()> rebuild = PropertyDrawers::GetInspectorRebuilder();
			const bool engineFont = path.ToString().empty() || path.ToString().starts_with("engine://");

			AddInspectorHeader(context, parent, "Faces", EditorTheme::GetColor(ColorToken::TextStrong));
			AddLine(context, parent, "Bold/italic/other weight files of this font (for font-weight/font-style)");
			if (const str declared = StyleOf(font); !declared.empty()) {
				AddLine(context, parent, std::format("This file: {}", declared));
			}
			if (engineFont) {
				for (const Guid& face : font.faces) {
					AddLine(context, parent, Caption(face));
				}
			} else {
				DraftFor(guid, font);
				BuildList(context, parent, guid, &FontDraft::faces, "Add Face", rebuild);
			}

			AddInspectorHeader(context, parent, "Fallback Fonts", EditorTheme::GetColor(ColorToken::TextStrong));
			AddLine(context, parent, "Searched top to bottom for characters this font lacks (installed fonts last)");
			if (engineFont) {
				for (const Guid& fallback : font.fallbacks) {
					AddLine(context, parent, Caption(fallback));
				}
				AddLine(context, parent, "Engine font (import a font into the project to list faces + fallbacks)");
				return;
			}
			BuildList(context, parent, guid, &FontDraft::fallbacks, "Add Fallback", rebuild);

			ElementHandle row = AddRow(context, parent);
			Style end = context.GetStyle(row);
			end.justifyContent = Align::End;
			end.margin = {0.0f, 6.0f, 0.0f, 2.0f};
			context.SetStyle(row, end);

			Widgets::ButtonDesc revert;
			revert.label = "Revert";
			revert.font = UISystem::GetDefaultFont();
			revert.fontSizePx = BodyPx();
			revert.onClick = [rebuild]() {
				if (_draft) {
					_draft->asset = Guid{};
					rebuild();
				}
			};
			Widgets::Button(context, row, revert);

			Widgets::ButtonDesc apply;
			apply.label = "Apply";
			apply.font = UISystem::GetDefaultFont();
			apply.fontSizePx = BodyPx();
			apply.onClick = [path, rebuild]() {
				if (!_draft || (_draft->faces == _draft->savedFaces && _draft->fallbacks == _draft->savedFallbacks)) {
					return;
				}
				const Vector<Guid> faces = _draft->faces;
				const Vector<Guid> fallbacks = _draft->fallbacks;
				FontInspectorOps::Apply(path, {faces.data(), faces.size()}, {fallbacks.data(), fallbacks.size()});
				_draft->asset = Guid{};
				rebuild();
			};
			Widgets::Button(context, row, apply);
		}

	} // anonymous namespace

	void RegisterFontAssetInspector() {
		PropertyDrawers::Initialize();
		PropertyDrawers::RegisterAssetInspector(FontAsset::GetReflectType()->name, &DrawFontInspector);
	}

	void TerminateFontAssetInspector() {
		if (_draft) {
			Delete(_draft);
			_draft = nullptr;
		}
	}

} // namespace Shalltear
