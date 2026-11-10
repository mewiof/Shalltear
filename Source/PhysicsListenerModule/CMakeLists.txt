file(GLOB_RECURSE PHYSICS_LISTENER_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearPhysicsListenerModule SHARED ${PHYSICS_LISTENER_MODULE_SOURCES})

target_link_libraries(ShalltearPhysicsListenerModule PRIVATE ShalltearCore Physics3D)
