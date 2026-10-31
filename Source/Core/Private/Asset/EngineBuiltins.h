#pragma once

#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace EngineBuiltins {

		inline constexpr Guid PresentPassShader{0x376e2ff23e456f74, 0x32aa3655589414a0};

		inline constexpr Guid LitShader{0x5d0eee565c0eb506, 0xd813bbcaa86ba3a8};

		inline constexpr Guid DebugLinesShader{0x5c1d9a4e7b30f682, 0xa41f6d28c95e0b73};

		inline constexpr Guid WireframeShader{0x7d41ba60e2c85f39, 0x1ab7e594cd06283f};

		inline constexpr Guid BlitShader{0xab2785d7cb197a4c, 0x024a0663fec99085};

		inline constexpr Guid DepthCopyShader{0x8f67be9cadb890a4, 0x98b2bb207a41f7e2};

		inline constexpr Guid DebugViewShader{0xe579e36a1ff6b994, 0xb51481f739564cc8};

		inline constexpr Guid DebugOverlayShader{0x3f6c02b8ad415e79, 0xc7d9418ba2e60f35};

		inline constexpr Guid OutlineShader{0xde0ec401677a49c2, 0xa4490098cd78380f};

		inline constexpr Guid SkyShader{0x9c4b17e6a2d05f38, 0x64e18b3cf7a9d201};

		inline constexpr Guid SkyPrefilterShader{0x6a3f1c9e2b847d05, 0xe1c74b2a90f36d18};

		inline constexpr Guid UIShader{0x338097fb9c0c87b0, 0x92e117f93e38c514};

		inline constexpr Guid DefaultFont{0x5d280fd701b66993, 0xda09822ec0a9b52b};

		inline constexpr Guid Roots[] = {
			PresentPassShader,
			LitShader,
			DebugLinesShader,
			WireframeShader,
			SkyShader,
			UIShader,
			BlitShader,
			DebugViewShader,
			DebugOverlayShader,
			DefaultFont,
			DepthCopyShader,
			OutlineShader,
			SkyPrefilterShader,
		};

	} // namespace EngineBuiltins

} // namespace Shalltear
