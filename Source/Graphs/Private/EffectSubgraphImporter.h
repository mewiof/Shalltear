#pragma once

#include "GraphsAPI.h"

#include "GraphDefinitions.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	class SHALLTEAR_GRAPHS_API VisualEffectSubgraphOperator : public Asset {
	public:
		SLT_REFLECT_CLASS(VisualEffectSubgraphOperator)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		~VisualEffectSubgraphOperator() override;

		StringHash GetAssetTypeId() const override { return VisualEffectSubgraphOperator::GetReflectType()->name; }
	};

	class SHALLTEAR_GRAPHS_API VisualEffectSubgraphBlock : public Asset {
	public:
		SLT_REFLECT_CLASS(VisualEffectSubgraphBlock)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		~VisualEffectSubgraphBlock() override;

		StringHash GetAssetTypeId() const override { return VisualEffectSubgraphBlock::GetReflectType()->name; }
	};

	class EffectSubgraphImporter : public IAssetImporter {
	public:
		explicit EffectSubgraphImporter(GraphSubgraphKind kind) : _kind(kind) {}

		StringHash GetAssetType() const override;
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override;
		std::string_view GetCreateDefaultFileName() const override;
		bool CreateAssetSource(Buffer& out) override;
	private:
		GraphSubgraphKind _kind;
	};

	namespace EffectSubgraphImporters {
		SHALLTEAR_GRAPHS_API void Register(const void* owner);
	} // namespace EffectSubgraphImporters

} // namespace Shalltear
