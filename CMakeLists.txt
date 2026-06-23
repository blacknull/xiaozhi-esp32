# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.0.0")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Uncomment to enable support for the Weirboard (my custom board)
#add_compile_options(-DMY_WEIRDBOARD=1)

# Uncomment to temporarily disable OTA firmware upgrade (server config/time sync still works)
add_compile_options(-DCONFIG_DISABLE_OTA_UPGRADE=1)

# Uncomment to show user/assistant chat text on display during conversation (default: off)
#add_compile_options(-DCONFIG_SHOW_CHAT_MSG=1)

# Comment out to disable servo (PCA9685) control and related MCP tools
#set(CONFIG_ENABLE_SERVO ON CACHE BOOL "Enable servo (PCA9685) control")
if(CONFIG_ENABLE_SERVO)
    add_compile_options(-DCONFIG_ENABLE_SERVO=1)
endif()

# Comment out to disable Otto robot servo control and related MCP tools
#set(CONFIG_ENABLE_OTTO ON CACHE BOOL "Enable Otto robot servo control")
if(CONFIG_ENABLE_OTTO)
    add_compile_options(-DCONFIG_ENABLE_OTTO=1)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

