#include "Shalltear/Physics3D/PhysicsMaterial.h"

#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	PhysicsMaterial::~PhysicsMaterial() = default;

	void RegisterPhysicsMaterialImporter(const void* owner) {
		AssetImporterRegistry::RegisterDeclared(PhysicsMaterial::GetReflectType(), owner);
	}

} // namespace Shalltear
