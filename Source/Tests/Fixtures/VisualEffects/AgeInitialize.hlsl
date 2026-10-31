#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// M9's hand-written initialize kernel (VisualEffects.md §7, M9): what M11's
// compiler generates from an Initialize context. Attribute 0 is the age and 1
// the lifetime, the frame block's first value past its header
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
	SltParticleStore(1, index, SltParticleEffectFloat(SLT_PARTICLE_EFFECT_HEADER));
	SltParticleAppendAlive(index);
}
