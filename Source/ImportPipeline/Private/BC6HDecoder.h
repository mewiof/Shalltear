#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear::BC6H {

	// RGBA half floats, raster order, reserved mode = black
	void DecodeBlock(const u8 block[16], u16 rgba[64]);

} // namespace Shalltear::BC6H
