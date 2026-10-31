#pragma shalltear_kernel

// A KERNEL READING A MESH BY ADDRESS (VisualEffects.md §9.15): for each of the
// first `count` indices, the index itself and the x of the position it names -
// the first float of a 52-byte MeshVertex - stored as two words per index:
// what FramePassTest reads back to see that a mesh's indices reach a kernel
// through MeshAsset::indexBufferAddress
[[vk::binding(0, 0)]] RWByteAddressBuffer values;

struct MeshPush {
	uint64_t vertices;
	uint64_t indices;
	uint count;
	uint padding;
};
[[vk::push_constant]] MeshPush push;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	if (id.x >= push.count) {
		return;
	}
	const uint index = vk::RawBufferLoad<uint>(push.indices + uint64_t(id.x) * 4, 4);
	const float x = vk::RawBufferLoad<float>(push.vertices + uint64_t(index) * 52, 4);
	values.Store2(id.x * 8, uint2(index, asuint(x)));
}
