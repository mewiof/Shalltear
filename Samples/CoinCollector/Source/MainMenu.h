#pragma once

#include "Shalltear.h"

// the menu's Play button. Sits below the UIPanelRenderer that draws the
// menu (on the same object) -> the button exists when OnEnable looks for it
class MainMenu : public Behavior {
public:
	SLT_REFLECT_COMPONENT(MainMenu, Behavior)
	SLT_REFLECT_FIELD(firstLevel)
	SLT_END_REFLECT()

	// the scene Play loads
	Handle<SceneAsset> firstLevel;

protected:
	void OnEnable() override;
	void OnDisable() override;
};
