#include "SceneFileFlows.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <memory>

namespace Shalltear::SceneFileFlows {

	namespace {

		Color TextNormal() { return EditorTheme::GetColor(EditorTheme::ColorToken::Text); }
		Color TextDim() { return EditorTheme::GetColor(EditorTheme::ColorToken::TextDim); }

		UI::ElementHandle BeginPopupCard(UI::Context& context) {
			UI::ElementHandle card = context.CreateElement();
			UI::Style style;
			style.minWidth = UI::Length::Pixel(320.0f);
			context.SetStyle(card, style);
			context.AddClass(card, EditorTheme::Class::PopupCard);
			UI::Context* contextPtr = &context;
			context.Events(card).onPopupClosed = [contextPtr, card]() {
				contextPtr->DestroyElementNextUpdate(card);
			};
			return card;
		}

		void AddPopupTitle(UI::Context& context, UI::ElementHandle card, std::string_view text) {
			UI::ElementHandle title = context.CreateElement();
			context.AppendChild(card, title);
			UI::Style style;
			style.margin = {0.0f, 0.0f, 0.0f, 10.0f};
			context.SetStyle(title, style);
			context.SetText(title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(EditorTheme::MetricToken::FontHeading), TextNormal(), text);
		}

		UI::ElementHandle AddButtonRow(UI::Context& context, UI::ElementHandle card) {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(card, row);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.justifyContent = UI::Align::End;
			style.margin = {0.0f, 10.0f, 0.0f, 0.0f};
			context.SetStyle(row, style);
			return row;
		}

		void AddPopupButton(UI::Context& context, UI::ElementHandle row, UI::ElementHandle card,
							std::string_view label, std::function<void()> action, bool isDefault = false) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(EditorTheme::MetricToken::FontRow);
			if (isDefault) {
				desc.defaultForPopup = card;
			}
			UI::Context* contextPtr = &context;
			desc.onClick = [contextPtr, card, action = std::move(action)]() {
				contextPtr->ClosePopup(card);
				if (action) {
					action();
				}
			};
			UI::ElementHandle button = UI::Widgets::Button(context, row, desc);
			UI::Style style = context.GetStyle(button);
			style.margin = {6.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(button, style);
		}

		constexpr UI::PopupFlags DIALOG_FLAGS =
			UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center | UI::PopupFlags::Scrim;

		struct TextPopup {
			UI::ElementHandle card;
			UI::ElementHandle field;
			std::shared_ptr<str> value;
		};

		TextPopup BeginTextPopup(UI::Context& context, const str& title, const str& initialText) {
			TextPopup popup;
			popup.card = BeginPopupCard(context);
			AddPopupTitle(context, popup.card, title);
			popup.value = std::make_shared<str>(initialText);

			UI::Widgets::TextFieldDesc field;
			field.text = initialText;
			field.font = UISystem::GetDefaultFont();
			field.fontSizePx = EditorTheme::GetMetric(EditorTheme::MetricToken::FontRow);
			auto value = popup.value;
			field.onCommit = [value](std::string_view text) {
				*value = str(text);
			};
			popup.field = UI::Widgets::TextField(context, popup.card, field);
			return popup;
		}

		void EndTextPopup(UI::Context& context, const TextPopup& popup, std::function<void(const str&)> onAccept,
						  const str& acceptLabel = "OK") {
			UI::Context* contextPtr = &context;
			UI::ElementHandle buttons = AddButtonRow(context, popup.card);
			AddPopupButton(context, buttons, popup.card, "Cancel", {});
			AddPopupButton(context, buttons, popup.card, acceptLabel, [contextPtr, field = popup.field, value = popup.value, onAccept = std::move(onAccept)]() {
							   str live = contextPtr->GetTextString(field);
							   if (!live.empty()) {
								   *value = live;
							   }
							   if (onAccept && !value->empty()) {
								   onAccept(*value);
							   } }, true);
			context.OpenPopup(popup.card, {}, DIALOG_FLAGS);
			context.SetFocus(popup.field);
		}

		void OpenPathPopup(UI::Context& context, str title, str initialPath,
						   std::function<void(const str&)> onAccept) {
			TextPopup popup = BeginTextPopup(context, title, initialPath);
			UI::ElementHandle card = popup.card;
			UI::ElementHandle fieldElement = popup.field;
			auto pathValue = popup.value;

			Vector<str> recents{GetMemoryResource(MemTag::Editor)};
			EditorSceneOps::GetRecentScenes(recents);
			UI::Context* contextPtr = &context;
			for (const str& recent : recents) {
				UI::ElementHandle row = context.CreateElement();
				context.AppendChild(card, row);
				UI::Style rowStyle;
				rowStyle.padding = {4.0f, 2.0f, 4.0f, 2.0f};
				rowStyle.cursor = SystemCursor::Hand;
				context.SetStyle(row, rowStyle);
				context.AddClass(row, StringHash("slt-tree-row"));
				context.SetText(row, UISystem::GetDefaultFont(), EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody), TextDim(), recent);
				context.Events(row).onClick = [contextPtr, fieldElement, pathValue, recent]() {
					*pathValue = recent;
					contextPtr->SetTextString(fieldElement, recent);
				};
			}

			EndTextPopup(context, popup, std::move(onAccept));
		}

		str SceneFolderSeed() {
			str root = AssetBrowserOps::GetDefaultCreateFolder().ToString();
			return (root.empty() ? str("project://Assets") : root) + "/Scenes/";
		}

		void UnloadAllForTransition() {
			EditorSelection::Clear();
			UndoSystem::Clear();
			SceneSystem::UnloadAllScenes();
			SceneSystem::EndFrame();
		}

		void LoadIntoFreshWorld(const Path& path) {
			UnloadAllForTransition();
			if (!EditorSceneOps::OpenScene(path).IsValid()) {
				EditorSceneOps::NewScene("Untitled");
			}
		}

		void SaveAllDirtyWithKnownPaths() {
			for (const Handle<Scene>& scene : SceneSystem::GetLoadedScenes()) {
				if (!EditorSceneOps::IsDirty(scene)) {
					continue;
				}
				if (EditorSceneOps::GetPrefabTarget(scene).IsValid()) {
					EditorSceneOps::SavePrefabScene(scene);
					continue;
				}
				Path path = EditorSceneOps::GetScenePath(scene);
				if (path.ToString().empty()) {
					SLT_LOG_WARN("Editor", "scene '{}' has no save path. Changes discarded (use Save As)",
								 scene.Get() ? scene.Get()->name : str("?"));
					continue;
				}
				EditorSceneOps::SaveScene(scene, path);
			}
		}

	} // anonymous namespace

	UI::ElementHandle OpenConfirmPopup(UI::Context& context, str message,
									   str acceptLabel, std::function<void()> onAccept,
									   str altLabel, std::function<void()> onAlt,
									   str cancelLabel, std::function<void()> onCancel) {
		return OpenDetailedConfirmPopup(context, std::move(message), {}, std::move(acceptLabel), std::move(onAccept),
										std::move(altLabel), std::move(onAlt), std::move(cancelLabel), std::move(onCancel));
	}

	UI::ElementHandle OpenDetailedConfirmPopup(UI::Context& context, str message, std::span<const str> details,
											   str acceptLabel, std::function<void()> onAccept,
											   str altLabel, std::function<void()> onAlt,
											   str cancelLabel, std::function<void()> onCancel) {
		UI::ElementHandle card = BeginPopupCard(context);
		AddPopupTitle(context, card, message);
		for (const str& detail : details) {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(card, line);
			UI::Style style;
			style.margin = {8.0f, 0.0f, 0.0f, 3.0f};
			context.SetStyle(line, style);
			context.SetText(line, UISystem::GetDefaultFont(),
							EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody), TextDim(), detail);
		}
		UI::ElementHandle buttons = AddButtonRow(context, card);
		if (!cancelLabel.empty()) {
			AddPopupButton(context, buttons, card, cancelLabel, std::move(onCancel));
		}
		if (!altLabel.empty()) {
			AddPopupButton(context, buttons, card, altLabel, std::move(onAlt));
		}
		if (!acceptLabel.empty()) {
			AddPopupButton(context, buttons, card, acceptLabel, std::move(onAccept), true);
		}
		context.OpenPopup(card, {}, DIALOG_FLAGS);
		return card;
	}

	UI::ElementHandle OpenTextInputPopup(UI::Context& context, str title, str initialText,
										 std::function<void(const str&)> onAccept, str acceptLabel) {
		TextPopup popup = BeginTextPopup(context, title, initialText);
		EndTextPopup(context, popup, std::move(onAccept), acceptLabel);
		return popup.card;
	}

	void ConfirmUnsavedThen(UI::Context& context, std::function<void()> proceed) {
		bool scenes = EditorSceneOps::AnyDirty();
		bool assets = EditorAssetOps::AnyDirty();
		if (!scenes && !assets) {
			if (proceed) {
				proceed();
			}
			return;
		}
		str what = scenes && assets ? str("scene and asset") : (scenes ? str("scene") : str("asset"));
		OpenConfirmPopup(
			context, "There are unsaved " + what + " changes.",
			"Save All & Continue", [proceed]() { SaveAllDirtyWithKnownPaths(); EditorAssetOps::SaveAllDirty(); if (proceed) { proceed(); } },
			"Discard", [proceed]() { EditorAssetOps::DiscardAllDirty(); if (proceed) { proceed(); } });
	}

	void NewScene(UI::Context& context) {
		ConfirmUnsavedThen(context, []() {
			UnloadAllForTransition();
			EditorSceneOps::NewScene("Untitled");
		});
	}

	void OpenScene(UI::Context& context) {
		UI::Context* contextPtr = &context;
		ConfirmUnsavedThen(context, [contextPtr]() {
			OpenPathPopup(*contextPtr, "Open Scene", SceneFolderSeed(), [](const str& path) {
				LoadIntoFreshWorld(Path(path));
			});
		});
	}

	void OpenScenePath(UI::Context& context, Path path) {
		ConfirmUnsavedThen(context, [path]() {
			LoadIntoFreshWorld(path);
		});
	}

	void AddScenePath(Path path) {
		EditorSceneOps::LoadSceneAdditive(path);
	}

	void LeavePrefabStage(UI::Context& context, std::function<void()> then) {
		Handle<Scene> stage = EditorSceneOps::GetPrefabStage();
		if (!stage.IsValid()) {
			if (then) {
				then();
			}
			return;
		}
		if (!EditorSceneOps::IsDirty(stage)) {
			EditorSceneOps::LeavePrefabStage();
			if (then) {
				then();
			}
			return;
		}
		str message = "Apply changes to '" + stage.Get()->name + "' before leaving?";
		OpenConfirmPopup(
			context, std::move(message),
			"Apply & Leave", [then]() {
				EditorSceneOps::SavePrefabScene(EditorSceneOps::GetPrefabStage());
				EditorSceneOps::LeavePrefabStage();
				if (then) {
					then();
				} },
			"Discard", [then]() {
				EditorSceneOps::LeavePrefabStage();
				if (then) {
					then();
				} });
	}

	void OpenPrefabPath(UI::Context& context, Path path) {
		Guid guid = AssetDatabase::LookupGuid(path);
		if (!guid.IsValid()) {
			SLT_LOG_WARN("Editor", "'{}' isn't an indexed asset", path.ToString());
			return;
		}
		if (EditorSceneOps::GetPrefabTarget(EditorSceneOps::GetPrefabStage()) == guid) {
			return;
		}
		LeavePrefabStage(context, [guid]() {
			EditorSceneOps::OpenPrefab(guid);
		});
	}

	void SaveScene(UI::Context& context, Handle<Scene> scene) {
		if (!scene.IsValid()) {
			scene = SceneSystem::GetPrimaryScene();
		}
		if (!scene.IsValid()) {
			return;
		}
		if (EditorSceneOps::GetPrefabTarget(scene).IsValid()) {
			EditorSceneOps::SavePrefabScene(scene);
			return;
		}
		Path path = EditorSceneOps::GetScenePath(scene);
		if (path.ToString().empty()) {
			SaveSceneAs(context, scene);
			return;
		}
		EditorSceneOps::SaveScene(scene, path);
	}

	void SaveSceneAs(UI::Context& context, Handle<Scene> scene) {
		if (!scene.IsValid()) {
			scene = SceneSystem::GetPrimaryScene();
		}
		Scene* resolved = scene.Get();
		if (!resolved) {
			return;
		}
		str initial = EditorSceneOps::GetScenePath(scene).ToString();
		if (initial.empty()) {
			initial = SceneFolderSeed() + resolved->name + ".scene";
		}
		OpenPathPopup(context, "Save Scene As", initial, [scene](const str& path) {
			EditorSceneOps::SaveScene(scene, Path(path));
		});
	}

} // namespace Shalltear::SceneFileFlows
