#pragma once

namespace Shalltear {
	class MeshAsset;
	class TextureAsset;
} // namespace Shalltear

namespace Shalltear::GraphicsInternal {

	bool RestoreDeviceContent(TextureAsset& texture);
	bool RestoreDeviceContent(MeshAsset& mesh);

} // namespace Shalltear::GraphicsInternal
