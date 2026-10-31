file(GLOB_RECURSE ANIMATION_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(Animation SOURCES ${ANIMATION_SOURCES})

file(GLOB ANIMATION_CONTROLLER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Private/Controller/*.cpp")
if(NOT ANIMATION_CONTROLLER_SOURCES)
	message(FATAL_ERROR "Source/Animation/Private/Controller has no sources (guarded directory is missing).")
endif()
set_source_files_properties(${ANIMATION_CONTROLLER_SOURCES}
	DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	PROPERTIES COMPILE_DEFINITIONS SLT_ANIMATION_PUBLIC_API_ONLY)

if(SHALLTEAR_BUILD_SHARED)
	file(GLOB_RECURSE ANIMATION_EDITOR_SOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/../AnimationEditor/Private/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/../AnimationEditor/Private/*.h")
	if(NOT ANIMATION_EDITOR_SOURCES)
		message(FATAL_ERROR "Source/AnimationEditor/Private has no sources (package's editor half is missing).")
	endif()
	target_sources(Animation PRIVATE ${ANIMATION_EDITOR_SOURCES})
	target_link_libraries(Animation PRIVATE ImportPipeline ShalltearEditor)
	target_compile_definitions(Animation PRIVATE SLT_WITH_EDITOR)

	target_include_directories(Animation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../AnimationEditor/Private)

	target_include_directories(Animation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private)
endif()
