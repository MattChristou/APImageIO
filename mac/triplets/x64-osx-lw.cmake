# Static, release-only, macOS 11+ -- matches the plugin's deployment target so
# the linker doesn't warn about objects built for a newer macOS.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)
set(VCPKG_BUILD_TYPE release)
