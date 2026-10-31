#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Window/Window.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API Camera : public Component {
	public:
		SLT_REFLECT_COMPONENT(Camera, Component)
		SLT_REFLECT_FIELD(_fieldOfViewDeg, SLT_RANGE(1.0f, 179.0f))
		SLT_REFLECT_FIELD(_nearPlane)
		SLT_REFLECT_FIELD(_farPlane)
		SLT_REFLECT_FIELD(_priority)
		SLT_REFLECT_FIELD(_clearColor)
		SLT_REFLECT_FIELD(_cullingMask)
		SLT_REFLECT_FIELD(_iso)
		SLT_REFLECT_FIELD(_shutterSpeed)
		SLT_REFLECT_FIELD(_aperture, SLT_RANGE(0.7f, 32.0f))
		SLT_REFLECT_FIELD(_targetWindow, SLT_TRANSIENT)
		SLT_REFLECT_ARRAY(_passes)
		SLT_END_REFLECT()

		Camera() = default;
		~Camera() override;

		f32 GetFieldOfViewDeg() const { return _fieldOfViewDeg; }
		void SetFieldOfViewDeg(f32 degrees) { _fieldOfViewDeg = degrees; }

		f32 GetNearPlane() const { return _nearPlane; }
		void SetNearPlane(f32 distance) { _nearPlane = distance; }
		f32 GetFarPlane() const { return _farPlane; }
		void SetFarPlane(f32 distance) { _farPlane = distance; }

		i32 GetPriority() const { return _priority; }
		void SetPriority(i32 priority) { _priority = priority; }

		Color GetClearColor() const { return _clearColor; }
		void SetClearColor(const Color& color) { _clearColor = color; }

		u32 GetCullingMask() const { return _cullingMask; }
		void SetCullingMask(u32 mask) { _cullingMask = mask; }

		i32 GetIso() const { return _iso; }
		void SetIso(i32 iso) { _iso = iso; }

		f32 GetShutterSpeed() const { return _shutterSpeed; }
		void SetShutterSpeed(f32 seconds) { _shutterSpeed = seconds; }

		f32 GetAperture() const { return _aperture; }
		void SetAperture(f32 fNumber) { _aperture = fNumber; }

		u32 GetPassCount() const { return static_cast<u32>(_passes.size()); }
		Handle<RenderPass> GetPass(u32 index) const {
			return index < _passes.size() ? _passes[index] : Handle<RenderPass>{};
		}
		void SetPass(u32 index, Handle<RenderPass> pass) {
			if (index >= _passes.size()) {
				_passes.resize(index + 1);
			}
			_passes[index] = pass;
		}

		Handle<Window> GetTargetWindow() const { return _targetWindow; }
		void SetTargetWindow(Handle<Window> window) { _targetWindow = window; }

		Mat4 BuildProjection(f32 aspect) const {
			return Mat4::PerspectiveReverseZ(_fieldOfViewDeg, aspect, _nearPlane, _farPlane);
		}
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		f32 _fieldOfViewDeg = 60.0f;
		f32 _nearPlane = 0.1f;
		f32 _farPlane = 1000.0f;
		i32 _priority = 0;
		Color _clearColor = Color::Black();
		u32 _cullingMask = ~0u;
		i32 _iso = 200;
		f32 _shutterSpeed = 1.0f / 200.0f;
		f32 _aperture = 16.0f;
		Handle<Window> _targetWindow;
		Vector<Handle<RenderPass>> _passes{GetMemoryResource(MemTag::Scene)};
	};

} // namespace Shalltear
