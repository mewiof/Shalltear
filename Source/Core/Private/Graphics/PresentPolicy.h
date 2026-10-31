#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear::RHI {

	enum class PresentPolicy : u8 {
		Fifo,
		Mailbox,

		Count,
	};

	static_assert(static_cast<u8>(PresentPolicy::Count) == 2);

	inline constexpr PresentPolicy ChoosePresentPolicy(bool vsync, bool mailboxOffered) noexcept {
		return (!vsync && mailboxOffered) ? PresentPolicy::Mailbox : PresentPolicy::Fifo;
	}

} // namespace Shalltear::RHI
