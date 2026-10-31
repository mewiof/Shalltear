#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics.h"

using namespace Shalltear;

extern "C" __declspec(dllexport) u32 PhysicsScriptModuleOverlapSphereCount(f32 centerX, f32 centerY, f32 centerZ, f32 radius) {
	Vector<Handle<Collider>> results;
	return Physics::OverlapSphere(Vec3{centerX, centerY, centerZ}, radius, results);
}

extern "C" __declspec(dllexport) bool PhysicsScriptModuleRaycastHits(f32 originX, f32 originY, f32 originZ,
																	 f32 dirX, f32 dirY, f32 dirZ) {
	RaycastHit hit;
	return Physics::Raycast(Ray{Vec3{originX, originY, originZ}, Vec3{dirX, dirY, dirZ}}, hit);
}

SLT_IMPLEMENT_MODULE(PhysicsScriptModule)
