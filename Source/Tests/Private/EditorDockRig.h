#pragma once

#include "UITestUtil.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

namespace EditorTest {

	using namespace Shalltear;

	struct DockedPanelRig {
		static constexpr StringHash PANEL_TAB = "tab.panel"_hash;
		static constexpr StringHash OTHER_TAB = "tab.other"_hash;

		UITest::MockFontSystem fonts;
		UI::Context context;
		UI::DockManager dock;
		Handle<EditorPanel> panel;

		explicit DockedPanelRig(std::string_view panelTypeName, Vec2I sizePx = {600, 400})
			: context(Desc(sizePx), &fonts), dock(context, Handle<FontAsset>{}) {
			EditorTheme::ApplyToContext(context);
			panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash(panelTypeName)));
			Handle<EditorPanel> built = panel;
			dock.RegisterPanel(PANEL_TAB, "Panel", [built](UI::Context& c, UI::ElementHandle parent) {
				if (EditorPanel* live = built.Get()) {
					live->BuildContent(c, parent);
				}
			});
			dock.RegisterPanel(OTHER_TAB, "Other", [](UI::Context& c, UI::ElementHandle parent) {
				c.AppendChild(parent, c.CreateElement());
			});
			dock.DockPanel(PANEL_TAB, dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel(OTHER_TAB, dock.GetRootNode(), UI::DockZone::Center);
			dock.ActivatePanel(PANEL_TAB);
			Frame();
			Frame();
		}

		void Frame(f32 deltaSeconds = 0.016f) {
			PanelManager::TickAll(deltaSeconds);
			dock.Build(context.GetRoot());
			context.Update();
			PanelManager::SubmitViews(deltaSeconds);
		}

		void ShowTab(StringHash tab) {
			dock.ActivatePanel(tab);
			Frame();
		}

		void SwitchAwayAndBack() {
			ShowTab(OTHER_TAB);
			ShowTab(PANEL_TAB);
			Frame();
		}

		UI::ElementHandle Root() { return context.GetRoot(); }

		static UI::ContextDesc Desc(Vec2I sizePx) {
			UI::ContextDesc desc;
			desc.sizePx = sizePx;
			return desc;
		}
	};

} // namespace EditorTest
