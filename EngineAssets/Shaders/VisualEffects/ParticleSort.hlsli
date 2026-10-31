#ifndef SHALLTEAR_PARTICLE_SORT_HLSLI
#define SHALLTEAR_PARTICLE_SORT_HLSLI

#include "VisualEffects/ParticleSystem.hlsli"
#include "VisualEffects/ParticleOrder.hlsli"

[[vk::binding(2, 0)]] RWByteAddressBuffer sltParticleSortKeys;

#define SLT_PARTICLE_SORT_THREADS 256
static const uint SLT_PARTICLE_SORT_MAX_GROUPS = 32768;

uint SltParticleSortStride() {
	return max(min(sltParticle.sortCount, SLT_PARTICLE_SORT_THREADS * SLT_PARTICLE_SORT_MAX_GROUPS), SLT_PARTICLE_SORT_THREADS);
}

float3 SltParticleSortViewPosition() {
	return float3(sltParticle.sortViewX, sltParticle.sortViewY, sltParticle.sortViewZ);
}

float3 SltParticleSortViewForward() {
	return float3(sltParticle.sortForwardX, sltParticle.sortForwardY, sltParticle.sortForwardZ);
}

float SltParticleSortDistance(float3 world) {
	float3 toParticle = world - SltParticleSortViewPosition();
	return dot(toParticle, toParticle);
}

float SltParticleSortDepth(float3 world) {
	return dot(world - SltParticleSortViewPosition(), SltParticleSortViewForward());
}

void SltParticleSortStore(uint slot, uint index, float key) {
	sltParticleSortKeys.Store2(slot * 8, uint2(SltParticleSortableKey(key * sltParticle.sortSign), index));
}

void SltParticleSortPad(uint slot) {
	sltParticleSortKeys.Store2(slot * 8, SLT_PARTICLE_SORT_PAD);
}

#endif
