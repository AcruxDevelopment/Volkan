/* Main.cpp -- a persistent process, unlike source/examples/
 * HotReloadDemo/Host (which runs a fixed script and exits). This one
 * loads Plugin/src/LivePlugin.cpp's compiled output once, then polls
 * that file's last-write time every second: whenever it changes (i.e.
 * you rebuilt it), it reloads automatically. Run this, then edit and
 * rebuild the plugin in another terminal -- see LivePlugin.cpp's own
 * comment for exactly what to try. Stop with Ctrl+C.
 */
#include "hot_reload/hot_reload.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
namespace
{

	std::atomic<bool> g_running{true};

	void handleSigint(int /*signal*/)
	{
		g_running = false;
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
		if (event->kind == HOT_RELOAD_SYMBOL_REMOVED)
		{
			return; // reported, with more context, by onDeletedSymbol below
		}
		std::printf("    [reload] %-24s %s\n", eventKindName(event->kind), event->name);
	}

	int fallbackForDeletedTick(int frame)
	{
		std::printf(
			"  live_tick was deleted -- this is the Host's fallback (frame %d). Bring it back under the same "
			"name and rebuild to try LivePlugin.cpp's suggestion #2 through to the end.\n",
			frame);
		return -1;
	}

	void* onDeletedSymbol(const char* symbolName, void* /*userData*/)
	{
		std::printf("    [reload] REMOVED                 %s -> falling back to the Host's stub\n", symbolName);
		return reinterpret_cast<void*>(&fallbackForDeletedTick);
	}

}

int main(int argc, char** argv)
{
	// Force line buffering regardless of whether stdout is a real
	// terminal: this is a "watch it happen live" demo, and glibc (and
	// most libc implementations) fully buffer stdout instead of
	// line-buffering it the moment it's not connected to a TTY -- a
	// redirect to a log file, a `| tee`, or some IDEs' embedded
	// terminals all trigger this. Without this call, output can sit
	// in the buffer for a long time (observed directly while building
	// this: zero output appeared for several seconds of real,
	// correctly-running execution until the buffer was force-flushed).
	std::setvbuf(stdout, nullptr, _IOLBF, 4096);

	std::string pluginPath = argc > 1 ? argv[1] : LIVE_PLUGIN_PATH;

	std::printf("================================================================\n");
	std::printf(" Hot Reload -- live demo\n");
	std::printf("================================================================\n");
	std::printf(" Watching:  %s\n", pluginPath.c_str());
	std::printf(" Edit:      source/examples/HotReloadLiveDemo/Plugin/src/LivePlugin.cpp\n");
	std::printf(" Rebuild:   cmake --build <your build dir> --target hotreload_live_plugin\n");
	std::printf("            (or run watch.sh, in this example's own directory, in another\n");
	std::printf("             terminal to rebuild automatically every time you save)\n");
	std::printf(" Stop:      Ctrl+C\n");
	std::printf("================================================================\n\n");

	std::signal(SIGINT, &handleSigint);

	HotReloadOptions options{};
	options.on_symbol_event = &onSymbolEvent;
	options.on_deleted_symbol = &onDeletedSymbol;

	HotReloadContext* ctx = hot_reload_context_create(&options);
	if (ctx == nullptr)
	{
		std::fprintf(stderr, "hot_reload_context_create failed\n");
		return 1;
	}

	if (hot_reload_load(ctx, "live", pluginPath.c_str()) != HOT_RELOAD_OK)
	{
		std::fprintf(stderr,
			"Could not load '%s': %s\n"
			"(if the file exists but this still fails, that's usually a genuinely undefined symbol in the "
			"plugin -- e.g. a non-inline static class member that's declared but never defined, only caught "
			"once something actually calls into the code path that uses it; the message above names it)\n",
			pluginPath.c_str(), hot_reload_last_error(ctx));
		hot_reload_context_destroy(ctx);
		return 1;
	}

	// Resolved ONCE, here, and deliberately never re-resolved below --
	// the whole point of this demo is watching this same pointer keep
	// working, correctly, across as many reloads as you make. The one
	// case where that stops being true: if you delete live_tick (see
	// LivePlugin.cpp suggestion #2) and later re-add it under the same
	// name, the re-added one is a NEW, unrelated symbol as far as
	// diffing is concerned -- there is no "old" address to patch for
	// it, so it comes back as HOT_RELOAD_SYMBOL_ADDED, not PATCHED, and
	// this specific pointer (already redirected to the fallback below)
	// keeps running that fallback rather than picking the new one back
	// up automatically. Restart the Host to resolve it fresh, or call
	// hot_reload_get_symbol() again yourself, if you want to see that.
	using TickFunction = int (*)(int);
	auto tick = reinterpret_cast<TickFunction>(hot_reload_get_symbol(ctx, "live", "live_tick"));

	std::error_code timeStatus;
	auto lastWriteTime = std::filesystem::last_write_time(pluginPath, timeStatus);

	int frame = 0;
	while (g_running)
	{
		std::error_code currentTimeStatus;
		auto currentWriteTime = std::filesystem::last_write_time(pluginPath, currentTimeStatus);
		if (!currentTimeStatus && currentWriteTime != lastWriteTime)
		{
			// A short settle delay for a linker that might still be
			// mid-write on a slower filesystem -- not load-bearing for
			// correctness either way: a reload attempted too early
			// just fails cleanly (hot_reload_reload() never disturbs
			// the running version on failure) and gets retried next
			// iteration, since lastWriteTime below is only updated on
			// success.
			std::this_thread::sleep_for(std::chrono::milliseconds(150));

			std::printf("\n-- change detected in %s, reloading --\n", pluginPath.c_str());
			HotReloadStatus status = hot_reload_reload(ctx, "live", pluginPath.c_str());
			if (status == HOT_RELOAD_OK)
			{
				lastWriteTime = currentWriteTime;
			}
			else
			{
				std::printf("-- reload failed: %s -- still running the previous version; fix and save again --\n",
					hot_reload_last_error(ctx));
			}
			std::printf("\n");
		}

		if (tick != nullptr)
		{
			tick(frame);
		}
		else
		{
			std::printf("  (live_tick not found -- build the plugin; see instructions above)\n");
		}

		// Unlike tick above, deliberately re-resolved every frame:
		// live_extra is not expected to exist until you add it (see
		// LivePlugin.cpp suggestion #3), so the Host has to keep
		// checking rather than assume it already knows the answer.
		// This is a cheap lookup over a handful of symbols, not a
		// reload -- fine to do every frame.
		using ExtraFunction = int (*)(int);
		auto extra = reinterpret_cast<ExtraFunction>(hot_reload_get_symbol(ctx, "live", "live_extra"));
		if (extra != nullptr)
		{
			extra(frame);
		}

		++frame;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	std::printf("\nShutting down.\n");
	hot_reload_context_destroy(ctx);
	return 0;
}
