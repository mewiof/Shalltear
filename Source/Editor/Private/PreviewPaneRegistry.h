#pragma once

#include "Shalltear/Core/StringHash.h"

#include <string_view>

namespace Shalltear {

	struct AssetPreviewPaneDesc;

	namespace EditorPreviewsInternal {

		// valid until the next Register/sweep (never store)
		const AssetPreviewPaneDesc* FindPane(StringHash assetTypeName);

		StringHash AssetTypeForFileName(std::string_view fileName);

		bool HasProducer(StringHash assetTypeName);

	} // namespace EditorPreviewsInternal

} // namespace Shalltear
