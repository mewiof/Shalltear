#include "InspectorPanel.h"
#include "SceneFileFlows.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

namespace Shalltear {

	namespace {

		using EditorTheme::MetricToken;

		f32 FieldFont() { return EditorTheme::GetMetric(MetricToken::FontBody); }

		void Note(UI::Context& context, UI::ElementHandle parent, EditorTheme::ColorToken color, std::string_view text) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			context.SetHoverable(element, false);
			UI::Style style;
			style.padding = UI::Edges::All(8.0f);
			context.SetStyle(element, style);
			context.SetText(element, UISystem::GetDefaultFont(), FieldFont(), EditorTheme::GetColor(color), text);
		}

		UI::ElementHandle MakeActionRow(UI::Context& context, UI::ElementHandle parent, std::string_view label = {}) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(parent, row);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.padding = {8.0f, 5.0f, 8.0f, 1.5f};
			style.width = UI::Length::Percent(1.0f);
			context.SetStyle(row, style);
			if (!label.empty()) {
				UI::ElementHandle labelElement = context.CreateElement();
				context.AppendChild(row, labelElement);
				UI::Style labelStyle;
				labelStyle.width = UI::Length::Pixel(110.0f);
				context.SetStyle(labelElement, labelStyle);
				context.SetHoverable(labelElement, false);
				context.SetText(labelElement, UISystem::GetDefaultFont(), FieldFont(),
								EditorTheme::GetColor(EditorTheme::ColorToken::TextDim), label);
			}
			return row;
		}

	} // anonymous namespace

	void RegisterPrefabAssetInspector() {
		PropertyDrawers::Initialize();

		PropertyDrawers::RegisterAssetInspector(
			StringHash("PrefabAsset"),
			[](UI::Context& context, UI::ElementHandle parent, const Guid& guid, Asset& asset) {
				PrefabAsset& prefab = static_cast<PrefabAsset&>(asset);
				PrefabKind kind = prefab.GetKind();
				const char* header = kind == PrefabKind::Model			? "Model Prefab"
									 : prefab.GetBasePrefab().IsValid() ? "Prefab Variant"
																		: "Prefab";
				AddInspectorHeader(context, parent, header, EditorTheme::GetColor(EditorTheme::ColorToken::TextStrong));

				if (Guid basePrefab = prefab.GetBasePrefab(); basePrefab.IsValid()) {
					Path basePath = AssetDatabase::LookupPath(basePrefab);
					str baseName = basePath.ToString().empty() ? str("(missing)") : str(basePath.GetFilename());
					UI::ElementHandle row = MakeActionRow(context, parent, "Base");
					if (baseName == "(missing)") {
						UI::ElementHandle text = context.CreateElement();
						context.AppendChild(row, text);
						context.SetHoverable(text, false);
						context.SetText(text, UISystem::GetDefaultFont(), FieldFont(),
										EditorTheme::GetColor(EditorTheme::ColorToken::StatusWarn), baseName);
					} else {
						UI::Widgets::ButtonDesc reveal;
						reveal.label = baseName;
						reveal.font = UISystem::GetDefaultFont();
						reveal.fontSizePx = FieldFont();
						reveal.onClick = [basePrefab]() {
							EditorSelection::SelectAsset(basePrefab);
							EditorEvents::PingRequested.Publish(EventBus::GetGlobal(),
																EditorPingRequestedEvent{{.asset = basePrefab}});
						};
						UI::Widgets::Button(context, row, reveal);
					}
				}

				if (kind == PrefabKind::Model) {
					Note(context, parent, EditorTheme::ColorToken::TextDim,
						 "Imported from a model file. Its contents are rebuilt from the file on import (can't be edited here).");
					Note(context, parent, EditorTheme::ColorToken::TextDim,
						 "Drag it into a scene and override the instance (or make a Prefab Variant).");
					return;
				}

				UI::Widgets::ButtonDesc open;
				open.label = "Open Prefab";
				open.font = UISystem::GetDefaultFont();
				open.fontSizePx = FieldFont();
				open.onClick = [contextPtr = &context, guid]() {
					if (EditorSceneOps::GetPrefabTarget(EditorSceneOps::GetPrefabStage()) == guid) {
						return;
					}
					SceneFileFlows::LeavePrefabStage(*contextPtr, [guid]() {
						EditorSceneOps::OpenPrefab(guid);
					});
				};
				UI::Widgets::Button(context, MakeActionRow(context, parent), open);

				InspectorPanel* panel = InspectorPanel::GetBuildingPanel();
				Handle<SceneObject> root = EditorSceneOps::GetPrefabPreviewRoot(guid);
				if (!panel) {
					return;
				}
				if (!root.IsValid()) {
					Note(context, parent, EditorTheme::ColorToken::StatusWarn,
						 "This prefab couldn't be composed.");
					return;
				}
				panel->BuildPrefabAssetBody(context, root, guid);
			});
	}

} // namespace Shalltear
