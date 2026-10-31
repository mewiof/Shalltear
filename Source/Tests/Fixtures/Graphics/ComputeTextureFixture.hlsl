#pragma shalltear_kernel

// A KERNEL SAMPLING A TEXTURE ASSET (VisualEffects.md §9.13): the volume at
// binding 1, its sampler at binding 2, read at `uvw` on mip 0 and stored as
// four floats at the front of `values` - what FramePassTest reads back to see
// which texture a dispatch bound
[[vk::binding(0, 0)]] RWByteAddressBuffer values;
[[vk::binding(1, 0)]] Texture3D volume;
[[vk::binding(2, 0)]] SamplerState volumeSampler;

struct TexturePush {
	float3 uvw;
	float level;
};
[[vk::push_constant]] TexturePush push;

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	values.Store4(0, asuint(volume.SampleLevel(volumeSampler, push.uvw, push.level)));
}
