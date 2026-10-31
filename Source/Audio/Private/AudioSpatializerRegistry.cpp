#include "AudioSpatializerRegistry.h"

#include "AudioSystem.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			struct Entry {
				str name;
				IAudioSpatializer* spatializer = nullptr;
				const void* owner = nullptr;
			};

			Vector<Entry>* _entries = nullptr;

			Vector<Entry>& Entries() {
				if (!_entries) {
					_entries = New<Vector<Entry>>(MemTag::Audio, GetMemoryResource(MemTag::Audio));
				}
				return *_entries;
			}

		} // anonymous namespace

		IAudioSpatializer* FindSpatializer(std::string_view name) {
			if (!_entries || name.empty()) {
				return nullptr;
			}
			for (const Entry& entry : *_entries) {
				if (entry.name == name) {
					return entry.spatializer;
				}
			}
			return nullptr;
		}

		u32 GetRegisteredSpatializerCount() {
			return _entries ? static_cast<u32>(_entries->size()) : 0u;
		}

		void TakeSpatializersOwnedBy(const void* owner, Vector<IAudioSpatializer*>& out) {
			if (!_entries) {
				return;
			}
			for (const Entry& entry : *_entries) {
				if (entry.owner == owner) {
					out.push_back(entry.spatializer);
				}
			}
			std::erase_if(*_entries, [owner](const Entry& entry) { return entry.owner == owner; });
		}

		void ShutdownSpatializerRegistry() {
			if (_entries) {
				Delete(_entries);
				_entries = nullptr;
			}
		}

	} // namespace AudioImpl

	namespace AudioSpatializers {

		using namespace AudioImpl;

		bool Register(std::string_view name, IAudioSpatializer* spatializer, const void* owner) {
			if (name.empty() || !spatializer) {
				SLT_LOG_WARN("Audio", "a spatializer was registered with no name or no spatializer (ignored)");
				return false;
			}
			if (FindSpatializer(name)) {
				SLT_LOG_WARN("Audio", "a spatializer named '{}' is already registered. Ignoring the 2nd", name);
				return false;
			}
			Entries().push_back(Entry{str(name), spatializer, owner});
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			Vector<IAudioSpatializer*> gone{GetMemoryResource(MemTag::Audio)};
			TakeSpatializersOwnedBy(owner, gone);
			AudioSystem& system = GetAudioSystem();
			AudioDetachment detachment;
			for (IAudioSpatializer* spatializer : gone) {
				system.DetachSpatializer(spatializer, detachment);
			}
			system.CompleteDetachment(detachment);
			return static_cast<u32>(gone.size());
		}

	} // namespace AudioSpatializers

} // namespace Shalltear
