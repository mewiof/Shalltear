if(NOT SHALLTEAR_BUILD_SHARED)
	return()
endif()

file(GLOB_RECURSE PHYSICS_SCRIPT_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearPhysicsScriptModule SHARED ${PHYSICS_SCRIPT_MODULE_SOURCES})

target_link_libraries(ShalltearPhysicsScriptModule PRIVATE ShalltearCore Physics3D)
