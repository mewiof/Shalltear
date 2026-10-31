#pragma once

#include "Shalltear.h"

#include "LevelDefinition.h"

// FD
namespace Shalltear {
class UIPanelRenderer;
}

// runs one level: counts down its time, shows the coins + the clock, pauses
// the game, moves on when the level is won (or the time runs out)
//
// Sits on the same object as the UIPanelRenderer that draws the HUD (below)
// A component is enabled after the ones above -> the HUD is built by
// the time OnEnable looks for its buttons
class LevelController : public Behavior {
public:
	LevelController();

	SLT_REFLECT_COMPONENT(LevelController, Behavior)
	SLT_REFLECT_FIELD(level)
	SLT_REFLECT_FIELD(player)
	SLT_REFLECT_FIELD(menuScene)
	SLT_END_REFLECT()

	// this level's rules
	Handle<LevelDefinition> level;
	// the object with the PlayerController
	Handle<SceneObject> player;
	// where a lost level (or the pause menu's Main Menu button) goes
	Handle<SceneAsset> menuScene;

protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnUpdate() override;

private:
	UIPanelRenderer *GetHud() const;
	void SetPaused(bool paused);
	void GoTo(Handle<SceneAsset> scene);

	InputActionMap _controls;
	ActionId _pause;
	f32 _timeLeft = 0.0f;
	bool _paused = false;
	bool _finished = false;
};
