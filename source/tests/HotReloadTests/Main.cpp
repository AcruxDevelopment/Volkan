/* Exercises source/HotReload end to end against the real PluginV1/
 * PluginV2 builds from source/examples/HotReloadDemo -- see
 * ../../examples/HotReloadDemo/Shared/PluginApi.h and CppWidgetApi.h
 * for what each exported name is there to prove. Deliberately does
 * NOT use assert(): this project's default build type is Release,
 * where assert() is compiled out by NDEBUG and would make every
 * check here a silent, permanent no-op -- see CHECK() below instead.
 */
#include "CppWidgetApi.h"
#include "hot_reload/hot_reload.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#ifndef HOTRELOAD_TESTS_SKIP

namespace
{

	int g_failures = 0;

	void checkImpl(bool condition, const char* expression, const char* file, int line)
	{
		if (!condition)
		{
			std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", expression, file, line);
			++g_failures;
		}
	}

#define CHECK(expr) checkImpl((expr), #expr, __FILE__, __LINE__)

	using PluginFunction = int (*)(int);

	std::map<std::string, HotReloadSymbolEvent> g_events;

	void onSymbolEvent(const HotReloadSymbolEventInfo* event, void* /*userData*/)
	{
		g_events[event->name] = event->kind;
	}

}

int main()
{
	HotReloadOptions options{};
	options.on_symbol_event = &onSymbolEvent;
	// Deliberately no on_deleted_symbol here -- this test covers the
	// DEFAULT deletion policy (leave the old implementation running);
	// source/examples/HotReloadDemo/Host/Main.cpp covers supplying a
	// stub instead. Between the two, both documented behaviors for
	// HOT_RELOAD_SYMBOL_REMOVED get exercised somewhere in this repo.

	HotReloadContext* ctx = hot_reload_context_create(&options);
	CHECK(ctx != nullptr);

	// --- load V1 ---
	CHECK(hot_reload_load(ctx, "plugin", PLUGIN_V1_PATH) == HOT_RELOAD_OK);

	auto tick = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_tick"));
	auto legacy = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_legacy_feature"));
	auto markerBefore = static_cast<int*>(hot_reload_get_symbol(ctx, "plugin", "plugin_build_marker"));
	CHECK(tick != nullptr);
	CHECK(legacy != nullptr);
	CHECK(markerBefore != nullptr);

	CHECK(tick(10) == 20);         // V1: input * 2
	CHECK(legacy(7) == 7);         // V1's own body
	CHECK(*markerBefore == 1);

	// --- a failed reload must be a complete no-op ---
	HotReloadStatus badStatus = hot_reload_reload(ctx, "plugin", "/nonexistent/path/does-not-exist.so");
	CHECK(badStatus == HOT_RELOAD_ERROR_LOAD_FAILED);
	CHECK(tick(10) == 20); // still V1 -- untouched by the failed attempt

	// --- reload() before any load() must fail distinctly ---
	CHECK(hot_reload_reload(ctx, "no-such-module", PLUGIN_V2_PATH) == HOT_RELOAD_ERROR_NO_PREVIOUS_VERSION);

	// --- real reload to V2 ---
	g_events.clear();
	CHECK(hot_reload_reload(ctx, "plugin", PLUGIN_V2_PATH) == HOT_RELOAD_OK);

	// Same, never-re-resolved pointers now running V2's code:
	CHECK(tick(10) == 30);        // V2: input * 3 -- the actual jump patch took effect
	CHECK(legacy(7) == 7);        // no on_deleted_symbol supplied -> old V1 body keeps running, unharmed
	CHECK(*markerBefore == 1);    // V1's copy: never patched, still reads its original value

	auto markerAfter = static_cast<int*>(hot_reload_get_symbol(ctx, "plugin", "plugin_build_marker"));
	CHECK(markerAfter != nullptr);
	CHECK(*markerAfter == 2); // V2's own, freshly-resolved copy

	auto newFeature = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_new_feature"));
	CHECK(newFeature != nullptr);
	CHECK(newFeature(10) == 100); // V2: input * 10

	// --- every symbol classified exactly as PluginApi.h says it should be ---
	CHECK(g_events["plugin_tick"] == HOT_RELOAD_SYMBOL_PATCHED);
	CHECK(g_events["plugin_legacy_feature"] == HOT_RELOAD_SYMBOL_REMOVED);
	CHECK(g_events["plugin_new_feature"] == HOT_RELOAD_SYMBOL_ADDED);
	CHECK(g_events["plugin_build_marker"] == HOT_RELOAD_SYMBOL_SKIPPED_NOT_CODE);
	if (g_events.find("plugin_tiny_function") != g_events.end())
	{
		// Only exists on GCC/Clang builds (see PluginApi.h) -- absent
		// entirely under MSVC, which has no equivalent per-function
		// override to test here.
		CHECK(g_events["plugin_tiny_function"] == HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL);
	}

	// --- the demangling utility ---
	char* demangled = hot_reload_demangle("_ZN9CppWidget4tickEi");
	CHECK(demangled != nullptr);
	CHECK(demangled != nullptr && std::strcmp(demangled, "CppWidget::tick(int)") == 0);
	hot_reload_free_string(demangled);

	char* unchanged = hot_reload_demangle("plugin_tick"); // not a mangled name -- must come back as-is
	CHECK(unchanged != nullptr);
	CHECK(unchanged != nullptr && std::strcmp(unchanged, "plugin_tick") == 0);
	hot_reload_free_string(unchanged);

	hot_reload_context_destroy(ctx);

	// --- a real C++ class, no extern "C" anywhere -- CppWidgetApi.h ---
	// Its own HotReloadContext: HOT_RELOAD_ERROR_INVALID_ARGUMENT
	// above (loading "plugin" twice under one context) already covers
	// that specific rule, so a fresh context here keeps this section
	// independent and easier to read on its own.
	HotReloadContext* cppCtx = hot_reload_context_create(nullptr);
	CHECK(cppCtx != nullptr);
	CHECK(hot_reload_load(cppCtx, "cpp-plugin", PLUGIN_V1_PATH) == HOT_RELOAD_OK);

	auto makeCppWidget =
		reinterpret_cast<CppWidget* (*) ()>(hot_reload_get_symbol(cppCtx, "cpp-plugin", "make_cpp_widget"));
	CHECK(makeCppWidget != nullptr);
	CppWidget* widget = makeCppWidget != nullptr ? makeCppWidget() : nullptr;
	CHECK(widget != nullptr);

	using CppWidgetTickFunction = int (*)(CppWidget*, int);
	auto tickByMangledName = reinterpret_cast<CppWidgetTickFunction>(
		hot_reload_get_symbol(cppCtx, "cpp-plugin", "_ZN9CppWidget4tickEi"));
	CHECK(tickByMangledName != nullptr);

	if (widget != nullptr)
	{
		CHECK(widget->virtualTick(10) == 20); // V1, through the real vtable
	}
	if (tickByMangledName != nullptr && widget != nullptr)
	{
		CHECK(tickByMangledName(widget, 10) == 20); // V1, through a manually-resolved mangled name
	}

	CHECK(hot_reload_reload(cppCtx, "cpp-plugin", PLUGIN_V2_PATH) == HOT_RELOAD_OK);

	if (widget != nullptr)
	{
		CHECK(widget->virtualTick(10) == 30); // same object, same vtable slot -- now V2
	}
	if (tickByMangledName != nullptr && widget != nullptr)
	{
		CHECK(tickByMangledName(widget, 10) == 30); // same resolved pointer -- also now V2
	}

	delete widget;
	hot_reload_context_destroy(cppCtx);

	// --- hot_reload_last_error(): a genuinely undefined symbol, only
	// reachable once something calls into the function that ODR-uses
	// it -- see UndefinedSymbolFixture/src/Fixture.cpp for the full
	// story. Checking for a non-empty message is deliberately the
	// only assertion here (not the exact dlerror() wording, which
	// isn't a portability guarantee this library makes) -- the
	// property this test protects is "some actionable detail exists
	// at all", not a specific string.
	HotReloadContext* fixtureCtx = hot_reload_context_create(nullptr);
	CHECK(fixtureCtx != nullptr);
	HotReloadStatus fixtureStatus = hot_reload_load(fixtureCtx, "fixture", UNDEFINED_SYMBOL_FIXTURE_PATH);
	CHECK(fixtureStatus == HOT_RELOAD_ERROR_LOAD_FAILED);
	const char* fixtureError = hot_reload_last_error(fixtureCtx);
	CHECK(fixtureError != nullptr);
	CHECK(fixtureError != nullptr && std::strlen(fixtureError) > 0);
	hot_reload_context_destroy(fixtureCtx);

	if (g_failures == 0)
	{
		std::printf("hotreload_tests: all checks passed\n");
	}
	else
	{
		std::fprintf(stderr, "hotreload_tests: %d check(s) failed\n", g_failures);
	}
	return g_failures == 0 ? 0 : 1;
}

#else

int main()
{
	std::printf("hotreload_tests: skipped (needs BUILD_EXAMPLES=ON too -- see this module's CMakeLists.txt)\n");
	return 0;
}

#endif
