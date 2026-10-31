#ifndef SHALLTEAR_PARTICLE_ORDER_HLSLI
#define SHALLTEAR_PARTICLE_ORDER_HLSLI

static const uint2 SLT_PARTICLE_SORT_PAD = uint2(0u, 0xFFFFFFFFu);

uint SltParticleSortableKey(float key) {
	uint bits = asuint(key);
	return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
}

bool SltParticleSortBefore(uint2 a, uint2 b) {
	return a.x != b.x ? a.x > b.x : a.y < b.y;
}

#endif
