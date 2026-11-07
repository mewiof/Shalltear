#include "Shalltear/PostProcessing/LensFlare.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct FlareList {
			Vector<LensFlare*> flares{GetMemoryResource(MemTag::Scene)};
		};

		FlareList* _list = nullptr;

		FlareList& List() {
			if (!_list) {
				_list = New<FlareList>(MemTag::Scene);
			}
			return *_list;
		}

		void Remove(LensFlare* flare) {
			if (_list) {
				Vector<LensFlare*>& flares = _list->flares;
				flares.erase(std::remove(flares.begin(), flares.end(), flare), flares.end());
			}
		}

	} // anonymous namespace

	LensFlareAsset::~LensFlareAsset() = default;

	LensFlare::~LensFlare() {
		Remove(this);
	}

	void LensFlare::OnEnable() {
		Vector<LensFlare*>& flares = List().flares;
		if (std::find(flares.begin(), flares.end(), this) == flares.end()) {
			flares.push_back(this);
		}
	}

	void LensFlare::OnDisable() {
		Remove(this);
	}

	u32 LensFlare::GetEnabledCount() {
		return _list ? static_cast<u32>(_list->flares.size()) : 0;
	}

	LensFlare* LensFlare::GetEnabled(u32 index) {
		if (!_list || index >= _list->flares.size()) {
			return nullptr;
		}
		return _list->flares[index];
	}

	void RegisterLensFlareAssetImporter(const void* owner) {
		AssetImporterRegistry::RegisterDeclared(LensFlareAsset::GetReflectType(), owner);
	}

	void ReleaseLensFlareList() {
		if (_list) {
			Delete(_list);
			_list = nullptr;
		}
	}

} // namespace Shalltear
