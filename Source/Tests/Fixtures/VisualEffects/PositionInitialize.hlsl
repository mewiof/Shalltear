#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// M10's hand-written initialize kernel (VisualEffects.md §7, M10): AgeInitialize
// with a position, which is what an output draws. Attribute 0 is the age, 1 the
// lifetime (the frame block's first value past its header), and 2-4 the world
// position (the next three) - every spawn at the one point, which is all a
// draw needs
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
	float4 values = SltParticleEffectFloat4(SLT_PARTICLE_EFFECT_HEADER);
	SltParticleStore(1, index, values.x);
	SltParticleStore(2, index, values.y);
	SltParticleStore(3, index, values.z);
	SltParticleStore(4, index, values.w);
	SltParticleAppendAlive(index);
}
