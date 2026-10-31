#ifndef SHALLTEAR_PARTICLE_BOUNDS_HLSLI
#define SHALLTEAR_PARTICLE_BOUNDS_HLSLI

#include "ParticleSystem.hlsli"

static const float4 SLT_PARTICLE_NO_SPHERE = float4(0.0, 0.0, 0.0, -1.0);

groupshared uint sltBoundsLow[3 * SLT_PARTICLE_KERNEL_GROUP];
groupshared uint sltBoundsHigh[3 * SLT_PARTICLE_KERNEL_GROUP];

uint SltParticleBoundsKey(float value) {
	uint bits = asuint(value);
	return bits ^ ((bits >> 31) != 0 ? 0xFFFFFFFFu : 0x80000000u);
}

void SltParticleReduceBounds(uint thread, float4 sphere) {
	float3 low = sphere.xyz - sphere.w;
	float3 high = sphere.xyz + sphere.w;
	bool adds = sphere.w >= 0.0 && all(isfinite(low)) && all(isfinite(high));
	[unroll] for (uint c = 0; c < 3; ++c) {
		sltBoundsLow[c * SLT_PARTICLE_KERNEL_GROUP + thread] = adds ? SltParticleBoundsKey(low[c]) : SLT_PARTICLE_BOUNDS_EMPTY_LOW;
		sltBoundsHigh[c * SLT_PARTICLE_KERNEL_GROUP + thread] = adds ? SltParticleBoundsKey(high[c]) : 0u;
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint step = SLT_PARTICLE_KERNEL_GROUP / 2; step > 0; step >>= 1) {
		if (thread < step) {
			[unroll] for (uint c = 0; c < 3; ++c) {
				uint at = c * SLT_PARTICLE_KERNEL_GROUP + thread;
				sltBoundsLow[at] = min(sltBoundsLow[at], sltBoundsLow[at + step]);
				sltBoundsHigh[at] = max(sltBoundsHigh[at], sltBoundsHigh[at + step]);
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (thread == 0 && sltBoundsLow[0] != SLT_PARTICLE_BOUNDS_EMPTY_LOW) {
		uint at = SltParticleBoundsOffset(1 - SltParticleParity());
		[unroll] for (uint c = 0; c < 3; ++c) {
			sltParticleLists.InterlockedMin(at + c * 4, sltBoundsLow[c * SLT_PARTICLE_KERNEL_GROUP]);
			sltParticleLists.InterlockedMax(at + 12 + c * 4, sltBoundsHigh[c * SLT_PARTICLE_KERNEL_GROUP]);
		}
	}
}

#endif
