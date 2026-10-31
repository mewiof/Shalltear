#include "MainMenu.h"

SLT_REGISTER_TYPE(MainMenu)

// runs when the game starts (not while you edit the scene). A component's hooks
// run only while the game plays. It wires the Play button (by the id it has in
// Menu.slxml) same way LevelController wires pause menu
void MainMenu::OnEnable() {
	UIPanelRenderer *menu = GetOwner()->GetComponent<UIPanelRenderer>().Get();
	if (!menu || !menu->GetContext()) {
		SLT_LOG_WARN("CoinCollector", "MainMenu needs a UIPanelRenderer above it on the same object");
		return;
	}
	Handle<MainMenu> self(this);
	menu->GetContext()->Events(menu->FindElement("play")).onClick = [self] {
		MainMenu *mainMenu = self.Get();
		if (SceneAsset *level = mainMenu ? mainMenu->firstLevel.Get() : nullptr) {
			SceneSystem::ReplaceScenes(level->assetId);
		}
	};
}

void MainMenu::OnDisable() {
	UIPanelRenderer *menu = GetOwner()->GetComponent<UIPanelRenderer>().Get();
	if (menu && menu->GetContext()) {
		menu->GetContext()->Events(menu->FindElement("play")).onClick = nullptr;
	}
}
