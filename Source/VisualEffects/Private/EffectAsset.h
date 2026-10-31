#pragma once

#include "EffectDescription.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffectAsset.h"

namespace Shalltear {

	class MaterialAsset;
	class MeshAsset;
	class ShaderAsset;
	class TextureAsset;
	struct ComputeTexture;

	namespace VisualEffectsImpl {

		struct ParticleMesh;

		struct EffectDependency {
			str physicalPath;
			u64 contentHash = 0;
		};

		struct EffectAssetState {
			EffectDescription description;
			Vector<ShaderAsset*> shaders{GetMemoryResource(MemTag::Asset)};
			Vector<MaterialAsset*> materials{GetMemoryResource(MemTag::Asset)};
			u32 generation = 0;
			bool assetsHeld = false;
			Vector<Handle<TextureAsset>> kernelTextures{GetMemoryResource(MemTag::Asset)};
			Vector<Handle<MeshAsset>> meshes{GetMemoryResource(MemTag::Asset)};
			Vector<EffectDependency> dependencies{GetMemoryResource(MemTag::Asset)};
		};

		struct EffectAssetAccess {
			static bool Adopt(VisualEffectAsset& asset, EffectDescription description, Vector<ShaderAsset*>& shaders);
			static void Replace(VisualEffectAsset& live, VisualEffectAsset& compiled);

			static void SetDependencies(VisualEffectAsset& asset, Vector<EffectDependency> dependencies) {
				asset._state->dependencies = std::move(dependencies);
			}
			static const Vector<EffectDependency>& Dependencies(const VisualEffectAsset& asset) { return asset._state->dependencies; }

			static const EffectDescription& Description(const VisualEffectAsset& asset) { return asset._state->description; }
			static u32 Generation(const VisualEffectAsset& asset) { return asset._state->generation; }
			static Handle<ShaderAsset> Shader(const VisualEffectAsset& asset, u32 index);
			static Handle<MaterialAsset> OutputMaterial(const VisualEffectAsset& asset, u32 system, u32 output);
			static void KernelTextures(const VisualEffectAsset& asset, u32 system, bool update, Vector<ComputeTexture>& out);
			static void Meshes(const VisualEffectAsset& asset, Vector<ParticleMesh>& out);
		};

	} // namespace VisualEffectsImpl

} // namespace Shalltear
