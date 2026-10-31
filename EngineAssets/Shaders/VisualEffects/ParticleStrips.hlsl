#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

// 1 thread per strip
[[vk::binding(2, 0)]] RWByteAddressBuffer sltParticleParentLists;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (!SltParticleEnterUpdate(id)) {
		return;
	}
	uint strip = id.x;
	if (strip >= sltParticle.stripCount) {
		return;
	}
	uint at = SltParticleStripOffset(strip);
	uint least = sltParticleLists.Load(at + SLT_PARTICLE_STRIP_LEAST);
	uint most = sltParticleLists.Load(at + SLT_PARTICLE_STRIP_MOST);
	bool empty = least == SLT_PARTICLE_STRIP_EMPTY;
	uint first = sltParticleLists.Load(at + SLT_PARTICLE_STRIP_FIRST);
	uint count = empty ? 0u : most - least + 1u;
	sltParticleLists.Store(at + SLT_PARTICLE_STRIP_FIRST, empty ? 0u : (first + least) % SltParticlePerStrip());
	sltParticleLists.Store(at + SLT_PARTICLE_STRIP_COUNT, count);
	sltParticleLists.Store(at + SLT_PARTICLE_STRIP_BEGAN, count);
	sltParticleLists.Store(at + SLT_PARTICLE_STRIP_LEAST, SLT_PARTICLE_STRIP_EMPTY);
	sltParticleLists.Store(at + SLT_PARTICLE_STRIP_MOST, 0u);
	if (empty && sltParticle.parentCapacity != 0 && sltParticleLists.Load(at + SLT_PARTICLE_STRIP_HELD) != 0) {
		sltParticleLists.Store(at + SLT_PARTICLE_STRIP_HELD, 0u);
		uint slot;
		sltParticleParentLists.InterlockedAdd(SLT_PARTICLE_DEAD_COUNT, 1u, slot);
		sltParticleParentLists.Store(SLT_PARTICLE_LISTS + 2 * sltParticle.parentCapacity * 4 + slot * 4, strip);
	}
}
