# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Uncomment to enable support for the Weirboard (my custom board)
add_compile_options(-DMY_WEIRDBOARD=1)

# Uncomment to temporarily disable OTA firmware upgrade (server config/time sync still works)
add_compile_options(-DCONFIG_DISABLE_OTA_UPGRADE=1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.2.4")
project(xiaozhi)
