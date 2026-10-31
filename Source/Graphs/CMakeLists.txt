file(GLOB_RECURSE GRAPHS_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(Graphs SOURCES ${GRAPHS_SOURCES})

target_compile_definitions(Graphs PRIVATE SHALLTEAR_GRAPHS_BUILD)
target_include_directories(Graphs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private)

target_link_libraries(Graphs PRIVATE ShalltearEditor)
