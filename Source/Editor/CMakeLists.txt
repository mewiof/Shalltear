file(GLOB_RECURSE EDITOR_SOURCES "Private/*.cpp" "Private/*.h" "Public/*.h")

add_library(ShalltearEditor ${SHALLTEAR_LIB_TYPE} ${EDITOR_SOURCES})

set_target_properties(ShalltearEditor PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(ShalltearEditor PRIVATE SHALLTEAR_EDITOR_BUILD)

target_include_directories(ShalltearEditor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

if(NOT SHALLTEAR_DISABLE_PCH)
    target_precompile_headers(ShalltearEditor PRIVATE Private/EditorPCH.h)
endif()

target_link_libraries(ShalltearEditor PUBLIC ShalltearCore)
