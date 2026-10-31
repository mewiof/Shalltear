#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear::RHI {

	struct SwapChainImpl;

	class SHALLTEAR_CORE_API SwapChain {
	public:
		SwapChain(const SwapChain&) = delete;
		SwapChain& operator=(const SwapChain&) = delete;

		// between BeginFrame/EndFrame, may be invalid (skip the window)
		TextureHandle AcquireNextImage();

		void Present();

		void Recreate(Vec2I newSizePx, bool vsync);

		Format GetFormat() const;
		Vec2I GetSize() const;
	private:
		friend class Device;
		friend struct DeviceImpl;
		SwapChain() = default;
		~SwapChain() = default;

		template <typename T, typename... Args>
		friend T* Shalltear::New(MemTag tag, Args&&... args);
		template <typename T>
		friend void Shalltear::Delete(T* p) noexcept;

		SwapChainImpl* _impl = nullptr;
	};

} // namespace Shalltear::RHI
