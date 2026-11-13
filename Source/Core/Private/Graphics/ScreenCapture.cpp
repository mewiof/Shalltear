#include "Shalltear/Core/Graphics/ScreenCapture.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/ImageEncoding.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Screen.h"

#include "Graphics/FrameCapture.h"
#include "ScreenInternal.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

namespace Shalltear {

	namespace {

		struct EncodeJob {
			Vector<u8> rgba{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			str path;
			std::atomic<bool> finished{false};
			bool written = false;
		};

		struct Shot {
			ScreenCaptureDone onDone;
			ScreenCaptureResult result;
		};

		struct Shots {
			std::vector<std::weak_ptr<Shot>> pending;
			std::vector<std::shared_ptr<Shot>> finished;
		};

		Shots& GetShots() {
			static Shots shots;
			return shots;
		}

		bool WriteBytes(const str& path, const Vector<u8>& bytes) {
			const std::filesystem::path target(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
			std::error_code ec;
			if (target.has_parent_path()) {
				std::filesystem::create_directories(target.parent_path(), ec);
			}
			std::ofstream out(target, std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return static_cast<bool>(out);
		}

		void Encode(EncodeJob& job) {
			Vector<u8> png{GetMemoryResource(MemTag::Graphics)};
			job.written = ImageEncoding::EncodePNG(job.rgba, job.size, png) && WriteBytes(job.path, png);
			job.finished.store(true, std::memory_order_release);
		}

		void Finish(const std::shared_ptr<Shot>& shot, ScreenCaptureResult result) {
			shot->result = std::move(result);
			GetShots().finished.push_back(shot);
		}

		GraphicsInternal::CaptureDelivery Deliver(str path, ScreenCaptureDone onDone) {
			auto shot = std::make_shared<Shot>();
			shot->onDone = std::move(onDone);
			std::vector<std::weak_ptr<Shot>>& pending = GetShots().pending;
			std::erase_if(pending, [](const std::weak_ptr<Shot>& entry) { return entry.expired(); });
			pending.push_back(shot);
			return [path = std::move(path), shot](bool captured, Vector<u8>&& rgba8, Vec2I size) -> GraphicsInternal::CaptureFollowUp {
				if (!captured) {
					SLT_LOG_WARN("Graphics", "couldn't capture '{}' (no frame to read back)", path);
					Finish(shot, ScreenCaptureResult{false, path, {}});
					return {};
				}
				auto job = std::make_shared<EncodeJob>();
				job->rgba = std::move(rgba8);
				job->size = size;
				job->path = path;
				if (JobSystem::GetWorkerCount() > 0) {
					JobSystem::Submit([job]() { Encode(*job); });
				} else {
					Encode(*job);
				}
				return [job, shot]() {
					if (!job->finished.load(std::memory_order_acquire)) {
						return false;
					}
					if (!job->written) {
						SLT_LOG_WARN("Graphics", "couldn't write screenshot '{}'", job->path);
					}
					Finish(shot, ScreenCaptureResult{job->written, job->path, job->size});
					return true;
				};
			};
		}

	} // anonymous namespace

	namespace GraphicsInternal {

		void DeliverFinishedCaptures() {
			std::vector<std::shared_ptr<Shot>> finished;
			finished.swap(GetShots().finished);
			for (const std::shared_ptr<Shot>& shot : finished) {
				const ScreenCaptureDone onDone = std::move(shot->onDone);
				if (onDone) {
					onDone(shot->result);
				}
			}
		}

		void DropCaptureCallbacks() {
			Shots& shots = GetShots();
			for (const std::weak_ptr<Shot>& entry : shots.pending) {
				if (const std::shared_ptr<Shot> shot = entry.lock()) {
					shot->onDone = {};
				}
			}
			shots.pending.clear();
			shots.finished.clear();
		}

	} // namespace GraphicsInternal

	namespace ScreenCapture {

		bool CaptureScreenshot(std::string_view filePath, ScreenCaptureDone onDone) {
			if (filePath.empty() || !RenderSystem::IsRenderingAvailable()) {
				return false;
			}
			if (const Handle<Window> window = ScreenInternal::GetGameWindow(); window.IsValid()) {
				GraphicsInternal::RequestWindowCapture(window, Deliver(str(filePath), std::move(onDone)));
				return true;
			}
			const Vec2I size(Screen::GetWidth(), Screen::GetHeight());
			if (size.x <= 0 || size.y <= 0) {
				return false;
			}
			GraphicsInternal::RequestSceneCapture(size, Deliver(str(filePath), std::move(onDone)));
			return true;
		}

		bool CaptureCamera(Handle<Camera> camera, Vec2I sizePx, std::string_view filePath, ScreenCaptureDone onDone) {
			if (filePath.empty() || !camera.IsValid() || sizePx.x <= 0 || sizePx.y <= 0 || !RenderSystem::IsRenderingAvailable()) {
				return false;
			}
			const Handle<Camera> cameras[] = {camera};
			GraphicsInternal::RequestCamerasCapture(cameras, sizePx, Deliver(str(filePath), std::move(onDone)));
			return true;
		}

	} // namespace ScreenCapture

} // namespace Shalltear
