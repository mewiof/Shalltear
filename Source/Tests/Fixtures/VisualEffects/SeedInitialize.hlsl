#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// M13a's hand-written initialize kernel (VisualEffects.md §3.7): what the
// instance hands every kernel to draw by. Attributes 0 and 1 are AgeUpdate's
// age and lifetime; 2 holds the effect's seed as its bits, and 3 the spawn's
// id - the frame's first spawn's, plus its place among the frame's spawns
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (!SltParticleEnterUpdate(id) || id.x >= SltParticleSpawnCount()) {
		return;
	}
	uint index;
	if (!SltParticlePopDead(index)) {
		return;
	}
	SltParticleStore(0, index, 0.0);
	SltParticleStore(1, index, 100.0);
	SltParticleStore(2, index, asfloat(SltParticleEffectSeed()));
	SltParticleStore(3, index, float(SltParticleEffectIdBase() + id.x));
	SltParticleAppendAlive(index);
}
