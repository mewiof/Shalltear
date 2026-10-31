#include "AudioEditor.h"

#include "AudioImportOps.h"
#include "MiniaudioShared.h"

#include "Shalltear/Audio/AudioClip.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#define STB_VORBIS_HEADER_ONLY
#include "ThirdParty/stb_vorbis.c"

#include <cctype>
#include <format>

namespace Shalltear {

	namespace AudioEditor {

		namespace {

			using namespace AudioImpl;

			constexpr u32 DECODE_CHUNK_FRAMES = 4096;

			str Lowercase(const str& text) {
				str lowered = text;
				for (char& character : lowered) {
					character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
				}
				return lowered;
			}

			bool DecodeVorbis(const Buffer& file, DecodedAudio& out, str& error) {
				int code = 0;
				stb_vorbis* vorbis = stb_vorbis_open_memory(file.data, static_cast<int>(file.size), &code, nullptr);
				if (!vorbis) {
					error = std::format("the file isn't an Ogg Vorbis stream the decoder can read (stb_vorbis error {})", code);
					return false;
				}
				const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
				if (info.channels <= 0 || info.channels > static_cast<int>(MAX_AUDIO_CHANNELS) || info.sample_rate == 0) {
					stb_vorbis_close(vorbis);
					error = std::format("the stream has {} channels (1-8 allowed)", info.channels);
					return false;
				}
				const u32 channels = static_cast<u32>(info.channels);
				out.layout = MakeVorbisLayout(channels);
				out.sampleRate = info.sample_rate;
				if (const unsigned int length = stb_vorbis_stream_length_in_samples(vorbis); length != 0) {
					out.samples.reserve(static_cast<size_t>(length) * channels);
				}

				Vector<f32> chunk{GetMemoryResource(MemTag::Audio)};
				chunk.resize(static_cast<size_t>(DECODE_CHUNK_FRAMES) * channels);
				for (;;) {
					const int frames = stb_vorbis_get_samples_float_interleaved(vorbis, info.channels, chunk.data(), static_cast<int>(chunk.size()));
					if (frames <= 0) {
						break;
					}
					out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<size_t>(frames) * channels);
				}
				stb_vorbis_close(vorbis);
				return true;
			}

			enum class LayoutSource : u8 {
				Decoder,
				WaveMask,
				Flac,
			};

			bool DecodeMiniaudio(const Buffer& file, ma_encoding_format encoding, LayoutSource source, DecodedAudio& out, str& error) {
				ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
				config.encodingFormat = encoding;
				config.allocationCallbacks = MakeMiniaudioAllocationCallbacks();
				ma_decoder decoder;
				ma_result result = ma_decoder_init_memory(file.data, file.size, &config, &decoder);
				if (result != MA_SUCCESS) {
					error = std::format("the decoder rejected the file: {}", ma_result_description(result));
					return false;
				}

				ma_format format = ma_format_unknown;
				ma_uint32 channels = 0;
				ma_uint32 rate = 0;
				ma_channel map[MA_MAX_CHANNELS] = {};
				ma_decoder_get_data_format(&decoder, &format, &channels, &rate, map, MA_MAX_CHANNELS);
				if (channels == 0 || channels > MAX_AUDIO_CHANNELS || rate == 0) {
					ma_decoder_uninit(&decoder);
					error = std::format("the file has {} channels (1-8 allowed)", channels);
					return false;
				}
				out.sampleRate = rate;
				if (source == LayoutSource::Flac) {
					out.layout = MakeFlacLayout(channels);
				} else if (source != LayoutSource::WaveMask || !ReadWaveChannelLayout(file.data, file.size, channels, out.layout)) {
					out.layout = {};
					out.layout.count = channels;
					for (u32 channel = 0; channel < channels; ++channel) {
						out.layout.positions[channel] = ToSpeakerPosition(map[channel]);
					}
				}

				Vector<f32> chunk{GetMemoryResource(MemTag::Audio)};
				chunk.resize(static_cast<size_t>(DECODE_CHUNK_FRAMES) * channels);
				for (;;) {
					ma_uint64 read = 0;
					result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), DECODE_CHUNK_FRAMES, &read);
					if (read > 0) {
						out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<size_t>(read) * channels);
					}
					if (result != MA_SUCCESS || read < DECODE_CHUNK_FRAMES) {
						break;
					}
				}
				ma_decoder_uninit(&decoder);
				return true;
			}

			bool DecodeFile(const Path& sourcePath, DecodedAudio& decoded, str& error) {
				Buffer file = FileSystem::ReadFile(sourcePath, MemTag::Asset);
				if (!file) {
					error = "the file couldn't be read";
					return false;
				}
				const str extension = Lowercase(sourcePath.GetExtension());
				if (extension == ".ogg") {
					return DecodeVorbis(file, decoded, error);
				}
				if (extension == ".flac") {
					return DecodeMiniaudio(file, ma_encoding_format_flac, LayoutSource::Flac, decoded, error);
				}
				if (extension == ".mp3") {
					return DecodeMiniaudio(file, ma_encoding_format_mp3, LayoutSource::Decoder, decoded, error);
				}
				return DecodeMiniaudio(file, ma_encoding_format_wav, extension == ".wav" ? LayoutSource::WaveMask : LayoutSource::Decoder, decoded,
									   error);
			}

			class AudioClipImporter final : public IAssetImporter {
			public:
				StringHash GetAssetType() const override { return AudioClip::GetReflectType()->name; }

				u32 GetImporterVersion() const override { return 2; }

				Asset* Import(const Path& sourcePath) override { return ImportWithSettings(sourcePath, sourcePath, JSONValue{}); }

				Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override {
					AudioImportSettings importSettings;
					ReadImportSettings(settings, importSettings);

					DecodedAudio decoded;
					str error;
					const bool ok = DecodeFile(sourcePath, decoded, error);

					AudioClip* clip = nullptr;
					if (ok) {
						clip = New<AudioClip>(MemTag::Asset);
						if (!BuildClip(decoded, importSettings, *clip, error)) {
							Delete(clip);
							clip = nullptr;
						}
					}
					if (!clip) {
						SLT_LOG_ERROR("Audio", "'{}' wasn't imported: {}", virtualPath.ToString(), error);
					}
					return clip;
				}

				void SerializeSettings(const Path& sourcePath, JSONValue& out) const override {
					AudioImportSettings defaults;
					DecodedAudio decoded;
					str error;
					if (DecodeFile(sourcePath, decoded, error) && decoded.sampleRate > 0) {
						defaults.loadType = DefaultLoadType(static_cast<f64>(decoded.GetFrames()) / static_cast<f64>(decoded.sampleRate));
					}
					WriteImportSettings(defaults, out);
				}
			};

			AudioClipImporter _importer;

		} // anonymous namespace

		bool RegisterClipImporter(const void* owner) {
			AssetImporterRegistry::Register(AudioClip::GetReflectType()->name, &_importer, {".wav", ".aiff", ".aif", ".flac", ".mp3", ".ogg"}, owner);
			return true;
		}

	} // namespace AudioEditor

} // namespace Shalltear
