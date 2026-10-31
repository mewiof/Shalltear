#pragma once

#include "GraphShading.h"
#include "NodeShader.h"

namespace NodeShaderTesting {

	inline const int NODE_SHADER_RESTORED_TOKEN = 0;

	class NodeShaderSwitchedOff {
	public:
		NodeShaderSwitchedOff() {
			if (const Shalltear::GraphShadingTarget* target = Shalltear::GraphShadingTargets::Find(Shalltear::GRAPH_SHADER_DOCUMENT_TYPE)) {
				_saved = *target;
			}
			Shalltear::ShutdownNodeShader();
			Shalltear::GraphShadingTargets::UnregisterOwnedBy(&NODE_SHADER_RESTORED_TOKEN);
		}
		~NodeShaderSwitchedOff() {
			Shalltear::GraphShadingTargets::Register(Shalltear::GRAPH_SHADER_DOCUMENT_TYPE, _saved, &NODE_SHADER_RESTORED_TOKEN);
		}
		NodeShaderSwitchedOff(const NodeShaderSwitchedOff&) = delete;
		NodeShaderSwitchedOff& operator=(const NodeShaderSwitchedOff&) = delete;

		bool HadTarget() const { return _saved.generateFunctions != nullptr; }
	private:
		Shalltear::GraphShadingTarget _saved;
	};

} // namespace NodeShaderTesting
