file(GLOB_RECURSE PLAYER_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearPlayerEntry STATIC ${PLAYER_SOURCES})

target_link_libraries(ShalltearPlayerEntry PRIVATE ShalltearCore)

set_target_properties(ShalltearPlayerEntry PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

include(${CMAKE_SOURCE_DIR}/CMake/ShalltearPlayer.cmake)
_shalltear_write_build_stamp(ShalltearPlayerEntry FALSE)
