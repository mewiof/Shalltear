#ifndef SHALLTEAR_SUN_DISC_HLSLI
#define SHALLTEAR_SUN_DISC_HLSLI

float3 ShalltearSunDisc(float3 directionWS, float3 sunDirectionWS, float3 sunColor, float cosSize) {
	if (cosSize > 1.0) {
		return float3(0.0, 0.0, 0.0);
	}
	float cosAngle = dot(directionWS, sunDirectionWS);
	float feather = max((1.0 - cosSize) * 0.15, 1.0e-5);
	return sunColor * smoothstep(cosSize - feather, cosSize + feather, cosAngle);
}

#endif
