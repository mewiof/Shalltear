file(GLOB_RECURSE IMPORT_PIPELINE_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(ImportPipeline SOURCES ${IMPORT_PIPELINE_SOURCES})

target_include_directories(ImportPipeline
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
        ${CMAKE_SOURCE_DIR}/Source/Core/Private)

set(SHALLTEAR_UFBX_DIR ${CMAKE_SOURCE_DIR}/External/Submodules/ufbx)
if(NOT EXISTS ${SHALLTEAR_UFBX_DIR}/ufbx.h)
    message(FATAL_ERROR "ufbx submodule missing. Run: git submodule update --init --recursive")
endif()

target_sources(ImportPipeline PRIVATE ${SHALLTEAR_UFBX_DIR}/ufbx.c)
set_source_files_properties(${SHALLTEAR_UFBX_DIR}/ufbx.c PROPERTIES C_STANDARD 99 COMPILE_OPTIONS -Wno-error)
target_include_directories(ImportPipeline PRIVATE ${SHALLTEAR_UFBX_DIR})

target_compile_definitions(ImportPipeline PRIVATE
    UFBX_NO_FORMAT_OBJ
    UFBX_NO_GEOMETRY_CACHE
    UFBX_NO_SUBDIVISION
    UFBX_NO_TESSELLATION)

file(GLOB IMPORT_PIPELINE_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/Private/ThirdParty/*/*.cpp")
set_source_files_properties(${IMPORT_PIPELINE_THIRD_PARTY} PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(ImportPipeline PRIVATE RGBCX_USE_SMALLER_TABLES=1)

if(SHALLTEAR_BUILD_SHARED)
    file(GLOB_RECURSE IMPORT_PIPELINE_EDITOR_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../ImportPipelineEditor/Private/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ImportPipelineEditor/Private/*.h")
    if(NOT IMPORT_PIPELINE_EDITOR_SOURCES)
        message(FATAL_ERROR "Source/ImportPipelineEditor/Private has no sources (package's editor half is missing).")
    endif()
    target_sources(ImportPipeline PRIVATE ${IMPORT_PIPELINE_EDITOR_SOURCES})
    target_link_libraries(ImportPipeline PRIVATE ShalltearEditor)
    target_compile_definitions(ImportPipeline PRIVATE SLT_WITH_EDITOR)
    target_include_directories(ImportPipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ImportPipelineEditor/Private)
endif()
