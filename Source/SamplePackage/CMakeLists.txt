file(GLOB_RECURSE SAMPLE_PACKAGE_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(SamplePackage SOURCES ${SAMPLE_PACKAGE_SOURCES})
