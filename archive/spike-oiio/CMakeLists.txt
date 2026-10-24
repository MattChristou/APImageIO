cmake_minimum_required(VERSION 3.20)
project(oiio_spike C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
find_package(OpenImageIO CONFIG REQUIRED)
add_executable(oiio_spike ../src/oiiobridge.cpp oiio_spike.cpp)
target_link_libraries(oiio_spike PRIVATE OpenImageIO::OpenImageIO)
target_link_options(oiio_spike PRIVATE /MAP /DEBUG)
target_compile_options(oiio_spike PRIVATE /Zi)

# DLL-load harness (mirrors how LightWave loads our .p): OIIO statically inside
# a /MT DLL, loaded by a plain stub exe.
add_library(oiio_dlltest SHARED ../src/oiiobridge.cpp dlltest.cpp)
target_link_libraries(oiio_dlltest PRIVATE OpenImageIO::OpenImageIO)

add_executable(oiio_dllstub dllstub.c)
