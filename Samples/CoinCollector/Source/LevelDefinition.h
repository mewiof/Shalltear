#pragma once

#include "Shalltear.h"

// rules of one level, stored in an asset (not in the scene). Each level
// gets a .level file (made from the Asset Browser's Create menu)
class LevelDefinition : public DataAsset {
public:
	SLT_REFLECT_CLASS(LevelDefinition)
	SLT_REFLECT_PARENT(DataAsset)
	SLT_CREATE_ASSET_MENU("Level Definition", ".level")
	SLT_REFLECT_FIELD(timeLimit)
	SLT_REFLECT_FIELD(coinsToWin)
	SLT_REFLECT_FIELD(nextLevel)
	SLT_END_REFLECT()

	// seconds the player has to finish
	f32 timeLimit = 30.0f;
	i32 coinsToWin = 5;
	// the scene following a win. a win should return to menu if empty
	Handle<SceneAsset> nextLevel;
};
