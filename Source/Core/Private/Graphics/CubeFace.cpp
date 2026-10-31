#include "Shalltear/Core/Graphics/CubeFace.h"

#include "Graphics/EnvironmentIBL.h"

namespace Shalltear {

	Mat4 CubeFaceView(u32 face, const Vec3& eye) {
		const Vec3 forward = GraphicsInternal::CubeTexelDirection(face, 0.5f, 0.5f);
		const Vec3 top = GraphicsInternal::CubeTexelDirection(face, 0.5f, 0.0f);
		return Mat4::LookAt(eye, eye + forward, top);
	}

} // namespace Shalltear
