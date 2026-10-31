cmake_minimum_required(VERSION 3.20)
project(CoinCollector CXX)

# yours now, the editor never overwrites it

# editor passes Shalltear_ROOT, elsewhere set SHALLTEAR_SDK
find_package(Shalltear REQUIRED)

file(GLOB_RECURSE COINCOLLECTOR_SOURCES CONFIGURE_DEPENDS "Source/*.cpp" "Source/*.h")

# add frequently included headers with target_precompile_headers
shalltear_add_module(CoinCollector SOURCES ${COINCOLLECTOR_SOURCES})
