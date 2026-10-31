#include "Shalltear/Editor/EditorSelectionOps.h"

namespace Shalltear::EditorSelectionOps {

	bool ResolveRange(std::span<const u64> ordered, u64 anchor, u64 target, u32& first, u32& last) {
		constexpr u32 MISSING = ~0u;
		u32 anchorIndex = MISSING;
		u32 targetIndex = MISSING;
		for (u32 i = 0; i < ordered.size(); ++i) {
			if (ordered[i] == anchor && anchorIndex == MISSING) {
				anchorIndex = i;
			}
			if (ordered[i] == target && targetIndex == MISSING) {
				targetIndex = i;
			}
		}
		if (anchorIndex == MISSING || targetIndex == MISSING) {
			return false;
		}
		first = anchorIndex < targetIndex ? anchorIndex : targetIndex;
		last = anchorIndex < targetIndex ? targetIndex : anchorIndex;
		return true;
	}

	void ResolveTargets(std::span<const u64> selection, u64 actedOn, Vector<u64>& out) {
		for (u64 id : selection) {
			if (id == actedOn) {
				out.insert(out.end(), selection.begin(), selection.end());
				return;
			}
		}
		if (actedOn != 0) {
			out.push_back(actedOn);
		}
	}

} // namespace Shalltear::EditorSelectionOps
