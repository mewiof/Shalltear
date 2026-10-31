#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class AudioRandomContainer;

	namespace AudioImpl {

		struct AudioRandomPickState {
			u32 sequentialNext = 0;
			Vector<u32> pool{GetMemoryResource(MemTag::Audio)};
			Vector<u32> recent{GetMemoryResource(MemTag::Audio)};
			u32 elementCount = 0;
		};

		// valid until the next call
		AudioRandomPickState& GetRandomPickState(u64 container, u64 owner);

		i32 PickRandomElement(const AudioRandomContainer& container, AudioRandomPickState& state);

		f64 DrawUniform();
		f64 DrawBetween(f64 low, f64 high);
		u32 DrawIndex(u32 count);

	} // namespace AudioImpl

} // namespace Shalltear
