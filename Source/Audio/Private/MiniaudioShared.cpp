#include "MiniaudioShared.h"

#include "Shalltear/Core/Memory.h"

#include <cstring>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			struct alignas(16) BlockPrefix {
				size_t size;
			};
			static_assert(sizeof(BlockPrefix) == 16);

			void* OnMalloc(size_t size, void*) {
				try {
					auto* prefix = static_cast<BlockPrefix*>(Allocate(sizeof(BlockPrefix) + size, MemTag::Audio, alignof(BlockPrefix)));
					prefix->size = size;
					return prefix + 1;
				} catch (...) {
					return nullptr;
				}
			}

			void OnFree(void* block, void*) {
				if (block) {
					Free(static_cast<BlockPrefix*>(block) - 1);
				}
			}

			void* OnRealloc(void* block, size_t size, void* user) {
				if (!block) {
					return OnMalloc(size, user);
				}
				if (size == 0) {
					OnFree(block, user);
					return nullptr;
				}
				void* grown = OnMalloc(size, user);
				if (!grown) {
					return nullptr;
				}
				const size_t kept = (static_cast<BlockPrefix*>(block) - 1)->size;
				std::memcpy(grown, block, kept < size ? kept : size);
				OnFree(block, user);
				return grown;
			}

		} // anonymous namespace

		ma_allocation_callbacks MakeMiniaudioAllocationCallbacks() {
			ma_allocation_callbacks callbacks{};
			callbacks.onMalloc = &OnMalloc;
			callbacks.onRealloc = &OnRealloc;
			callbacks.onFree = &OnFree;
			return callbacks;
		}

		SpeakerPosition ToSpeakerPosition(ma_channel channel) {
			switch (channel) {
			case MA_CHANNEL_MONO:
				return SpeakerPosition::Mono;
			case MA_CHANNEL_FRONT_LEFT:
				return SpeakerPosition::FrontLeft;
			case MA_CHANNEL_FRONT_RIGHT:
				return SpeakerPosition::FrontRight;
			case MA_CHANNEL_FRONT_CENTER:
				return SpeakerPosition::FrontCenter;
			case MA_CHANNEL_LFE:
				return SpeakerPosition::LowFrequency;
			case MA_CHANNEL_BACK_LEFT:
				return SpeakerPosition::BackLeft;
			case MA_CHANNEL_BACK_RIGHT:
				return SpeakerPosition::BackRight;
			case MA_CHANNEL_BACK_CENTER:
				return SpeakerPosition::BackCenter;
			case MA_CHANNEL_SIDE_LEFT:
				return SpeakerPosition::SideLeft;
			case MA_CHANNEL_SIDE_RIGHT:
				return SpeakerPosition::SideRight;
			default:
				return SpeakerPosition::None;
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
