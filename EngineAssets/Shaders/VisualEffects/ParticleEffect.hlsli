#ifndef SHALLTEAR_PARTICLE_EFFECT_HLSLI
#define SHALLTEAR_PARTICLE_EFFECT_HLSLI

// must match the package's C++ header layout

static const uint SLT_PARTICLE_EFFECT_HEADER = 96;

float SltParticleEffectFloat(uint offset) {
	return vk::RawBufferLoad<float>(SltParticleEffectAddress() + offset, 4);
}

uint SltParticleEffectWord(uint offset) {
	return vk::RawBufferLoad<uint>(SltParticleEffectAddress() + offset, 4);
}

float2 SltParticleEffectFloat2(uint offset) {
	return float2(SltParticleEffectFloat(offset), SltParticleEffectFloat(offset + 4));
}

float3 SltParticleEffectFloat3(uint offset) {
	return float3(SltParticleEffectFloat(offset), SltParticleEffectFloat(offset + 4), SltParticleEffectFloat(offset + 8));
}

float4 SltParticleEffectFloat4(uint offset) {
	return float4(SltParticleEffectFloat3(offset), SltParticleEffectFloat(offset + 12));
}

float4 SltParticleEffectTime() {
	return SltParticleEffectFloat4(0);
}

float SltParticleEffectSmoothDelta() {
	return SltParticleEffectFloat(16);
}

float4x4 SltParticleEffectLocalToWorld() {
	return float4x4(SltParticleEffectFloat4(32), SltParticleEffectFloat4(48), SltParticleEffectFloat4(64), SltParticleEffectFloat4(80));
}

float4 SltParticleEffectLocalToWorldRow(uint row) {
	return SltParticleEffectFloat4(32 + 16 * row);
}

float4 SltParticleEffectWorldToLocalRow(uint row) {
	float3 r0 = SltParticleEffectFloat4(32).xyz;
	float3 r1 = SltParticleEffectFloat4(48).xyz;
	float3 r2 = SltParticleEffectFloat4(64).xyz;
	float3 t = float3(SltParticleEffectFloat(44), SltParticleEffectFloat(60), SltParticleEffectFloat(76));
	float3 c0 = cross(r1, r2);
	float3 c1 = cross(r2, r0);
	float3 c2 = cross(r0, r1);
	float inverseDeterminant = 1.0 / dot(r0, c0);
	float3 inverseRow = (row == 0 ? float3(c0.x, c1.x, c2.x) : (row == 1 ? float3(c0.y, c1.y, c2.y) : float3(c0.z, c1.z, c2.z))) * inverseDeterminant;
	return float4(inverseRow, -dot(inverseRow, t));
}

float3 SltParticleEffectWorldToLocalVector(float3 v) {
	return float3(dot(SltParticleEffectWorldToLocalRow(0).xyz, v), dot(SltParticleEffectWorldToLocalRow(1).xyz, v), dot(SltParticleEffectWorldToLocalRow(2).xyz, v));
}

float3 SltParticleEffectWorldToLocalPoint(float3 p) {
	float4 at = float4(p, 1.0);
	return float3(dot(SltParticleEffectWorldToLocalRow(0), at), dot(SltParticleEffectWorldToLocalRow(1), at), dot(SltParticleEffectWorldToLocalRow(2), at));
}

float4 SltParticleCameraRow(uint record, uint matrix, uint row) {
	return SltParticleEffectFloat4(record + 64 * matrix + 16 * row);
}

float4 SltParticleCameraSize(uint record) {
	return SltParticleEffectFloat4(record + 256);
}

uint64_t SltParticleMeshAddress(uint record, uint at) {
	return uint64_t(SltParticleEffectWord(record + at)) | (uint64_t(SltParticleEffectWord(record + at + 4)) << 32);
}

uint SltParticleMeshCount(uint record, uint which) {
	return SltParticleEffectWord(record + 16 + 4 * which);
}

uint SltParticleMeshIndex(uint record, float index) {
	uint at = uint(max(index, 0.0));
	if (at >= SltParticleMeshCount(record, 1)) {
		return 0;
	}
	return vk::RawBufferLoad<uint>(SltParticleMeshAddress(record, 8) + uint64_t(at) * 4, 4);
}

float4 SltParticleMeshVertex(uint record, float vertex, uint channel) {
	uint at = uint(max(vertex, 0.0));
	if (at >= SltParticleMeshCount(record, 0)) {
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	uint64_t base = SltParticleMeshAddress(record, 0) + uint64_t(at) * 52;
	if (channel == 0) {
		return float4(vk::RawBufferLoad<float3>(base, 4), 0.0);
	}
	if (channel == 1) {
		return float4(vk::RawBufferLoad<float3>(base + 16, 4), 0.0);
	}
	if (channel == 2) {
		return vk::RawBufferLoad<float4>(base + 32, 4);
	}
	if (channel == 3) {
		uint packed = vk::RawBufferLoad<uint>(base + 48, 4);
		return float4(uint4(packed, packed >> 8, packed >> 16, packed >> 24) & 255) / 255.0;
	}
	return float4(vk::RawBufferLoad<float>(base + 12, 4), vk::RawBufferLoad<float>(base + 28, 4), 0.0, 0.0);
}

uint SltParticleEffectIdBase() {
	return SltParticleEffectWord(24);
}

uint SltParticleEffectSeed() {
	return SltParticleEffectWord(28);
}

uint SltParticleEffectBatch(uint spawn, uint batches, uint stride, out uint first) {
	uint count = SltParticleEffectWord(20);
	uint end = 0;
	first = 0;
	for (uint batch = 0; batch < count; ++batch) {
		uint at = batches + batch * stride;
		first = end;
		end += SltParticleEffectWord(at);
		if (spawn < end) {
			return at;
		}
	}
	return batches + (count > 0 ? (count - 1) * stride : 0);
}

uint SltParticleEffectBatch(uint spawn, uint batches, uint stride) {
	uint first;
	return SltParticleEffectBatch(spawn, batches, stride, first);
}

uint SltParticleHash(uint seed) {
	seed = (seed ^ 61u) ^ (seed >> 16);
	seed += seed << 3;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2du;
	return seed ^ (seed >> 15);
}

float SltParticleRandomKey(uint particleId) {
	return asfloat(SltParticleHash(particleId ^ SltParticleHash(SltParticleEffectSeed() ^ SltParticleHash(SltParticleFrameIndex() + 1u))));
}

float SltParticleConstantRandomKey(uint particleId) {
	return asfloat(SltParticleHash(particleId ^ SltParticleHash(SltParticleEffectSeed())));
}

float SltParticleEffectRandomKey() {
	return asfloat(SltParticleHash(SltParticleEffectSeed() ^ SltParticleHash(SltParticleFrameIndex() + 1u)) ^ 0x9e3779b9u);
}

float SltParticleEffectConstantRandomKey() {
	return asfloat(SltParticleHash(SltParticleEffectSeed()) ^ 0x9e3779b9u);
}

#endif
