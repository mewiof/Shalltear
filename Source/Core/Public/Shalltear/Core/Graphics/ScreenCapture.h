#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	class Camera;

	struct ScreenCaptureResult {
		bool succeeded = false;
		str path;
		Vec2I size{};
	};

	// at EndFrame after capture, dropped on module unload
	using ScreenCaptureDone = std::function<void(const ScreenCaptureResult& result)>;

	namespace ScreenCapture {

		// game window as shown (screen UI included), editor: scene cameras only
		SHALLTEAR_CORE_API bool CaptureScreenshot(std::string_view filePath, ScreenCaptureDone onDone = {});

		SHALLTEAR_CORE_API bool CaptureCamera(Handle<Camera> camera, Vec2I sizePx, std::string_view filePath,
											  ScreenCaptureDone onDone = {});

	} // namespace ScreenCapture

} // namespace Shalltear
