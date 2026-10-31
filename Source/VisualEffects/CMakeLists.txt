file(GLOB_RECURSE VISUALEFFECTS_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(VisualEffects SOURCES ${VISUALEFFECTS_SOURCES})

if(SHALLTEAR_BUILD_SHARED)
	file(GLOB_RECURSE VISUALEFFECTS_EDITOR_SOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/../VisualEffectsEditor/Private/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/../VisualEffectsEditor/Private/*.h")
	if(NOT VISUALEFFECTS_EDITOR_SOURCES)
		message(FATAL_ERROR "Source/VisualEffectsEditor/Private has no sources (package's editor half is missing).")
	endif()
	target_sources(VisualEffects PRIVATE ${VISUALEFFECTS_EDITOR_SOURCES})
	target_link_libraries(VisualEffects PRIVATE Graphs ShalltearEditor)
	target_compile_definitions(VisualEffects PRIVATE SLT_WITH_EDITOR)
	target_include_directories(VisualEffects PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../VisualEffectsEditor/Private
		${CMAKE_CURRENT_SOURCE_DIR}/Private
		${CMAKE_SOURCE_DIR}/Source/Graphs/Private)
endif()
