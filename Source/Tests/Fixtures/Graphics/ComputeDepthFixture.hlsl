#pragma shalltear_kernel

// A KERNEL READING A CAMERA'S KEPT DEPTH (VisualEffects.md §9.14): the image at
// binding 1, loaded at `pixel` and stored at the front of `values` - what
// FramePassTest reads back to see which depth a dispatch bound. A frame's image
// is read by Load, so it has no sampler
[[vk::binding(0, 0)]] RWByteAddressBuffer values;
[[vk::binding(1, 0)]] Texture2D<float> depth;

struct DepthPush {
	uint2 pixel;
};
[[vk::push_constant]] DepthPush push;

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	values.Store(0, asuint(depth.Load(int3(push.pixel, 0))));
}
