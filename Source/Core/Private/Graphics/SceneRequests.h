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

	inline constexpr DeclaredName NAMES[] = {
		{"sltSceneColor", PassRequests::SceneColor}, {"sltSceneDepth", PassRequests::SceneDepth}, {"sltSceneNormals", PassRequests::SceneNormals}};

	inline PassRequests RequestNamed(std::string_view name) {
		for (const DeclaredName& declared : NAMES) {
			if (declared.name == name) {
				return declared.request;
			}
		}
		return PassRequests::None;
	}

	inline constexpr std::string_view VIEW_RECORD = "sltViewRecord";

	inline bool IsViewRecord(std::string_view name) {
		constexpr std::string_view PREFIX = "type.";
		return name == VIEW_RECORD || (name.starts_with(PREFIX) && name.substr(PREFIX.size()) == VIEW_RECORD);
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

} // namespace Shalltear::SceneRequests
