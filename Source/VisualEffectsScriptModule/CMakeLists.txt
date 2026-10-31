if(NOT SHALLTEAR_BUILD_SHARED)
	return()
endif()

file(GLOB_RECURSE VISUALEFFECTS_SCRIPT_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearVisualEffectsScriptModule SHARED ${VISUALEFFECTS_SCRIPT_MODULE_SOURCES})

target_link_libraries(ShalltearVisualEffectsScriptModule PRIVATE ShalltearCore VisualEffects)
