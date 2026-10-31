file(GLOB_RECURSE SAMPLE_DEPENDENT_PACKAGE_SOURCES "Private/*.cpp" "Private/*.h")

shalltear_add_package(SampleDependentPackage SOURCES ${SAMPLE_DEPENDENT_PACKAGE_SOURCES})
