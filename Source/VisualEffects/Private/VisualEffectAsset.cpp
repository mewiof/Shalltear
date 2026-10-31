#include "Shalltear/VisualEffects/VisualEffectAsset.h"

#include "EffectAsset.h"
#include "EffectPass.h"
#include "ParticleKernels.h"
#include "ParticleSimulation.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <cstring>
#include <utility>

SLT_REGISTER_TYPE(Shalltear::VisualEffectAsset)

namespace Shalltear {

	using namespace VisualEffectsImpl;

	namespace {

		constexpr u32 COOKED_VERSION = 26;

		bool CookShader(const ShaderAsset& shader, CookWriter& out) {
			PakStringPool pool;
			Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
			Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
			CookWriter descriptorWriter(descriptor, pool);
			CookWriter payloadWriter(payload, pool);
			if (!shader.Cook(descriptorWriter, payloadWriter)) {
				return false;
			}
			out.Write(pool.Count());
			for (const str& text : pool.Texts()) {
				out.Write(static_cast<u32>(text.size()));
				out.WriteBytes(text.data(), text.size());
			}
			out.Write(static_cast<u32>(descriptor.size()));
			out.WriteBytes(descriptor.data(), descriptor.size());
			out.Write(static_cast<u32>(payload.size()));
			out.WriteBytes(payload.data(), payload.size());
			return true;
		}

		ShaderAsset* LoadShader(CookReader& in) {
			u32 count = 0;
			in.Read(count);
			Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
			Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				u32 size = 0;
				in.Read(size);
				const u8* text = in.ReadSpan(size);
				if (!text) {
					return nullptr;
				}
				offsets.push_back(static_cast<u32>(blob.size()));
				blob.insert(blob.end(), text, text + size);
			}
			offsets.push_back(static_cast<u32>(blob.size()));
			u32 descriptorSize = 0;
			in.Read(descriptorSize);
			const u8* descriptor = in.ReadSpan(descriptorSize);
			u32 payloadSize = 0;
			in.Read(payloadSize);
			const u8* payload = in.ReadSpan(payloadSize);
			if (in.Failed()) {
				return nullptr;
			}
			PakStringTable strings;
			strings.offsets = offsets.data();
			strings.blob = blob.data();
			strings.count = count;
			strings.blobSize = static_cast<u32>(blob.size());
			CookReader descriptorReader(descriptor, descriptorSize, strings);
			CookReader payloadReader(payload, payloadSize, strings);
			ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
			if (!shader->LoadCooked(descriptorReader, payloadReader)) {
				Delete(shader);
				return nullptr;
			}
			return shader;
		}

		void NoteKernels(const EffectDescription& description, CookWriter& descriptor) {
			if (description.systems.empty()) {
				return;
			}
			descriptor.NoteAssetRef(PARTICLE_RESET_KERNEL);
			descriptor.NoteAssetRef(PARTICLE_ARGUMENTS_KERNEL);
			bool sorts = false;
			bool strips = false;
			for (const EffectSystem& system : description.systems) {
				strips = strips || system.strips > 0;
				for (const EffectOutput& output : system.outputs) {
					sorts = sorts || output.sort != EffectSort::None;
				}
			}
			if (sorts) {
				descriptor.NoteAssetRef(PARTICLE_SORT_LOCAL_KERNEL);
				descriptor.NoteAssetRef(PARTICLE_SORT_MERGE_KERNEL);
			}
			if (strips) {
				descriptor.NoteAssetRef(PARTICLE_STRIPS_KERNEL);
			}
		}

	} // anonymous namespace

	namespace {

		Handle<TextureAsset> HoldTexture(const Guid& texture) {
			if (!texture.IsValid()) {
				return {};
			}
			Asset* loaded = AssetDatabase::LoadAssetByGuid(texture);
			if (loaded && loaded->GetAssetTypeId() == StringHash("TextureAsset")) {
				return Handle<TextureAsset>(static_cast<TextureAsset*>(loaded));
			}
			if (loaded) {
				SLT_LOG_WARN("VisualEffects", "an effect uses {} (not a texture)", texture);
				AssetDatabase::UnloadAsset(loaded);
			} else {
				SLT_LOG_WARN("VisualEffects", "an effect uses texture {} (nothing resolves it)", texture);
			}
			return {};
		}

		Handle<MeshAsset> HoldMesh(const Guid& mesh) {
			if (!mesh.IsValid()) {
				return {};
			}
			Asset* loaded = AssetDatabase::LoadAssetByGuid(mesh);
			if (loaded && loaded->GetAssetTypeId() == StringHash("MeshAsset")) {
				return Handle<MeshAsset>(static_cast<MeshAsset*>(loaded));
			}
			if (loaded) {
				SLT_LOG_WARN("VisualEffects", "an effect uses {} (not a mesh)", mesh);
				AssetDatabase::UnloadAsset(loaded);
			} else {
				SLT_LOG_WARN("VisualEffects", "an effect uses mesh {} (nothing resolves it)", mesh);
			}
			return {};
		}

		void HoldAssets(EffectAssetState& state) {
			u32 index = 0;
			state.kernelTextures.clear();
			state.meshes.clear();
			for (const EffectMesh& mesh : state.description.meshes) {
				state.meshes.push_back(HoldMesh(mesh.mesh));
			}
			for (const EffectSystem& system : state.description.systems) {
				for (const Vector<EffectKernelTexture>* kernel : {&system.initializeTextures, &system.updateTextures}) {
					for (const EffectKernelTexture& texture : *kernel) {
						state.kernelTextures.push_back(HoldTexture(texture.texture));
					}
				}
				for (const EffectOutput& output : system.outputs) {
					MaterialAsset* material = state.materials[index++];
					for (const EffectOutputTexture& texture : output.textures) {
						for (MaterialAsset::TextureParam& slot : material->textures) {
							if (slot.name == texture.slot && texture.texture.IsValid()) {
								slot.texture = texture.texture;
								slot.resolved = HoldTexture(texture.texture);
							}
						}
					}
				}
			}
			state.assetsHeld = true;
		}

		void ReleaseAssets(EffectAssetState& state) {
			if (!state.assetsHeld) {
				return;
			}
			for (MaterialAsset* material : state.materials) {
				for (MaterialAsset::TextureParam& slot : material->textures) {
					if (Asset* texture = slot.resolved.Get()) {
						AssetDatabase::UnloadAsset(texture);
					}
					slot.resolved = {};
				}
			}
			for (const Handle<TextureAsset>& held : state.kernelTextures) {
				if (Asset* texture = held.Get()) {
					AssetDatabase::UnloadAsset(texture);
				}
			}
			state.kernelTextures.clear();
			for (const Handle<MeshAsset>& held : state.meshes) {
				if (Asset* mesh = held.Get()) {
					AssetDatabase::UnloadAsset(mesh);
				}
			}
			state.meshes.clear();
			state.assetsHeld = false;
		}

		void Free(EffectAssetState& state) {
			ReleaseAssets(state);
			for (MaterialAsset* material : state.materials) {
				Delete(material);
			}
			state.materials.clear();
			for (ShaderAsset* shader : state.shaders) {
				Delete(shader);
			}
			state.shaders.clear();
			state.description = EffectDescription{};
			state.dependencies.clear();
		}

		bool Finish(EffectAssetState& state, str& error) {
			if (!EffectDescriptions::Validate(state.description, static_cast<u32>(state.shaders.size()), error) ||
				!EffectDescriptions::Bind(state.description, error)) {
				return false;
			}
			for (const EffectSystem& system : state.description.systems) {
				if (!system.cpu && (!state.shaders[system.initialize]->IsKernel() || !state.shaders[system.update]->IsKernel())) {
					error = "a system's kernel isn't a kernel";
					return false;
				}
				for (const EffectOutput& output : system.outputs) {
					ShaderAsset& shader = *state.shaders[output.shader];
					if (shader.IsKernel()) {
						error = "an output's shader is a kernel";
						return false;
					}
					if (output.sortKernel != EFFECT_NO_BINDING && !state.shaders[output.sortKernel]->IsKernel()) {
						error = "an output's sort kernel isn't a kernel";
						return false;
					}
					MaterialAsset* material = New<MaterialAsset>(MemTag::Asset);
					material->shader = Handle<ShaderAsset>(&shader);
					Vector<str> textureSlots{GetMemoryResource(MemTag::Asset)};
					shader.GetMaterialTextureSlots(textureSlots);
					material->SyncToLayout(shader.GetMaterialParamLayout(), {textureSlots.data(), textureSlots.size()},
										   {shader.memberDefaults.data(), shader.memberDefaults.size()});
					state.materials.push_back(material);
				}
			}
			return true;
		}

	} // anonymous namespace

	VisualEffectAsset::VisualEffectAsset() : _state(New<EffectAssetState>(MemTag::Asset)) {}

	VisualEffectAsset::~VisualEffectAsset() {
		Free(*_state);
		Delete(_state);
	}

	bool VisualEffectsImpl::EffectAssetAccess::Adopt(VisualEffectAsset& asset, EffectDescription description, Vector<ShaderAsset*>& shaders) {
		EffectAssetState& state = *asset._state;
		Free(state);
		++state.generation;
		state.description = std::move(description);
		state.shaders.assign(shaders.begin(), shaders.end());
		shaders.clear();
		str error;
		if (!Finish(state, error)) {
			SLT_LOG_ERROR("VisualEffects", "a compiled effect was rejected: {}", error);
			Free(state);
			return false;
		}
		return true;
	}

	void VisualEffectsImpl::EffectAssetAccess::Replace(VisualEffectAsset& live, VisualEffectAsset& compiled) {
		VisualEffectsImpl::ReleaseEffectsOf(live);
		if (RHI::Device* device = RenderSystem::GetDevice()) {
			device->WaitIdle();
		}
		const bool held = live._state->assetsHeld;
		ReleaseAssets(*live._state);
		std::swap(live._state, compiled._state);
		live._state->generation = compiled._state->generation + 1;
		if (held) {
			HoldAssets(*live._state);
		}
	}

	void VisualEffectAsset::OnLoad() { HoldAssets(*_state); }

	void VisualEffectAsset::OnUnload() { ReleaseAssets(*_state); }

	Handle<ShaderAsset> VisualEffectsImpl::EffectAssetAccess::Shader(const VisualEffectAsset& asset, u32 index) {
		const EffectAssetState& state = *asset._state;
		return index < state.shaders.size() ? Handle<ShaderAsset>(state.shaders[index]) : Handle<ShaderAsset>{};
	}

	Handle<MaterialAsset> VisualEffectsImpl::EffectAssetAccess::OutputMaterial(const VisualEffectAsset& asset, u32 system, u32 output) {
		const EffectAssetState& state = *asset._state;
		u32 index = 0;
		for (u32 s = 0; s < state.description.systems.size(); ++s) {
			const u32 outputs = static_cast<u32>(state.description.systems[s].outputs.size());
			if (s == system) {
				return output < outputs ? Handle<MaterialAsset>(state.materials[index + output]) : Handle<MaterialAsset>{};
			}
			index += outputs;
		}
		return {};
	}

	void VisualEffectsImpl::EffectAssetAccess::KernelTextures(const VisualEffectAsset& asset, u32 system, bool update, Vector<ComputeTexture>& out) {
		out.clear();
		const EffectAssetState& state = *asset._state;
		u32 index = 0;
		for (u32 s = 0; s < state.description.systems.size(); ++s) {
			const EffectSystem& held = state.description.systems[s];
			for (const Vector<EffectKernelTexture>* kernel : {&held.initializeTextures, &held.updateTextures}) {
				const bool wanted = s == system && (kernel == &held.updateTextures) == update;
				for (const EffectKernelTexture& texture : *kernel) {
					if (wanted) {
						out.push_back({texture.binding, texture.samplerBinding,
									   index < state.kernelTextures.size() ? state.kernelTextures[index] : Handle<TextureAsset>{}});
					}
					++index;
				}
			}
		}
	}

	void VisualEffectsImpl::EffectAssetAccess::Meshes(const VisualEffectAsset& asset, Vector<ParticleMesh>& out) {
		out.clear();
		const EffectAssetState& state = *asset._state;
		for (u32 i = 0; i < state.description.meshes.size(); ++i) {
			out.push_back({state.description.meshes[i].offset, i < state.meshes.size() ? state.meshes[i] : Handle<MeshAsset>{}});
		}
	}

	bool VisualEffectAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		const EffectAssetState& state = *_state;
		descriptor.Write(COOKED_VERSION);
		descriptor.Write(static_cast<u32>(state.shaders.size()));
		descriptor.Write(static_cast<u32>(state.description.systems.size()));
		EffectDescriptions::Cook(state.description, descriptor);
		NoteKernels(state.description, descriptor);
		for (const ShaderAsset* shader : state.shaders) {
			if (!CookShader(*shader, payload)) {
				return false;
			}
		}
		return true;
	}

	bool VisualEffectAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		EffectAssetState& state = *_state;
		Free(state);
		++state.generation;
		u32 version = 0;
		u32 shaderCount = 0;
		u32 systemCount = 0;
		descriptor.Read(version);
		descriptor.Read(shaderCount);
		descriptor.Read(systemCount);
		if (descriptor.Failed() || version != COOKED_VERSION) {
			return false;
		}
		EffectDescription description;
		if (!EffectDescriptions::LoadCooked(descriptor, description) || description.systems.size() != systemCount) {
			return false;
		}
		for (u32 i = 0; i < shaderCount; ++i) {
			ShaderAsset* shader = LoadShader(payload);
			if (!shader) {
				Free(state);
				return false;
			}
			state.shaders.push_back(shader);
		}
		state.description = std::move(description);
		str error;
		if (!Finish(state, error)) {
			SLT_LOG_ERROR("VisualEffects", "a cooked effect was rejected: {}", error);
			Free(state);
			return false;
		}
		return true;
	}

	bool VisualEffectAsset::CookImportState(CookWriter& importState) const {
		importState.Write(static_cast<u32>(_state->dependencies.size()));
		for (const VisualEffectsImpl::EffectDependency& dependency : _state->dependencies) {
			importState.WriteString(dependency.physicalPath);
			importState.Write(dependency.contentHash);
		}
		return true;
	}

	bool VisualEffectAsset::LoadImportState(CookReader& importState) {
		u32 count = 0;
		importState.Read(count);
		Vector<VisualEffectsImpl::EffectDependency> restored{GetMemoryResource(MemTag::Asset)};
		for (u32 i = 0; i < count && !importState.Failed(); ++i) {
			VisualEffectsImpl::EffectDependency dependency;
			dependency.physicalPath = str(importState.ReadString());
			importState.Read(dependency.contentHash);
			restored.push_back(std::move(dependency));
		}
		if (importState.Failed()) {
			return false;
		}
		_state->dependencies = std::move(restored);
		return true;
	}

} // namespace Shalltear
