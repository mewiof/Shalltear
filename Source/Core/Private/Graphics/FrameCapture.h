#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <functional>
#include <span>

namespace Shalltear {
	class Camera;
	class Window;
} // namespace Shalltear

namespace Shalltear::GraphicsInternal {

	// polled once per RenderAll until true
	using CaptureFollowUp = std::function<bool()>;
	// main thread inside later RenderAll. rgba8 rows top first, alpha 255
	using CaptureDelivery = std::function<CaptureFollowUp(bool captured, Vector<u8>&& rgba8, Vec2I size)>;

	// frame as shown, screen UI included
	void RequestWindowCapture(Handle<Window> window, CaptureDelivery deliver);
	void RequestCamerasCapture(std::span<const Handle<Camera>> cameras, Vec2I size, CaptureDelivery deliver);
	void RequestSceneCapture(Vec2I size, CaptureDelivery deliver);
	void DeliverFinishedCaptures();
	void DropCaptureCallbacks();

	bool ToRGBA8(std::span<const u8> texels, RHI::Format format, Vec2I size, Vector<u8>& out);

} // namespace Shalltear::GraphicsInternal
