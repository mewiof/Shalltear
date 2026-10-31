#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// M9's hand-written update kernel: every particle ages by the frame's delta and
// dies when its age reaches its lifetime, which returns its index for reuse
[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (!SltParticleEnterUpdate(id) || id.x >= SltParticleAliveInCount()) {
		return;
	}
	uint index = SltParticleAliveIn(id.x);
	float age = SltParticleLoad(0, index) + SltParticleDeltaTime();
	if (age >= SltParticleLoad(1, index)) {
		SltParticleKill(index);
		return;
	}
	SltParticleStore(0, index, age);
	SltParticleAppendAlive(index);
}
