#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API UnresolvedComponent final : public Component {
	public:
		~UnresolvedComponent() override;

		SLT_REFLECT_CLASS(UnresolvedComponent)
		SLT_REFLECT_PARENT(Component)
		builder.SetFlags(::Shalltear::TypeFlagComponent);
		SLT_END_REFLECT()

		StringHash GetUnresolvedType() const { return _unresolvedType; }
	private:
		friend struct SceneInternalAccess;

		StringHash _unresolvedType;
		bool _blobIsBinary = false;
		str _text;
		Vector<u8> _bytes{GetMemoryResource(MemTag::Scene)};
	};

} // namespace Shalltear
