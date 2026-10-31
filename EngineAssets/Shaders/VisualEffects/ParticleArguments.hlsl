#pragma shalltear_kernel
#include "VisualEffects/ParticleSystem.hlsli"

static const uint SLT_PARTICLE_DRAW_PER_PARTICLE = 0;
static const uint SLT_PARTICLE_DRAW_INSTANCED = 1;
static const uint SLT_PARTICLE_DRAW_SEGMENTS = 2;
[[vk::binding(2, 0)]] RWByteAddressBuffer sltParticleArguments;

groupshared uint sltGroups[SLT_PARTICLE_KERNEL_GROUP];

[numthreads(SLT_PARTICLE_KERNEL_GROUP, 1, 1)]
void CSMain(uint thread : SV_GroupIndex) {
	uint width = max(sltParticle.updateGroupSize, 1u);
	uint fullest = 0;
	for (uint slot = thread; slot < sltParticle.slots; slot += SLT_PARTICLE_KERNEL_GROUP) {
		uint flags = SltParticleEnter(sltParticle.firstSlot + slot);
		if ((flags & SLT_PARTICLE_CLAIMED) == 0) {
			continue;
		}
		uint read = sltInstance.parity;
		if ((flags & SLT_PARTICLE_ADVANCES) != 0) {
			uint written = 1 - read;
			uint alive = sltParticleLists.Load(sltInstance.lists + SLT_PARTICLE_ALIVE_COUNT + written * 4);
			uint arguments = sltInstance.arguments;
			sltParticleArguments.Store3(arguments, uint3((alive + width - 1) / width, 1, 1));
			for (uint output = 0; output < sltParticle.outputCount; ++output) {
				uint vertices = SltParticleEffectWord(sltInstance.drawTable + output * 8);
				uint mode = SltParticleEffectWord(sltInstance.drawTable + output * 8 + 4);
				uint4 draw = uint4(alive * vertices, 1, 0, 0);
				if (mode == SLT_PARTICLE_DRAW_INSTANCED) {
					draw = uint4(vertices, alive, 0, 0);
				} else if (mode == SLT_PARTICLE_DRAW_SEGMENTS) {
					draw = uint4(alive > 0 ? vertices : 0u, 1, 0, 0);
				}
				sltParticleArguments.Store4(arguments + SLT_PARTICLE_DRAW_ARGUMENTS + output * 16, draw);
			}
			sltParticleLists.Store(sltInstance.lists + SLT_PARTICLE_ALIVE_COUNT + read * 4, 0u);
			uint box = SltParticleBoundsOffset(written);
			uint finished = SltParticleFinishedBoundsOffset();
			sltParticleLists.Store3(finished, sltParticleLists.Load3(box));
			sltParticleLists.Store3(finished + 12, sltParticleLists.Load3(box + 12));
			SltParticleEmptyBounds(read);
			if (sltParticle.events != 0) {
				uint list = sltInstance.lists + SltParticleEventListOffset(sltParticle.capacity, sltParticle.stripCount);
				uint spawned = SltParticleEventCount();
				sltParticleLists.Store2(list, uint2(0u, sltParticleLists.Load(list + SLT_PARTICLE_EVENT_ID_BASE) + spawned));
				sltParticleArguments.Store(arguments + SltParticleEventDispatchOffset(sltParticle.outputCount), 0u);
			}
			read = written;
		}
		uint next = sltParticleLists.Load(sltInstance.lists + SLT_PARTICLE_ALIVE_COUNT + read * 4);
		fullest = max(fullest, (next + width - 1) / width);
	}
	sltGroups[thread] = fullest;
	GroupMemoryBarrierWithGroupSync();
	for (uint step = SLT_PARTICLE_KERNEL_GROUP / 2; step > 0; step >>= 1) {
		if (thread < step) {
			sltGroups[thread] = max(sltGroups[thread], sltGroups[thread + step]);
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0) {
		sltParticleArguments.Store3(SltParticleBatchDispatchOffset(), uint3(sltGroups[0], sltParticle.slots, 1));
	}
}
