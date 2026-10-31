#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// M11c's hand-written initialize kernel: AgeInitialize, reading the position
// its spawn batch carries - what the effect compiler emits for a Set Position
// from Source block (VisualEffects.md §9.3). Attribute 0 is the age, 1 the
// lifetime (the frame block's first property), 2-4 the source position. The
// batches begin past the one property, sixteen bytes apiece: a count, then the
// position's three floats
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (!SltParticleEnterUpdate(id) || id.x >= SltParticleSpawnCount()) {
		return;
	}
	uint index;
	if (!SltParticlePopDead(index)) {
		return;
	}
	uint batch = SltParticleEffectBatch(id.x, 112u, 16u);
	float3 position = SltParticleEffectFloat3(batch + 4u);
	SltParticleStore(0, index, 0.0);
	SltParticleStore(1, index, SltParticleEffectFloat(SLT_PARTICLE_EFFECT_HEADER));
	SltParticleStore(2, index, position.x);
	SltParticleStore(3, index, position.y);
	SltParticleStore(4, index, position.z);
	SltParticleAppendAlive(index);
}
