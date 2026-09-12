# Hand-written rather than add_lib_module(), same reasoning as
# HotReloadDemo/PluginV1's CMakeLists.txt: this needs to be a plain,
# always-rebuildable SHARED library gated on BUILD_TESTS rather than a
# PRIMARY install target.

if(BUILD_TESTS)
	add_library(hotreload_tests_undefined_symbol_fixture SHARED src/Fixture.cpp)
else()
	add_library(hotreload_tests_undefined_symbol_fixture SHARED EXCLUDE_FROM_ALL src/Fixture.cpp)
endif()

set_target_properties(hotreload_tests_undefined_symbol_fixture PROPERTIES
	OUTPUT_NAME "hotreload_tests_undefined_symbol_fixture"
	WINDOWS_EXPORT_ALL_SYMBOLS ON)
_set_output_dirs(hotreload_tests_undefined_symbol_fixture
	"${CMAKE_BINARY_DIR}/${RUNTIME_OUTPUT_SUBDIR}/tests"
	"${CMAKE_BINARY_DIR}/${LIBRARY_OUTPUT_SUBDIR}/tests")
_apply_compile_options(hotreload_tests_undefined_symbol_fixture)

_register_module_target(TEST hotreload_tests_undefined_symbol_fixture)
add_dependencies(all_tests hotreload_tests_undefined_symbol_fixture)
