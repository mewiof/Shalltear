#include "Shalltear/Audio/AudioConcurrency.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include "AudioVoiceManager.h"

namespace Shalltear {

	AudioConcurrency::~AudioConcurrency() = default;

	bool AudioConcurrency::Import(const Path& sourcePath) {
		return TextSerializer::Deserialize(this, GetReflectType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool AudioConcurrency::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetReflectType()));
	}

	namespace {

		class AudioConcurrencyImporter final : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return AudioConcurrency::GetReflectType()->name; }

			Asset* Import(const Path& sourcePath) override {
				auto* concurrency = New<AudioConcurrency>(MemTag::Asset);
				if (!concurrency->Import(sourcePath)) {
					Delete(concurrency);
					return nullptr;
				}
				return concurrency;
			}

			bool CanCreateAsset() const override { return true; }
			std::string_view GetCreateMenuLabel() const override { return "Audio Concurrency"; }
			std::string_view GetCreateDefaultFileName() const override { return "New Audio Concurrency.concurrency"; }
			Asset* CreateAsset() override { return New<AudioConcurrency>(MemTag::Asset); }
		};

		AudioConcurrencyImporter _importer;

	} // anonymous namespace

	namespace AudioImpl {

		void RegisterConcurrencyImporter(const void* owner) {
			AssetImporterRegistry::Register(AudioConcurrency::GetReflectType()->name, &_importer, {".concurrency"}, owner);
		}

	} // namespace AudioImpl

} // namespace Shalltear
