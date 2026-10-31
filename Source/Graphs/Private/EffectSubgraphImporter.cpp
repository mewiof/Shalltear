#include "EffectSubgraphImporter.h"

#include "EffectGraphEditOps.h"
#include "GraphEffect.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <cstring>

SLT_REGISTER_TYPE(Shalltear::VisualEffectSubgraphOperator)
SLT_REGISTER_TYPE(Shalltear::VisualEffectSubgraphBlock)

namespace Shalltear {

	VisualEffectSubgraphOperator::~VisualEffectSubgraphOperator() = default;
	VisualEffectSubgraphBlock::~VisualEffectSubgraphBlock() = default;

	StringHash EffectSubgraphImporter::GetAssetType() const {
		return _kind == GraphSubgraphKind::EffectBlock ? VisualEffectSubgraphBlock::GetReflectType()->name
													   : VisualEffectSubgraphOperator::GetReflectType()->name;
	}

	Asset* EffectSubgraphImporter::Import(const Path& sourcePath) {
		const str text = FileSystem::ReadTextFile(sourcePath, MemTag::Editor);
		GraphDocument document;
		str error;
		if (text.empty()) {
			SLT_LOG_ERROR("VisualEffects", "'{}' is empty or unreadable", sourcePath.ToString());
			return nullptr;
		}
		if (!GraphDocumentOps::Read(text, document, error)) {
			SLT_LOG_ERROR("VisualEffects", "'{}' isn't a graph document: {}", sourcePath.ToString(), error);
			return nullptr;
		}
		const std::string_view wanted = GraphSubgraphs::DocumentType(_kind);
		if (document.type != wanted) {
			SLT_LOG_ERROR("VisualEffects", "'{}' is a {} document (not a {})", sourcePath.ToString(), document.type, wanted);
			return nullptr;
		}
		if (_kind == GraphSubgraphKind::EffectBlock) {
			return New<VisualEffectSubgraphBlock>(MemTag::Asset);
		}
		return New<VisualEffectSubgraphOperator>(MemTag::Asset);
	}

	std::string_view EffectSubgraphImporter::GetCreateMenuLabel() const {
		return _kind == GraphSubgraphKind::EffectBlock ? "Visual Effects/Effect Block" : "Visual Effects/Effect Operator";
	}

	std::string_view EffectSubgraphImporter::GetCreateDefaultFileName() const {
		return _kind == GraphSubgraphKind::EffectBlock ? "New Effect Block.sleffectblock" : "New Effect Operator.sleffectop";
	}

	bool EffectSubgraphImporter::CreateAssetSource(Buffer& out) {
		const str text = _kind == GraphSubgraphKind::EffectBlock ? EffectGraphEditOps::BlockSubgraphTemplate() : EffectGraphEditOps::OperatorSubgraphTemplate();
		out = Buffer(text.size(), MemTag::Asset);
		std::memcpy(out.data, text.data(), text.size());
		return true;
	}

	namespace EffectSubgraphImporters {

		void Register(const void* owner) {
			static EffectSubgraphImporter operators(GraphSubgraphKind::EffectOperator);
			static EffectSubgraphImporter blocks(GraphSubgraphKind::EffectBlock);
			AssetImporterRegistry::Register(operators.GetAssetType(), &operators, {GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION}, owner);
			AssetImporterRegistry::Register(blocks.GetAssetType(), &blocks, {GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION}, owner);
		}

	} // namespace EffectSubgraphImporters

} // namespace Shalltear
