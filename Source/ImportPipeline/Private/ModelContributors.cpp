#include "ModelContributorsInternal.h"

#include "ModelImport.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Log.h"

namespace Shalltear {

	str ModelContribution::MakeProductKey(std::string_view kind, std::string_view name) const {
		if (!keyLedger) {
			SLT_LOG_ERROR("Graphics", "ModelContribution::MakeProductKey: this contribution has no key ledger ('{}' not minted)", name);
			return str();
		}
		return ProductKey(kind, name, *keyLedger);
	}

	bool ModelContribution::AddRootComponent(StringHash componentType) const {
		if (!rootComponents) {
			SLT_LOG_ERROR("Graphics", "ModelContribution::AddRootComponent: this contribution has no component ledger (dropped)");
			return false;
		}
		if (componentType == StringHash()) {
			SLT_LOG_ERROR("Graphics", "ModelContribution::AddRootComponent: an empty type name isn't a component");
			return false;
		}
		for (const StringHash& already : *rootComponents) {
			if (already == componentType) {
				return true;
			}
		}
		rootComponents->push_back(componentType);
		return true;
	}

	ModelParseResult ParseReferencedModel(const Guid& modelGuid) {
		ModelParseResult failed;
		failed.success = false;
		if (!modelGuid.IsValid()) {
			failed.error = str("no model is named");
			return failed;
		}

		const Path modelVirtualPath = AssetDatabase::LookupPath(modelGuid);
		if (modelVirtualPath.ToString().empty()) {
			failed.error = str("the database knows no asset ") + modelGuid.ToString();
			return failed;
		}

		return ParseModel(FileSystem::Resolve(modelVirtualPath));
	}

	namespace {

		constexpr u32 MAX_CONTRIBUTORS = 8;
		ModelContributorDesc _contributors[MAX_CONTRIBUTORS];
		u32 _count = 0;

	} // anonymous namespace

	namespace ModelContributors {

		bool Register(const ModelContributorDesc& desc) {
			if (desc.id == StringHash{}) {
				SLT_LOG_ERROR("Graphics", "ModelContributors::Register: a contributor needs an id (not registered)");
				return false;
			}
			if (desc.contribute == nullptr) {
				SLT_LOG_ERROR("Graphics", "ModelContributors::Register: contributor {} has no function (not registered)", desc.id.GetValue());
				return false;
			}
			if (desc.owner == nullptr) {
				SLT_LOG_ERROR("Graphics", "ModelContributors::Register: contributor {} has no owner (not registered)", desc.id.GetValue());
				return false;
			}
			for (u32 i = 0; i < _count; ++i) {
				if (_contributors[i].id == desc.id) {
					SLT_LOG_ERROR("Graphics", "ModelContributors::Register: contributor {} is already registered (2nd ignored)", desc.id.GetValue());
					return false;
				}
			}
			if (_count == MAX_CONTRIBUTORS) {
				SLT_LOG_ERROR("Graphics", "ModelContributors::Register: the registry is full at {} contributors ({} ignored)", MAX_CONTRIBUTORS, desc.id.GetValue());
				return false;
			}

			_contributors[_count++] = desc;
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (owner == nullptr) {
				return 0;
			}
			u32 kept = 0;
			u32 removed = 0;
			for (u32 i = 0; i < _count; ++i) {
				if (_contributors[i].owner == owner) {
					++removed;
					continue;
				}
				_contributors[kept++] = _contributors[i];
			}
			for (u32 i = kept; i < _count; ++i) {
				_contributors[i] = ModelContributorDesc{};
			}
			_count = kept;
			return removed;
		}

		u64 Fingerprint() {
			u64 total = 0;
			for (u32 i = 0; i < _count; ++i) {
				u64 hash = 0xcbf29ce484222325ull;
				const u64 pair = _contributors[i].id.GetValue() ^ (static_cast<u64>(_contributors[i].version) << 32);
				for (u32 byte = 0; byte < 8; ++byte) {
					hash ^= (pair >> (byte * 8)) & 0xFFull;
					hash *= 0x00000100000001b3ull;
				}
				total += hash;
			}
			return total;
		}

		bool AnyWantsTracks() {
			for (u32 i = 0; i < _count; ++i) {
				if (_contributors[i].wantsTracks) {
					return true;
				}
			}
			return false;
		}

		void Invoke(const ModelContribution& prototype) {
			const u32 count = _count;
			for (u32 i = 0; i < count && i < _count; ++i) {
				const ModelContributorDesc desc = _contributors[i];
				ModelContribution contribution = prototype;
				contribution.user = desc.user;
				desc.contribute(contribution);
			}
		}

	} // namespace ModelContributors

} // namespace Shalltear
