#include "LevelController.h"

#include "PlayerController.h"

SLT_REGISTER_TYPE(LevelController)

LevelController::LevelController() {
	_pause = _controls.RegisterAction("Pause"_hash, ActionType::Button);
	_controls.Bind(_pause, {.code = u16(Key::Escape)});
	_controls.Bind(_pause, {.source = InputBinding::Source::GamepadButton, .code = u16(GamepadButton::Start)});
}

void LevelController::OnEnable() {
	LevelDefinition *rules = level.Get();
	_timeLeft = rules ? rules->timeLimit : 0.0f;
	_controls.Enable();

	// the pause menu's two buttons (by the id they have in Hud.slxml). The
	// handler holds a Handle, not `this` -> a click arriving after
	// this component is dead does nothing
	if (UIPanelRenderer *hud = GetHud()) {
		UI::Context *ui = hud->GetContext();
		Handle<LevelController> self(this);
		ui->Events(hud->FindElement("resume")).onClick = [self] {
			if (LevelController *controller = self.Get()) {
				controller->SetPaused(false);
			}
		};
		ui->Events(hud->FindElement("menu")).onClick = [self] {
			if (LevelController *controller = self.Get()) {
				controller->GoTo(controller->menuScene);
			}
		};
	}
	SetPaused(false);
}

void LevelController::OnDisable() {
	_controls.Disable();
	Time::SetTimeScale(1.0f);
	if (UIPanelRenderer *hud = GetHud()) {
		UI::Context *ui = hud->GetContext();
		ui->Events(hud->FindElement("resume")).onClick = nullptr;
		ui->Events(hud->FindElement("menu")).onClick = nullptr;
	}
}

// the HUD on this object (or null when it is missing/not built)
UIPanelRenderer *LevelController::GetHud() const {
	UIPanelRenderer *hud = GetOwner()->GetComponent<UIPanelRenderer>().Get();
	return hud && hud->GetContext() ? hud : nullptr;
}

void LevelController::OnUpdate() {
	_controls.Evaluate();
	if (_controls.WasPerformed(_pause) && !_finished) {
		SetPaused(!_paused);
	}

	LevelDefinition *rules = level.Get();
	SceneObject *playerObject = player.Get();
	PlayerController *hero = playerObject ? playerObject->GetComponent<PlayerController>().Get() : nullptr;
	if (!rules || !hero || _finished) {
		return;
	}

	// GetDeltaTime() is scaled time. While the game is paused it is 0
	_timeLeft = Math::Max(0.0f, _timeLeft - Time::GetDeltaTime());

	if (UIPanelRenderer *hud = GetHud()) {
		UI::Context *ui = hud->GetContext();
		ui->SetTextString(hud->FindElement("coins"), std::format("Coins: {} / {}", hero->GetCoins(), rules->coinsToWin));
		ui->SetTextString(hud->FindElement("timer"), std::format("Time: {}", static_cast<i32>(std::ceil(_timeLeft))));
	}

	if (hero->GetCoins() >= rules->coinsToWin) {
		// won: to the next level (or back to the menu if last)
		GoTo(rules->nextLevel.Get() ? rules->nextLevel : menuScene);
	} else if (_timeLeft <= 0.0f) {
		GoTo(menuScene);
	}
}

void LevelController::SetPaused(bool paused) {
	_paused = paused;
	// time scale 0 stops physics/everything that reads GetDeltaTime().
	// OnUpdate still runs (pause key can unpause)
	Time::SetTimeScale(paused ? 0.0f : 1.0f);
	// sound runs on own clock (paused separately). The
	// player's AudioSource plays every sound in this game (pausing it
	// holds its one-shots too). Game with many sources routes them to a mixer
	// group whose clock is Gameplay instead -> the 0 time scale holds them
	SceneObject *playerObject = player.Get();
	if (AudioSource *audio = playerObject ? playerObject->GetComponent<AudioSource>().Get() : nullptr) {
		if (paused) {
			audio->Pause();
		} else {
			audio->UnPause();
		}
	}
	// the document hides the pause menu with `display: none` -> it stays out
	// of the way while you edit. This shows/hides it again
	if (UIPanelRenderer *hud = GetHud()) {
		UI::Context *ui = hud->GetContext();
		UI::ElementHandle pauseMenu = hud->FindElement("pause-menu");
		UI::Style style = ui->GetStyle(pauseMenu);
		style.display = paused ? UI::Display::Flex : UI::Display::None;
		ui->SetStyle(pauseMenu, style);
	}
}

void LevelController::GoTo(Handle<SceneAsset> scene) {
	SceneAsset *target = scene.Get();
	if (!target || _finished) {
		return;
	}
	_finished = true;
	// every loaded scene is replaced by this one at the end of the frame
	// (safe to ask from anywhere -> a button click included)
	SceneSystem::ReplaceScenes(target->assetId);
}
