#pragma once

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Core/Types.h"

#include <thread>

namespace ThreadAllocationWatch {

	void Watch(std::thread::id thread);

	void Stop();

	Shalltear::u64 GetCount();

} // namespace ThreadAllocationWatch

#endif
