#include "AudioReverbPresets.h"

#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr AudioReverbValues I3DL2(f32 room, f32 roomHF, f32 rolloff, f32 decayTime, f32 decayHFRatio, f32 reflections, f32 reflectionsDelay,
											  f32 reverb, f32 reverbDelay, f32 diffusion, f32 density, f32 hfReference) {
				(void)rolloff;
				AudioReverbValues values;
				values.room = room;
				values.roomHF = roomHF;
				values.roomLF = 0.0f;
				values.decayTime = decayTime;
				values.decayHFRatio = decayHFRatio;
				values.reflections = reflections;
				values.reflectionsDelay = reflectionsDelay;
				values.reverb = reverb;
				values.reverbDelay = reverbDelay;
				values.hfReference = hfReference;
				values.lfReference = 250.0f;
				values.diffusion = diffusion;
				values.density = density;
				return values;
			}

			constexpr AudioReverbValues PRESETS[] = {
				I3DL2(-10000, -100, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -100, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -6000, 0.0f, 0.17f, 0.10f, -1204, 0.001f, 207, 0.002f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -454, 0.0f, 0.40f, 0.83f, -1646, 0.002f, 53, 0.003f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -1200, 0.0f, 1.49f, 0.54f, -370, 0.007f, 1030, 0.011f, 100.0f, 60.0f, 5000.0f),
				I3DL2(-1000, -6000, 0.0f, 0.50f, 0.10f, -1376, 0.003f, -1104, 0.004f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -300, 0.0f, 2.31f, 0.64f, -711, 0.012f, 83, 0.017f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -476, 0.0f, 4.32f, 0.59f, -789, 0.020f, -289, 0.030f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -500, 0.0f, 3.92f, 0.70f, -1230, 0.020f, -2, 0.029f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, 0, 0.0f, 2.91f, 1.30f, -602, 0.015f, -302, 0.022f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -698, 0.0f, 7.24f, 0.33f, -1166, 0.020f, 16, 0.030f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -1000, 0.0f, 10.05f, 0.23f, -602, 0.020f, 198, 0.030f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -4000, 0.0f, 0.30f, 0.10f, -1831, 0.002f, -1630, 0.030f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -300, 0.0f, 1.49f, 0.59f, -1219, 0.007f, 441, 0.011f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -237, 0.0f, 2.70f, 0.79f, -1214, 0.013f, 395, 0.020f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -270, 0.0f, 1.49f, 0.86f, -1204, 0.007f, -4, 0.011f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -3300, 0.0f, 1.49f, 0.54f, -2560, 0.162f, -613, 0.088f, 79.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -800, 0.0f, 1.49f, 0.67f, -2273, 0.007f, 2217, 0.011f, 50.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -2500, 0.0f, 1.49f, 0.21f, -2780, 0.300f, -2014, 0.100f, 27.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -1000, 0.0f, 1.49f, 0.83f, -10000, 0.061f, 500, 0.025f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -2000, 0.0f, 1.49f, 0.50f, -2466, 0.179f, -2514, 0.100f, 21.0f, 100.0f, 5000.0f),
				I3DL2(-1000, 0, 0.0f, 1.65f, 1.50f, -1363, 0.008f, -1153, 0.012f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, -1000, 0.0f, 2.81f, 0.14f, 429, 0.014f, 648, 0.011f, 80.0f, 60.0f, 5000.0f),
				I3DL2(-1000, -4000, 0.0f, 1.49f, 0.10f, -449, 0.007f, 1700, 0.011f, 100.0f, 100.0f, 5000.0f),
				I3DL2(-1000, 0, 0.0f, 8.39f, 1.39f, -115, 0.002f, 985, 0.030f, 50.0f, 42.87f, 5000.0f),
				I3DL2(-1000, -400, 0.0f, 17.23f, 0.56f, -1713, 0.020f, -613, 0.030f, 60.0f, 36.45f, 5000.0f),
				I3DL2(-1000, -151, 0.0f, 7.56f, 0.91f, -626, 0.020f, 774, 0.030f, 50.0f, 6.25f, 5000.0f),
			};
			static_assert(sizeof(PRESETS) / sizeof(PRESETS[0]) == static_cast<size_t>(AudioReverbPreset::User),
						  "a room per preset but User, in the enum's order");

			f32 Amplitude(f32 millibels) {
				return millibels > -10000.0f ? std::pow(10.0f, millibels / 2000.0f) : 0.0f;
			}

			f32 Millibels(f32 amplitude) {
				if (!(amplitude > 0.0f)) {
					return -10000.0f;
				}
				const f32 millibels = 2000.0f * std::log10(amplitude);
				return millibels > -10000.0f ? millibels : -10000.0f;
			}

		} // anonymous namespace

		const AudioReverbValues& GetReverbPreset(AudioReverbPreset preset) {
			const size_t index = static_cast<size_t>(preset);
			return index < sizeof(PRESETS) / sizeof(PRESETS[0]) ? PRESETS[index] : PRESETS[0];
		}

		void WriteSfxReverb(const AudioReverbValues& values, f32 dryLevel, f32* out) {
			out[0] = dryLevel;
			out[1] = values.room;
			out[2] = values.roomHF;
			out[3] = values.decayTime;
			out[4] = values.decayHFRatio;
			out[5] = values.reflections;
			out[6] = values.reflectionsDelay;
			out[7] = values.reverb;
			out[8] = values.reverbDelay;
			out[9] = values.diffusion;
			out[10] = values.density;
			out[11] = values.hfReference;
			out[12] = values.roomLF;
			out[13] = values.lfReference;
		}

		f32 ComputeZoneWeight(f32 distance, f32 minDistance, f32 maxDistance) {
			if (!std::isfinite(distance)) {
				return 0.0f;
			}
			const f32 minimum = minDistance > 0.0f ? minDistance : 0.0f;
			if (distance <= minimum) {
				return 1.0f;
			}
			if (!(maxDistance > minimum) || distance >= maxDistance) {
				return 0.0f;
			}
			return (maxDistance - distance) / (maxDistance - minimum);
		}

		bool BlendReverbZones(std::span<const AudioReverbZoneShape> zones, const Vec3& position, AudioReverbValues& out) {
			f32 total = 0.0f;
			for (const AudioReverbZoneShape& zone : zones) {
				total += ComputeZoneWeight((zone.position - position).Length(), zone.minDistance, zone.maxDistance);
			}
			if (!(total > 0.0f)) {
				out = GetReverbPreset(AudioReverbPreset::Off);
				return false;
			}
			const f32 roomShare = total > 1.0f ? 1.0f / total : 1.0f;
			const f32 share = 1.0f / total;
			f32 room = 0.0f;
			f32 roomHF = 0.0f;
			f32 roomLF = 0.0f;
			f32 reflections = 0.0f;
			f32 reverb = 0.0f;
			AudioReverbValues blend;
			blend.decayTime = 0.0f;
			blend.decayHFRatio = 0.0f;
			blend.reflectionsDelay = 0.0f;
			blend.reverbDelay = 0.0f;
			blend.hfReference = 0.0f;
			blend.lfReference = 0.0f;
			blend.diffusion = 0.0f;
			blend.density = 0.0f;
			for (const AudioReverbZoneShape& zone : zones) {
				const f32 weight = ComputeZoneWeight((zone.position - position).Length(), zone.minDistance, zone.maxDistance);
				if (weight == 0.0f) {
					continue;
				}
				const AudioReverbValues& values = zone.values;
				room += weight * roomShare * Amplitude(values.room);
				const f32 part = weight * share;
				roomHF += part * Amplitude(values.roomHF);
				roomLF += part * Amplitude(values.roomLF);
				reflections += part * Amplitude(values.reflections);
				reverb += part * Amplitude(values.reverb);
				blend.decayTime += part * values.decayTime;
				blend.decayHFRatio += part * values.decayHFRatio;
				blend.reflectionsDelay += part * values.reflectionsDelay;
				blend.reverbDelay += part * values.reverbDelay;
				blend.hfReference += part * values.hfReference;
				blend.lfReference += part * values.lfReference;
				blend.diffusion += part * values.diffusion;
				blend.density += part * values.density;
			}
			blend.room = Millibels(room);
			blend.roomHF = Millibels(roomHF);
			blend.roomLF = Millibels(roomLF);
			blend.reflections = Millibels(reflections);
			blend.reverb = Millibels(reverb);
			out = blend;
			return true;
		}

	} // namespace AudioImpl

} // namespace Shalltear
