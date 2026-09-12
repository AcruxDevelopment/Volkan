# _discover_module_subdirectories() (cmake/BuildHelpers.cmake) only
# add_subdirectory()'s the IMMEDIATE children of source/examples/ --
# this file exists purely so that mechanism finds this folder at all,
# then hands off to its three real targets: one host executable and
# two builds of the same plugin (see PluginV1/CMakeLists.txt for why
# two separate builds rather than one). Shared/ has no CMakeLists.txt
# of its own -- it is just PluginApi.h, pulled in by PluginV1/PluginV2
# via target_include_directories(), not add_subdirectory().
add_subdirectory(PluginV1)
add_subdirectory(PluginV2)
add_subdirectory(Host)
