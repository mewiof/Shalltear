#pragma once

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <string_view>

namespace Shalltear::SceneRequests {

	inline constexpr u32 SET = 2;

	struct DeclaredName {
		std::string_view name;
		PassRequests request = PassRequests::None;
	};

	inline constexpr DeclaredName Names[] = {
		{"sltSceneColor", PassRequests::SceneColor}, {"sltSceneDepth", PassRequests::SceneDepth}, {"sltSceneNormals", PassRequests::SceneNormals}};

	inline PassRequests RequestNamed(std::string_view name) {
		for (const DeclaredName& declared : Names) {
			if (declared.name == name) {
				return declared.request;
			}
		}
		return PassRequests::None;
	}

	inline constexpr std::string_view ViewRecord = "sltViewRecord";

	inline bool IsViewRecord(std::string_view name) {
		constexpr std::string_view Prefix = "type.";
		return name == ViewRecord || (name.starts_with(Prefix) && name.substr(Prefix.size()) == ViewRecord);
	}

	inline bool DeclaresViewSet(const ShaderAsset& shader) {
		for (const ShaderResourceInfo& resource : shader.resources) {
			if (resource.set == 0) {
				return true;
			}
		}
		return false;
	}

	inline PassRequests DeclaredBy(const ShaderAsset& shader) {
		PassRequests requests = PassRequests::None;
		for (const ShaderResourceInfo& resource : shader.resources) {
			if (resource.set == SET) {
				requests |= RequestNamed(resource.name);
			}
		}
		return requests;
	}

	// bindings sorted by set in MergeReflections
	inline bool DeclaresSceneSet(const ShaderAsset& shader) {
		for (auto binding = shader.bindings.rbegin(); binding != shader.bindings.rend() && binding->set >= SET; ++binding) {
			if (binding->set == SET) {
				return true;
			}
		}
		return false;
	}

	inline bool MayReadSceneOnMesh(const ShaderAsset& shader) {
		return DeclaresSceneSet(shader) && DeclaresViewSet(shader) && DeclaredBy(shader) != PassRequests::None;
	}

	u32 CountLiveMeshReaders();

	inline bool ReadsOnlyScene(const ShaderAsset& shader) {
		for (const ShaderResourceInfo& resource : shader.resources) {
			if (resource.set == SET && resource.type != RHI::DescriptorType::Sampler && RequestNamed(resource.name) == PassRequests::None) {
				return false;
			}
		}
		return true;
	}

} // namespace Shalltear::SceneRequests
