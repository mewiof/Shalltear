#include "Shalltear/PostProcessing/PostProcessVolume.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct VolumeList {
			Vector<PostProcessVolume*> volumes{GetMemoryResource(MemTag::Scene)};
		};

		VolumeList* _list = nullptr;

		VolumeList& List() {
			if (!_list) {
				_list = New<VolumeList>(MemTag::Scene);
			}
			return *_list;
		}

	} // anonymous namespace

	PostProcessVolume::~PostProcessVolume() {
		if (_list) {
			Vector<PostProcessVolume*>& volumes = _list->volumes;
			volumes.erase(std::remove(volumes.begin(), volumes.end(), this), volumes.end());
		}
	}

	void PostProcessVolume::OnEnable() {
		Vector<PostProcessVolume*>& volumes = List().volumes;
		if (std::find(volumes.begin(), volumes.end(), this) == volumes.end()) {
			volumes.push_back(this);
		}
	}

	void PostProcessVolume::OnDisable() {
		if (!_list) {
			return;
		}
		Vector<PostProcessVolume*>& volumes = _list->volumes;
		volumes.erase(std::remove(volumes.begin(), volumes.end(), this), volumes.end());
	}

	u32 PostProcessVolume::GetEnabledCount() {
		return _list ? static_cast<u32>(_list->volumes.size()) : 0;
	}

	PostProcessVolume* PostProcessVolume::GetEnabled(u32 index) {
		if (!_list || index >= _list->volumes.size()) {
			return nullptr;
		}
		return _list->volumes[index];
	}

	void ReleasePostProcessVolumeList() {
		if (_list) {
			Delete(_list);
			_list = nullptr;
		}
	}

} // namespace Shalltear
