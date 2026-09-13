# The hot-reload runtime is infrastructure the host application links
# against directly. Unlike the plugins it loads at runtime (see
# source/examples/HotReloadDemo), this library is never itself meant
# to be hot-reloaded, so TYPE STATIC is a deliberate choice here,
# independent of the project-wide BUILD_SHARED_LIBS default.
add_lib_module(hotreload_lib TYPE STATIC OUTPUT_NAME "hotreload")

if(NOT WIN32)
	# dlopen/dlclose/dlinfo (ModuleHandle.cpp). PUBLIC, not PRIVATE:
	# for a STATIC library this needs to reach whatever executable
	# ultimately links hotreload_lib, not just hotreload_lib's own
	# (nonexistent, for a static lib) link step.
	target_link_libraries(hotreload_lib PUBLIC ${CMAKE_DL_LIBS})
endif()

if(MSVC)
	# UnDecorateSymbolName (Demangle.cpp) lives in dbghelp.lib, which --
	# unlike kernel32/user32 -- MSVC does not link by default. Without
	# this, the final link fails with an undefined symbol for it;
	# found via an actual Windows build of this library, not just
	# documentation. PUBLIC for the same reason as CMAKE_DL_LIBS above.
	# Guarded on MSVC (true for both real cl.exe and clang-cl, and
	# false for MinGW) rather than WIN32, since MinGW's build of
	# Demangle.cpp takes the __GNUC__/__cxa_demangle branch instead and
	# never needs dbghelp at all.
	target_link_libraries(hotreload_lib PUBLIC dbghelp)
endif()
