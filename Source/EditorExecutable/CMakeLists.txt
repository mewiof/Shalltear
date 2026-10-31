file(GLOB_RECURSE EDITOR_EXECUTABLE_SOURCES "Private/*.cpp" "Private/*.h")

if(NOT CMAKE_RC_COMPILER OR NOT EXISTS "${CMAKE_RC_COMPILER}")
    get_filename_component(SHALLTEAR_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(SHALLTEAR_RC_COMPILER
        NAMES x86_64-w64-mingw32-windres llvm-windres windres
        HINTS "${SHALLTEAR_TOOLCHAIN_BIN}"
        NO_DEFAULT_PATH)
    if(SHALLTEAR_RC_COMPILER)
        set(CMAKE_RC_COMPILER "${SHALLTEAR_RC_COMPILER}" CACHE FILEPATH "" FORCE)
    endif()
endif()
enable_language(RC)

add_executable(ShalltearEditorExecutable ${EDITOR_EXECUTABLE_SOURCES} Shalltear.rc)

set_target_properties(ShalltearEditorExecutable PROPERTIES OUTPUT_NAME Shalltear)

target_link_libraries(ShalltearEditorExecutable PRIVATE ShalltearEditor)

set_target_properties(ShalltearEditorExecutable PROPERTIES WIN32_EXECUTABLE ON)
if(NOT MINGW)
    target_link_options(ShalltearEditorExecutable PRIVATE "-Wl,/ENTRY:mainCRTStartup")
endif()
