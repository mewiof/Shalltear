#pragma shalltear_kernel
#include "VisualEffects/ParticleSortPass.hlsli"

[numthreads(SLT_PARTICLE_SORT_MERGE_THREADS, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	uint stride = max(min(sltSort.count, SLT_PARTICLE_SORT_MERGE_THREADS * SLT_PARTICLE_SORT_MERGE_GROUPS), SLT_PARTICLE_SORT_MERGE_THREADS);
	for (uint at = id.x; at < sltSort.count; at += stride) {
		uint size = sltSort.size;
		uint pair = at & ~(2 * size - 1);
		bool firstRun = (at & size) == 0;
		uint other = firstRun ? pair + size : pair;
		uint2 entry = SltParticleSortLoad(sltSort.source, at);
		uint low = 0;
		uint high = size;
		while (low < high) {
			uint middle = (low + high) >> 1;
			uint2 probe = SltParticleSortLoad(sltSort.source, other + middle);
			bool before = firstRun ? SltParticleSortBefore(probe, entry) : !SltParticleSortBefore(entry, probe);
			if (before) {
				low = middle + 1;
			} else {
				high = middle;
			}
		}
		SltParticleSortWrite(1 - sltSort.source, pair + (at & (size - 1)) + low, entry);
	}
}
