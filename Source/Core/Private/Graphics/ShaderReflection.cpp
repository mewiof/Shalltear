#include "Shalltear/Core/Graphics/ShaderReflection.h"

namespace Shalltear {

	u32 UniformMemberFloatCount(UniformMemberType type) {
		switch (type) {
		case UniformMemberType::Float:
			return 1;
		case UniformMemberType::Vec2:
			return 2;
		case UniformMemberType::Vec3:
			return 3;
		case UniformMemberType::Vec4:
			return 4;
		default:
			return 0;
		}
	}

} // namespace Shalltear
