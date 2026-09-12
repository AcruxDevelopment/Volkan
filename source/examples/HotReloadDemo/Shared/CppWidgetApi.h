/* CppWidgetApi.h -- a C++ (not extern "C") surface living alongside
 * PluginApi.h's plain C one, in the SAME plugin build. Every member
 * function below keeps its real, compiler-mangled name; nothing here
 * needs special handling by source/HotReload, which classifies
 * symbols by their type and section, never by what their name looks
 * like -- see hot_reload.h's "REQUIREMENTS ON A HOT-RELOADABLE
 * MODULE" and README.md's "Hot reloading C++ symbols" for the full
 * picture.
 *
 * The two methods below are reached two different ways from
 * Host/Main.cpp, deliberately: see that file's comments for why.
 */
#pragma once

class CppWidget
{
public:
	virtual ~CppWidget() = default;

	/* Virtual: Host/Main.cpp calls this through ordinary C++
	 * polymorphic dispatch (widget->virtualTick(x)) and needs no
	 * manual symbol lookup at all -- the call compiles down to an
	 * indirect call through the object's vtable slot, not a direct
	 * reference to this specific symbol, so it needs no link-time
	 * dependency on whichever plugin build actually defines it. */
	virtual int virtualTick(int input);

	/* An ordinary out-of-line method. Calling this as widget->tick(x)
	 * would compile to a direct call to CppWidget::tick's own mangled
	 * symbol, which would need the Host to link against a plugin
	 * build at build time -- exactly what hot-reloading a plugin
	 * means avoiding. So Host/Main.cpp instead resolves it manually
	 * by its mangled name via hot_reload_get_symbol(), the same way
	 * it resolves PluginApi.h's plain C functions. */
	int tick(int input);

	/* An overloaded operator: mechanically just a function with an
	 * unusual mangled name, reached the same manual way as tick()
	 * above. */
	CppWidget operator+(const CppWidget& other) const;
};

/* The one entry point Host/Main.cpp looks up by a plain, human-typed
 * name -- extern "C" here for exactly the reason hot_reload.h
 * recommends it: something to hand a human-readable name to, without
 * needing to know or hardcode a mangled string. */
extern "C" CppWidget* make_cpp_widget();
