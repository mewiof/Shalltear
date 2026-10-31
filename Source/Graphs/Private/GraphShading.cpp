#include "GraphShading.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringHash.h"

namespace Shalltear {

	namespace {

		constexpr u32 MAX_TARGETS = 4;

		struct Entry {
			std::string_view documentType;
			GraphShadingTarget target;
			const void* owner = nullptr;
		};

		Entry _targets[MAX_TARGETS];
		u32 _count = 0;

	} // anonymous namespace

	namespace GraphShadingTargets {

		bool Register(std::string_view documentType, const GraphShadingTarget& target, const void* owner) {
			if (owner == nullptr) {
				SLT_LOG_ERROR("Graphs", "GraphShadingTargets::Register: the target for '{}' has no owner (not registered)", documentType);
				return false;
			}
			if (documentType.empty() || target.canShadeEffects == nullptr || target.generateFunctions == nullptr) {
				SLT_LOG_ERROR("Graphs", "GraphShadingTargets::Register: the target for '{}' is incomplete (not registered)", documentType);
				return false;
			}
			for (u32 index = 0; index < _count; ++index) {
				if (_targets[index].documentType == documentType) {
					SLT_LOG_ERROR("Graphs", "GraphShadingTargets::Register: '{}' documents already have a target (2nd ignored)", documentType);
					return false;
				}
			}
			if (_count == MAX_TARGETS) {
				SLT_LOG_ERROR("Graphs", "GraphShadingTargets::Register: the registry is full at {} targets ('{}' ignored)", MAX_TARGETS, documentType);
				return false;
			}
			_targets[_count++] = Entry{documentType, target, owner};
			return true;
		}

		u32 UnregisterOwnedBy(const void* owner) {
			if (owner == nullptr) {
				return 0;
			}
			u32 kept = 0;
			u32 removed = 0;
			for (u32 index = 0; index < _count; ++index) {
				if (_targets[index].owner == owner) {
					++removed;
					continue;
				}
				_targets[kept++] = _targets[index];
			}
			for (u32 index = kept; index < _count; ++index) {
				_targets[index] = Entry{};
			}
			_count = kept;
			return removed;
		}

		u64 Fingerprint() {
			u64 total = 0;
			for (u32 index = 0; index < _count; ++index) {
				total += StringHash(_targets[index].documentType).GetValue();
			}
			return total;
		}

		const GraphShadingTarget* Find(std::string_view documentType) {
			for (u32 index = 0; index < _count; ++index) {
				if (_targets[index].documentType == documentType) {
					return &_targets[index].target;
				}
			}
			return nullptr;
		}

	} // namespace GraphShadingTargets

} // namespace Shalltear
