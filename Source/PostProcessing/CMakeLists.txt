file(GLOB_RECURSE POSTPROCESSING_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(PostProcessing SOURCES ${POSTPROCESSING_SOURCES})

if(SHALLTEAR_BUILD_SHARED)
	file(GLOB_RECURSE POSTPROCESSING_EDITOR_SOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/../PostProcessingEditor/Private/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/../PostProcessingEditor/Private/*.h")
	if(NOT POSTPROCESSING_EDITOR_SOURCES)
		message(FATAL_ERROR "Source/PostProcessingEditor/Private has no sources (package's editor half is missing).")
	endif()
	target_sources(PostProcessing PRIVATE ${POSTPROCESSING_EDITOR_SOURCES})
	target_link_libraries(PostProcessing PRIVATE ShalltearEditor)
	target_compile_definitions(PostProcessing PRIVATE SLT_WITH_EDITOR)
	target_include_directories(PostProcessing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../PostProcessingEditor/Private)
endif()
