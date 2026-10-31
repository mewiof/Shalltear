#include "AudioVoiceEffects.h"

#include "Shalltear/Audio/AudioEffect.h"

#include "AudioChannels.h"
#include "AudioEffectRegistry.h"
#include "AudioRenderer.h"
#include "AudioVoice.h"

#include "Shalltear/Core/Log.h"

#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr u32 SAID_CAPACITY = 32;
			u64 _said[SAID_CAPACITY]{};
			u32 _saidCount = 0;

			void SayUnregisteredOnce(std::string_view name) {
				u64 hash = 1469598103934665603ull;
				for (const char c : name) {
					hash = (hash ^ static_cast<u8>(c)) * 1099511628211ull;
				}
				for (u32 index = 0; index < _saidCount; ++index) {
					if (_said[index] == hash) {
						return;
					}
				}
				if (_saidCount < SAID_CAPACITY) {
					_said[_saidCount++] = hash;
				}
				SLT_LOG_WARN("Audio", "a voice asked for unregistered effect '{}' (sound passes through until it's registered)", name);
			}

		} // anonymous namespace

		void DescribeEffects(AudioVoiceEffects& into, std::span<const AudioVoiceEffect> effects) {
			into.rows.clear();
			into.rows.reserve(effects.size());
			into.followsDistance = false;
			for (const AudioVoiceEffect& effect : effects) {
				AudioVoiceEffectRow& row = into.rows.emplace_back();
				row.name = str(effect.effect);
				if (effect.distanceCurve && !effect.distanceCurve->keys.empty()) {
					row.distance = *effect.distanceCurve;
					row.distanceParameter = effect.distanceParameter;
					into.followsDistance = true;
				}
				row.values.assign(effect.parameters.begin(), effect.parameters.end());
				row.definition = FindEffect(effect.effect);
				if (row.definition) {
					ResolveParameterValues(*row.definition, row.values);
				} else {
					SayUnregisteredOnce(effect.effect);
				}
			}
		}

		void ReadDistance(AudioVoiceEffects& description, f32 share) {
			if (!description.followsDistance) {
				return;
			}
			for (AudioVoiceEffectRow& row : description.rows) {
				if (row.distance.keys.empty() || !row.definition) {
					continue;
				}
				const std::span<const AudioParameterDefinition> parameters = row.definition->GetParameters();
				if (row.distanceParameter >= parameters.size() || row.distanceParameter >= row.values.size()) {
					continue;
				}
				const AudioParameterDefinition& parameter = parameters[row.distanceParameter];
				const f32 at = row.distance.Evaluate(share);
				const f32 clamped = at >= 0.0f ? (at <= 1.0f ? at : 1.0f) : 0.0f;
				row.values[row.distanceParameter] = parameter.minimum + (parameter.maximum - parameter.minimum) * clamped;
			}
		}

		bool ResolveWaiting(AudioVoiceEffects& description, std::string_view name) {
			bool resolved = false;
			for (AudioVoiceEffectRow& row : description.rows) {
				if (!row.definition && row.name == name) {
					row.definition = FindEffect(name);
					if (row.definition) {
						ResolveParameterValues(*row.definition, row.values);
						resolved = true;
					}
				}
			}
			return resolved;
		}

		AudioVoiceChain* MakeVoiceChain(const AudioVoiceEffects& description, u32 sampleRate, const ChannelLayout& layout, bool onOutput,
										bool keptWhileVirtual, const AudioVoiceChain* carry) {
			const u32 count = static_cast<u32>(description.rows.size());
			if (count == 0) {
				return nullptr;
			}
			const size_t bytes = sizeof(AudioVoiceChain) + (sizeof(IAudioEffect*) + sizeof(IAudioEffectDefinition*)) * count;
			AudioVoiceChain* chain = new (Allocate(bytes, MemTag::Audio, alignof(AudioVoiceChain))) AudioVoiceChain();
			chain->effects = reinterpret_cast<IAudioEffect**>(chain + 1);
			chain->definitions = reinterpret_cast<IAudioEffectDefinition**>(chain->effects + count);
			chain->count = count;
			chain->onOutput = onOutput;
			chain->keptWhileVirtual = keptWhileVirtual;

			AudioEffectContext context;
			context.sampleRate = sampleRate;
			context.blockFrames = AudioRenderer::BLOCK_FRAMES;
			context.channels = layout.count;
			for (u32 channel = 0; channel < layout.count; ++channel) {
				context.speakers[channel] = layout.positions[channel];
			}
			for (u32 index = 0; index < count; ++index) {
				const AudioVoiceEffectRow& row = description.rows[index];
				chain->definitions[index] = row.definition;
				chain->effects[index] = nullptr;
				if (!row.definition) {
					continue;
				}
				if (carry && index < carry->count && carry->definitions[index] == row.definition && carry->effects[index] && carry->onOutput == onOutput) {
					chain->effects[index] = carry->effects[index];
					continue;
				}
				context.parameters = {row.values.data(), row.values.size()};
				chain->effects[index] = row.definition->CreateEffect(context);
				if (!chain->effects[index]) {
					SLT_LOG_WARN("Audio", "the effect '{}' couldn't make an instance for a voice with {} channel(s) at {} Hz (sound passes through)",
								 row.name, layout.count, sampleRate);
				}
			}
			return chain;
		}

		void ReleaseVoiceChain(AudioVoiceChain* chain, const AudioVoiceChain* kept) {
			if (!chain) {
				return;
			}
			for (u32 index = 0; index < chain->count; ++index) {
				IAudioEffect* effect = chain->effects[index];
				if (!effect) {
					continue;
				}
				bool carried = false;
				for (u32 other = 0; kept && other < kept->count && !carried; ++other) {
					carried = kept->effects[other] == effect;
				}
				if (!carried) {
					effect->Release();
				}
			}
			chain->~AudioVoiceChain();
			Free(chain);
		}

		bool ChainRuns(const AudioVoiceChain& chain, const IAudioEffectDefinition* definition) {
			for (u32 index = 0; index < chain.count; ++index) {
				if (chain.definitions[index] == definition && chain.effects[index]) {
					return true;
				}
			}
			return false;
		}

	} // namespace AudioImpl

} // namespace Shalltear
