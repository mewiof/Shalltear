file(GLOB_RECURSE SAMPLE_MODULE_SOURCES "Private/*.cpp" "Private/*.h")

add_library(ShalltearSampleModule SHARED ${SAMPLE_MODULE_SOURCES})

target_link_libraries(ShalltearSampleModule PRIVATE ShalltearCore)
