#include "AudioChannels.h"

#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			using P = SpeakerPosition;

			constexpr f32 FOLD = 0.70710678f;

			i32 Find(const ChannelLayout& layout, SpeakerPosition position) [[clang::nonblocking]] {
				for (u32 channel = 0; channel < layout.count; ++channel) {
					if (layout.positions[channel] == position) {
						return static_cast<i32>(channel);
					}
				}
				return -1;
			}

			bool IsLeft(SpeakerPosition position) [[clang::nonblocking]] {
				return position == P::FrontLeft || position == P::BackLeft || position == P::SideLeft;
			}

			bool IsRight(SpeakerPosition position) [[clang::nonblocking]] {
				return position == P::FrontRight || position == P::BackRight || position == P::SideRight;
			}

			struct Folder {
				const ChannelLayout& out;
				f32* gains;
				u32 inCount;

				void Add(i32 output, u32 input, f32 gain) [[clang::nonblocking]] {
					if (output >= 0) {
						gains[static_cast<u32>(output) * inCount + input] += gain;
					}
				}

				i32 Centre() const [[clang::nonblocking]] {
					const i32 mono = Find(out, P::Mono);
					return mono >= 0 ? mono : Find(out, P::FrontCenter);
				}

				void Spread(u32 input, f32 gain) [[clang::nonblocking]] {
					const i32 left = Find(out, P::FrontLeft);
					const i32 right = Find(out, P::FrontRight);
					if (left >= 0 && right >= 0) {
						Add(left, input, gain * FOLD);
						Add(right, input, gain * FOLD);
					} else {
						Add(Centre() >= 0 ? Centre() : 0, input, gain);
					}
				}

				void Side(u32 input, SpeakerPosition twin, SpeakerPosition front) [[clang::nonblocking]] {
					if (const i32 other = Find(out, twin); other >= 0) {
						Add(other, input, 1.0f);
					} else if (const i32 same = Find(out, front); same >= 0) {
						Add(same, input, FOLD);
					} else {
						Add(Centre() >= 0 ? Centre() : 0, input, FOLD);
					}
				}
			};

		} // anonymous namespace

		ChannelLayout MakeStandardLayout(u32 channels) {
			static constexpr P LAYOUTS[MAX_AUDIO_CHANNELS][MAX_AUDIO_CHANNELS] = {
				{P::Mono},
				{P::FrontLeft, P::FrontRight},
				{P::FrontLeft, P::FrontRight, P::FrontCenter},
				{P::FrontLeft, P::FrontRight, P::FrontCenter, P::BackCenter},
				{P::FrontLeft, P::FrontRight, P::FrontCenter, P::BackLeft, P::BackRight},
				{P::FrontLeft, P::FrontRight, P::FrontCenter, P::LowFrequency, P::SideLeft, P::SideRight},
				{P::FrontLeft, P::FrontRight, P::FrontCenter, P::LowFrequency, P::BackCenter, P::SideLeft, P::SideRight},
				{P::FrontLeft, P::FrontRight, P::FrontCenter, P::LowFrequency, P::BackLeft, P::BackRight, P::SideLeft, P::SideRight},
			};
			ChannelLayout layout;
			if (channels == 0) {
				return layout;
			}
			layout.count = channels < MAX_AUDIO_CHANNELS ? channels : MAX_AUDIO_CHANNELS;
			for (u32 channel = 0; channel < layout.count; ++channel) {
				layout.positions[channel] = LAYOUTS[layout.count - 1][channel];
			}
			return layout;
		}

		void ComputeChannelGains(const ChannelLayout& in, const ChannelLayout& out, f32 stereoPan, f32* gains) {
			for (u32 index = 0; index < out.count * in.count; ++index) {
				gains[index] = 0.0f;
			}
			if (in.count == 0 || out.count == 0) {
				return;
			}
			const f32 pan = stereoPan < -1.0f ? -1.0f : (stereoPan > 1.0f ? 1.0f : stereoPan);
			Folder fold{out, gains, in.count};

			if (in.count == 1) {
				const i32 left = Find(out, P::FrontLeft);
				const i32 right = Find(out, P::FrontRight);
				if (left >= 0 && right >= 0) {
					fold.Add(left, 0, std::sqrt((1.0f - pan) * 0.5f));
					fold.Add(right, 0, std::sqrt((1.0f + pan) * 0.5f));
				} else {
					fold.Add(fold.Centre() >= 0 ? fold.Centre() : 0, 0, 1.0f);
				}
				return;
			}

			for (u32 channel = 0; channel < in.count; ++channel) {
				const SpeakerPosition position = in.positions[channel];
				if (const i32 direct = Find(out, position); direct >= 0) {
					fold.Add(direct, channel, 1.0f);
					continue;
				}
				switch (position) {
				case P::Mono:
				case P::FrontCenter:
					fold.Spread(channel, 1.0f);
					break;
				case P::FrontLeft:
				case P::FrontRight:
					fold.Add(fold.Centre() >= 0 ? fold.Centre() : 0, channel, FOLD);
					break;
				case P::SideLeft:
					fold.Side(channel, P::BackLeft, P::FrontLeft);
					break;
				case P::SideRight:
					fold.Side(channel, P::BackRight, P::FrontRight);
					break;
				case P::BackLeft:
					fold.Side(channel, P::SideLeft, P::FrontLeft);
					break;
				case P::BackRight:
					fold.Side(channel, P::SideRight, P::FrontRight);
					break;
				case P::BackCenter: {
					const i32 backLeft = Find(out, P::BackLeft) >= 0 ? Find(out, P::BackLeft) : Find(out, P::SideLeft);
					const i32 backRight = Find(out, P::BackRight) >= 0 ? Find(out, P::BackRight) : Find(out, P::SideRight);
					if (backLeft >= 0 && backRight >= 0) {
						fold.Add(backLeft, channel, FOLD);
						fold.Add(backRight, channel, FOLD);
					} else {
						fold.Spread(channel, FOLD);
					}
					break;
				}
				case P::LowFrequency:
				case P::None:
					break;
				}
			}

			if (pan != 0.0f) {
				const f32 leftScale = pan > 0.0f ? 1.0f - pan : 1.0f;
				const f32 rightScale = pan < 0.0f ? 1.0f + pan : 1.0f;
				for (u32 output = 0; output < out.count; ++output) {
					const f32 scale = IsLeft(out.positions[output]) ? leftScale : (IsRight(out.positions[output]) ? rightScale : 1.0f);
					for (u32 channel = 0; channel < in.count; ++channel) {
						gains[output * in.count + channel] *= scale;
					}
				}
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
