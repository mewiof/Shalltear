#pragma shalltear_kernel
#include "VisualEffects/ParticleSortPass.hlsli"

groupshared uint2 sltSortShared[SLT_PARTICLE_SORT_LOCAL];

[numthreads(SLT_PARTICLE_SORT_LOCAL_THREADS, 1, 1)]
void CSMain(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
	uint first = group.x * SLT_PARTICLE_SORT_LOCAL;
	for (uint loadAt = thread; loadAt < SLT_PARTICLE_SORT_LOCAL; loadAt += SLT_PARTICLE_SORT_LOCAL_THREADS) {
		sltSortShared[loadAt] = first + loadAt < sltSort.count ? SltParticleSortLoad(0, first + loadAt) : SLT_PARTICLE_SORT_PAD;
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint block = 2; block <= SLT_PARTICLE_SORT_LOCAL; block <<= 1) {
		for (uint span = block >> 1; span > 0; span >>= 1) {
			for (uint i = thread; i < SLT_PARTICLE_SORT_LOCAL; i += SLT_PARTICLE_SORT_LOCAL_THREADS) {
				uint partner = i ^ span;
				if (partner > i) {
					uint2 a = sltSortShared[i];
					uint2 b = sltSortShared[partner];
					bool forward = (i & block) == 0;
					if (forward ? SltParticleSortBefore(b, a) : SltParticleSortBefore(a, b)) {
						sltSortShared[i] = b;
						sltSortShared[partner] = a;
					}
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}
	for (uint storeAt = thread; storeAt < SLT_PARTICLE_SORT_LOCAL; storeAt += SLT_PARTICLE_SORT_LOCAL_THREADS) {
		if (first + storeAt < sltSort.count) {
			SltParticleSortWrite(0, first + storeAt, sltSortShared[storeAt]);
		}
	}
}
