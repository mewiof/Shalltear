#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	enum class LightType : u8 {
		Directional = 0,
		Point,
		Spot
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::LightType)
SLT_ENUM_VALUE(Directional)
SLT_ENUM_VALUE(Point)
SLT_ENUM_VALUE(Spot)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_CORE_API Light : public Component {
	public:
		SLT_REFLECT_COMPONENT(Light, Component)
		SLT_REFLECT_FIELD(_lightType)
		SLT_REFLECT_FIELD(_color)
		SLT_REFLECT_FIELD(_intensity)
		SLT_REFLECT_FIELD(_range)
		SLT_REFLECT_FIELD(_spotAngleDeg, SLT_RANGE(1.0f, 179.0f))
		SLT_REFLECT_FIELD(_innerSpotAngleDeg, SLT_RANGE(0.0f, 179.0f))
		SLT_REFLECT_FIELD(_castShadows)
		SLT_REFLECT_FIELD(_shadowStrength, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_shadowBias, SLT_RANGE(0.0f, 4.0f))
		SLT_REFLECT_FIELD(_shadowNormalBias, SLT_RANGE(0.0f, 4.0f))
		SLT_END_REFLECT()

		Light() = default;
		~Light() override;

		LightType GetLightType() const { return _lightType; }
		void SetLightType(LightType type) { _lightType = type; }

		Color GetColor() const { return _color; }
		void SetColor(const Color& color) { _color = color; }

		f32 GetIntensity() const { return _intensity; }
		void SetIntensity(f32 intensity) { _intensity = intensity; }

		f32 GetRange() const { return _range; }
		void SetRange(f32 range) { _range = range; }

		f32 GetSpotAngleDeg() const { return _spotAngleDeg; }
		void SetSpotAngleDeg(f32 degrees) { _spotAngleDeg = degrees; }

		f32 GetInnerSpotAngleDeg() const { return _innerSpotAngleDeg; }
		void SetInnerSpotAngleDeg(f32 degrees) { _innerSpotAngleDeg = degrees; }

		bool GetCastShadows() const { return _castShadows; }
		void SetCastShadows(bool cast) { _castShadows = cast; }

		f32 GetShadowStrength() const { return _shadowStrength; }
		void SetShadowStrength(f32 strength) { _shadowStrength = strength; }

		f32 GetShadowBias() const { return _shadowBias; }
		void SetShadowBias(f32 bias) { _shadowBias = bias; }

		f32 GetShadowNormalBias() const { return _shadowNormalBias; }
		void SetShadowNormalBias(f32 bias) { _shadowNormalBias = bias; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		LightType _lightType = LightType::Directional;
		Color _color = Color::White();
		f32 _intensity = 1.0f;
		f32 _range = 10.0f;
		f32 _spotAngleDeg = 45.0f;
		f32 _innerSpotAngleDeg = 0.0f;
		bool _castShadows = true;
		f32 _shadowStrength = 1.0f;
		f32 _shadowBias = 1.0f;
		f32 _shadowNormalBias = 1.0f;
	};

} // namespace Shalltear
