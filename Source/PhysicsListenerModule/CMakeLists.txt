file(GLOB_RECURSE PHYSICS_LISTENER_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearPhysicsListenerModule SHARED ${PHYSICS_LISTENER_MODULE_SOURCES})

target_link_libraries(ShalltearPhysicsListenerModule PRIVATE ShalltearCore)

target_include_directories(ShalltearPhysicsListenerModule PRIVATE
	${CMAKE_SOURCE_DIR}/Source/Physics3D/Public)
