#include "hot_reload/hot_reload.h"

#include "../HotReloadContext.hpp"

namespace
{
	/// The opaque ::HotReloadContext the public header declares is
	/// never actually instantiated as itself -- every instance really
	/// is a hot_reload::HotReloadContext, following the same
	/// opaque-pointer PIMPL pattern cpp-style-guide.md's NetManager
	/// example uses. This is the one place that cast happens.
	hot_reload::HotReloadContext* impl(::HotReloadContext* ctx)
	{
		return reinterpret_cast<hot_reload::HotReloadContext*>(ctx);
	}
}

// Per cpp-style-guide.md section 11: a C++ exception must never cross
// an extern "C" boundary -- doing so is undefined behavior for any
// caller that is not itself compiled to expect one, which a plain C
// caller of this API never is. Every function here is a thin,
// non-throwing wrapper for exactly that reason: std::string/vector/
// unordered_map construction is the only thing in hot_reload::
// HotReloadContext that could throw at all (std::bad_alloc under
// memory pressure), so catching (...) and mapping it to this API's
// existing failure values is sufficient without needing per-exception-
// type handling.
extern "C"
{

	HotReloadContext* hot_reload_context_create(const HotReloadOptions* options)
	{
		try
		{
			return reinterpret_cast<::HotReloadContext*>(new hot_reload::HotReloadContext(options));
		}
		catch (...)
		{
			return nullptr;
		}
	}

	void hot_reload_context_destroy(HotReloadContext* ctx)
	{
		delete impl(ctx);
	}

	HotReloadStatus hot_reload_load(HotReloadContext* ctx, const char* module_name, const char* path)
	{
		if (ctx == nullptr || module_name == nullptr || path == nullptr)
		{
			return HOT_RELOAD_ERROR_INVALID_ARGUMENT;
		}
		try
		{
			return impl(ctx)->load(module_name, path);
		}
		catch (...)
		{
			return HOT_RELOAD_ERROR_LOAD_FAILED;
		}
	}

	HotReloadStatus hot_reload_reload(HotReloadContext* ctx, const char* module_name, const char* new_path)
	{
		if (ctx == nullptr || module_name == nullptr || new_path == nullptr)
		{
			return HOT_RELOAD_ERROR_INVALID_ARGUMENT;
		}
		try
		{
			return impl(ctx)->reload(module_name, new_path);
		}
		catch (...)
		{
			return HOT_RELOAD_ERROR_LOAD_FAILED;
		}
	}

	void* hot_reload_get_symbol(HotReloadContext* ctx, const char* module_name, const char* symbol_name)
	{
		if (ctx == nullptr || module_name == nullptr || symbol_name == nullptr)
		{
			return nullptr;
		}
		try
		{
			return impl(ctx)->getSymbol(module_name, symbol_name);
		}
		catch (...)
		{
			return nullptr;
		}
	}

	const char* hot_reload_last_error(HotReloadContext* ctx)
	{
		if (ctx == nullptr)
		{
			return "";
		}
		try
		{
			return impl(ctx)->lastError().c_str();
		}
		catch (...)
		{
			return "";
		}
	}

	const char* hot_reload_status_string(HotReloadStatus status)
	{
		switch (status)
		{
			case HOT_RELOAD_OK:
				return "ok";
			case HOT_RELOAD_ERROR_INVALID_ARGUMENT:
				return "invalid argument";
			case HOT_RELOAD_ERROR_LOAD_FAILED:
				return "load failed";
			case HOT_RELOAD_ERROR_NO_PREVIOUS_VERSION:
				return "no previous version loaded for this module name";
			case HOT_RELOAD_ERROR_UNSUPPORTED_ARCHITECTURE:
				return "no patch backend for this CPU architecture";
		}
		return "unknown hot reload status";
	}

}
