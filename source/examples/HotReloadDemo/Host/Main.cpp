/* Main.cpp -- loads PluginV1, calls into it, reloads to PluginV2, and
 * calls the SAME already-resolved function pointers again to show
 * they now run the new code -- without the Host re-resolving anything.
 * Along the way this exercises every HotReloadSymbolEvent kind: watch
 * the printed event log against PluginV1/src/Plugin.cpp and
 * PluginV2/src/Plugin.cpp side by side. The second half does the same
 * thing for CppWidgetApi.h's real C++ (non-extern-"C") surface -- see
 * that header's comments for why its two methods are each reached a
 * different way below.
 */
#include "CppWidgetApi.h"
#include "hot_reload/hot_reload.h"

#include <cstdio>
#include <string>

namespace
{

	using PluginFunction = int (*)(int);

	// CppWidget::tick's real ABI: an implicit this-pointer followed by
	// its one declared parameter, same calling convention the
	// compiler itself would generate for widget->tick(x) -- this is
	// just spelling that out explicitly so it can be called through a
	// manually-resolved address instead.
	using CppWidgetTickFunction = int (*)(CppWidget*, int);

	int hostSideDeletedFunctionStub(int)
	{
		std::printf("    !! called a deleted plugin function -- this is the Host's stub, not a crash !!\n");
		return -1;
	}

	/* Runs once per HOT_RELOAD_SYMBOL_REMOVED, before onSymbolEvent
	 * below sees the same event -- see hot_reload.h's doc comment on
	 * HotReloadDeletedSymbolFn. Redirecting a removed function to a
	 * stub living in the HOST executable (not in either plugin build)
	 * demonstrates that the target of a patch can be anywhere at all,
	 * not just inside the module being reloaded. */
	void* onDeletedSymbol(const char* symbolName, void* /*userData*/)
	{
		std::printf("  [event] REMOVED    %-22s -> redirecting old callers to the Host's stub\n", symbolName);
		return reinterpret_cast<void*>(&hostSideDeletedFunctionStub);
	}

	const char* eventKindName(HotReloadSymbolEvent kind)
	{
		switch (kind)
		{
			case HOT_RELOAD_SYMBOL_PATCHED:
				return "PATCHED";
			case HOT_RELOAD_SYMBOL_ADDED:
				return "ADDED";
			case HOT_RELOAD_SYMBOL_REMOVED:
				return "REMOVED";
			case HOT_RELOAD_SYMBOL_SKIPPED_NOT_CODE:
				return "SKIPPED (data, not code)";
			case HOT_RELOAD_SYMBOL_SKIPPED_TOO_SMALL:
				return "SKIPPED (too small to patch)";
			case HOT_RELOAD_SYMBOL_SKIPPED_UNWRITABLE:
				return "SKIPPED (page not writable)";
		}
		return "?";
	}

	void onSymbolEvent(const HotReloadSymbolEventInfo* event, void* /*userData*/)
	{
		// REMOVED is already reported, with more context, by
		// onDeletedSymbol() above -- it fires exactly once per removed
		// symbol, same as this callback would.
		if (event->kind == HOT_RELOAD_SYMBOL_REMOVED)
		{
			return;
		}
		// event->name is always the real symbol-table name (the exact
		// string hot_reload_get_symbol() would need) -- demangling it
		// is purely a display choice, made here and only here; a
		// plain extern "C" name like "plugin_tick" comes back
		// unchanged, so this is safe to apply unconditionally.
		char* readableName = hot_reload_demangle(event->name);
		std::printf("  [event] %-30s %s\n", eventKindName(event->kind), readableName != nullptr ? readableName : event->name);
		hot_reload_free_string(readableName);
	}

}

int main(int argc, char** argv)
{
	std::string pluginV1Path = argc > 1 ? argv[1] : PLUGIN_V1_PATH;
	std::string pluginV2Path = argc > 2 ? argv[2] : PLUGIN_V2_PATH;

	HotReloadOptions options{};
	options.on_symbol_event = &onSymbolEvent;
	options.on_deleted_symbol = &onDeletedSymbol;
	options.user_data = nullptr;

	HotReloadContext* ctx = hot_reload_context_create(&options);
	if (ctx == nullptr)
	{
		std::fprintf(stderr, "hot_reload_context_create failed\n");
		return 1;
	}

	std::printf("== loading plugin V1 (%s) ==\n", pluginV1Path.c_str());
	HotReloadStatus status = hot_reload_load(ctx, "plugin", pluginV1Path.c_str());
	if (status != HOT_RELOAD_OK)
	{
		std::fprintf(stderr, "hot_reload_load failed: %s (%s)\n", hot_reload_status_string(status),
			hot_reload_last_error(ctx));
		hot_reload_context_destroy(ctx);
		return 1;
	}

	// Resolved ONCE, before the reload below -- and never re-resolved.
	// The point of this demo is that these exact pointers keep working
	// correctly after the plugin underneath them changes.
	auto tick = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_tick"));
	auto legacy = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_legacy_feature"));
	auto markerBefore = static_cast<int*>(hot_reload_get_symbol(ctx, "plugin", "plugin_build_marker"));

	std::printf("\n-- calling V1 through freshly-resolved pointers --\n");
	tick(21);
	legacy(7);
	std::printf("    plugin_build_marker = %d\n", markerBefore != nullptr ? *markerBefore : -1);

	// ---------------------------------------------------------------
	// The same plugin build ALSO exports a real C++ class
	// (CppWidgetApi.h) with no extern "C" anywhere -- a virtual
	// method and an ordinary out-of-line method, both with real
	// compiler-mangled names. Same "plugin" module name as above,
	// since it is physically the same shared library on disk.
	// ---------------------------------------------------------------
	std::printf("\n== the same plugin build's C++ surface (CppWidgetApi.h, no extern \"C\") ==\n");

	auto makeCppWidget = reinterpret_cast<CppWidget* (*) ()>(hot_reload_get_symbol(ctx, "plugin", "make_cpp_widget"));
	CppWidget* widget = makeCppWidget != nullptr ? makeCppWidget() : nullptr;

	// CppWidget::tick(int)'s real, compiler-mangled name -- see
	// CppWidgetApi.h's comment on why this one is resolved manually
	// rather than called as widget->tick(x). Re-derive this yourself
	// with `nm -D --defined-only <plugin path> | c++filt` if you
	// change CppWidget's signature; there is no portable way to ask
	// the compiler for another declaration's mangled name at compile
	// time, which is exactly the inconvenience extern "C" exists to
	// avoid for anything you need to look up this way.
	auto tickByMangledName =
		reinterpret_cast<CppWidgetTickFunction>(hot_reload_get_symbol(ctx, "plugin", "_ZN9CppWidget4tickEi"));

	std::printf("-- V1: virtual method through REAL polymorphic dispatch (widget->virtualTick) --\n");
	if (widget != nullptr)
	{
		widget->virtualTick(21);
	}
	std::printf("-- V1: ordinary method through a manually-resolved mangled-name pointer --\n");
	if (tickByMangledName != nullptr && widget != nullptr)
	{
		tickByMangledName(widget, 21);
	}

	std::printf("\n== reloading \"plugin\" to V2 (%s) ==\n", pluginV2Path.c_str());
	status = hot_reload_reload(ctx, "plugin", pluginV2Path.c_str());
	if (status != HOT_RELOAD_OK)
	{
		std::fprintf(stderr, "hot_reload_reload failed: %s (%s)\n", hot_reload_status_string(status),
			hot_reload_last_error(ctx));
		delete widget;
		hot_reload_context_destroy(ctx);
		return 1;
	}

	std::printf("\n-- calling the exact same pointers from before, un-re-resolved --\n");
	tick(21);   // same function pointer as above -- now runs V2's tripling logic
	legacy(7);  // same function pointer as above -- now runs the Host's stub instead of V1's body

	std::printf("\n-- plugin_new_feature only exists in V2, resolved fresh --\n");
	auto newFeature = reinterpret_cast<PluginFunction>(hot_reload_get_symbol(ctx, "plugin", "plugin_new_feature"));
	if (newFeature != nullptr)
	{
		newFeature(21);
	}

	auto markerAfter = static_cast<int*>(hot_reload_get_symbol(ctx, "plugin", "plugin_build_marker"));
	std::printf("\n-- plugin_build_marker: proof that data is never patched --\n");
	std::printf("    resolved fresh from V2 right now       = %d\n", markerAfter != nullptr ? *markerAfter : -1);
	std::printf("    V1's ORIGINAL address, read again       = %d   (still V1's value -- never touched)\n",
		markerBefore != nullptr ? *markerBefore : -1);

	std::printf("\n-- the SAME widget object, same vtable slot, same manually-resolved pointer --\n");
	if (widget != nullptr)
	{
		widget->virtualTick(21); // same object, same vtable slot -- now V2's tripling logic
	}
	if (tickByMangledName != nullptr && widget != nullptr)
	{
		tickByMangledName(widget, 21); // same resolved pointer as before -- also now V2's logic
	}

	delete widget;
	hot_reload_context_destroy(ctx);
	return 0;
}
