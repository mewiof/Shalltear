#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

[[vk::binding(2, 0)]] RWByteAddressBuffer sltParticleArguments;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (sltParticle.batchFresh != 0 && id.x == 0 && id.y == 0) {
		sltParticleArguments.Store3(SltParticleBatchDispatchOffset(), uint3(0, 1, 1));
	}
	if ((SltParticleEnter(sltParticle.firstSlot + id.y) & SLT_PARTICLE_FRESH) == 0) {
		return;
	}
	uint i = id.x;
	if (i < sltParticle.capacity) {
		sltParticleLists.Store(SltParticleDeadListOffset() + i * 4, sltParticle.capacity - 1 - i);
	}
	if (i < sltParticle.stripCount) {
		uint at = SltParticleStripOffset(i);
		sltParticleLists.Store4(at, uint4(0u, 0u, 0u, SLT_PARTICLE_STRIP_EMPTY));
		sltParticleLists.Store2(at + SLT_PARTICLE_STRIP_MOST, uint2(0u, 0u));
	}
	if (i == 0) {
		uint arguments = sltInstance.arguments;
		sltParticleLists.Store4(sltInstance.lists + SLT_PARTICLE_ALIVE_COUNT, uint4(0, 0, sltParticle.capacity, 0));
		sltParticleArguments.Store3(arguments, uint3(0, 1, 1));
		for (uint output = 0; output < sltParticle.outputCount; ++output) {
			sltParticleArguments.Store4(arguments + SLT_PARTICLE_DRAW_ARGUMENTS + output * 16, uint4(0, 0, 0, 0));
		}
		if (sltParticle.events != 0) {
			sltParticleLists.Store2(sltInstance.lists + SltParticleEventListOffset(sltParticle.capacity, sltParticle.stripCount), uint2(0u, 0u));
			sltParticleArguments.Store3(arguments + SltParticleEventDispatchOffset(sltParticle.outputCount), uint3(0, 1, 1));
		}
		SltParticleEmptyBounds(0);
		SltParticleEmptyBounds(1);
		SltParticleEmptyBoundsAt(SltParticleFinishedBoundsOffset());
	}
}
