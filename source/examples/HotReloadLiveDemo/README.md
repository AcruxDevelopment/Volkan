# Hot Reload -- live demo

Unlike `source/examples/HotReloadDemo` (a fixed script that loads two
pre-built versions and exits), this one is interactive: a host that
keeps running, and one plugin file you edit and rebuild yourself,
however many times you like, while it's live.

## Run it

```sh
cmake -S . -B out/build -DBUILD_EXAMPLES=ON
cmake --build out/build
./out/build/bin/examples/hotreload_live_demo_host
```

Leave that running. In another terminal, either rebuild by hand after
each edit:

```sh
cmake --build out/build --target hotreload_live_plugin
```

or run the watcher in this directory so saving is all you need to do:

```sh
source/examples/HotReloadLiveDemo/watch.sh out/build
```

Then open `Plugin/src/LivePlugin.cpp` -- it has its own comment with
several concrete things worth trying, in order (change a value, delete
the function, add a new one, add a data symbol). Each one lines up
with something `source/HotReload/README.md`'s "Hot reloading C++
symbols" and "The two guarantees" sections describe -- this is a place
to go watch each of those claims happen, not just read about them.

## What you should see

Editing and saving `live_tick()`'s multiplier and rebuilding should
change the running Host's printed values within about a second, on
the frame count it was already at -- not a restart. This was verified
in exactly this shape while building the feature: host running,
`watch.sh` running, and nothing else done except editing the file and
saving -- the running process picked up the new multiplier on its own
a couple of seconds later.

## If a reload doesn't seem to happen

- Check the target actually rebuilt -- `watch.sh` prints a build
  failure clearly rather than silently doing nothing; a plain compile
  error leaves the previous `.so`/`.dll` completely untouched, which
  is why the Host would keep running old code without any error of
  its own in that case.
- The Host polls once a second (see `Host/Main.cpp`) -- give it a
  moment.
- Confirm you rebuilt the target this example actually watches,
  `hotreload_live_plugin`, not the whole project (rebuilding
  everything works too, it's just slower per iteration).

## Why rebuilding a `.so`/`.dll` the Host already has loaded works at all

On Linux/macOS this is unremarkable -- a running process's existing
mapping of a file survives the file being replaced, which is exactly
this project's normal build-then-relink cycle.

On Windows it's normally *not* possible to overwrite or delete a DLL
file a running process has loaded, which would otherwise make this
exact workflow (keep the Host running, rebuild the DLL it already
loaded) fail with the linker unable to write its output. This example
avoids that for a reason that already exists elsewhere in this
library, not anything specific to this demo:
`HotReloadContext::stagePathForLoad()` (`source/HotReload/src/
HotReloadContext.cpp`) copies a plugin to a fresh, uniquely-named file
before ever calling `LoadLibrary`/`dlopen` on it -- built originally to
sidestep a dlopen caching gotcha on POSIX (see that function's
comment) -- so the path this example's build actually writes to is
never the one Windows locks. Confirmed directly on real Windows/MSVC:
rebuilding `hotreload_live_plugin` repeatedly while the Host stayed
running worked correctly, hot-reloading each time.

## "Rebuild Solution" (or any other full clean) fails while the Host is running -- this is expected

Different from the DLL case just above, and this one genuinely can't
be worked around the same way. Visual Studio's "Rebuild" (and the
equivalent `cmake --build --clean-first`, or deleting the build
directory yourself) deletes *every* build output first, including
`hotreload_live_demo_host.exe` itself -- and Windows will not let you
delete a running process's own executable image, full stop, for as
long as it's running. There's no staging trick that helps here the way
there is for the plugin DLL: the thing being deleted is quite literally
the file currently executing.

Confirmed directly: this fails with `ninja: error: remove(bin/
examples/hotreload_live_demo_host.exe): Access is denied.`, and the
"Rebuild failed" it's reported as makes it look worse than it is --
depending on your build tool, the *subsequent* build step can still
run and still correctly rebuild+reload the plugin regardless (also
confirmed directly), since only the clean step, not the whole
operation, actually failed.

**The fix is simply not to use "Rebuild"/a full clean while the Host
is running** -- an ordinary incremental "Build", or building just
`hotreload_live_plugin` specifically (exactly what `watch.sh` and the
manual `cmake --build ... --target hotreload_live_plugin` command
above already do), never touches the Host's own executable at all, so
neither is affected by this. Stop the Host first if you ever do want a
genuine full clean.

## Leftover `hotreload_live_plugin.dll.hotreload.live.N` files

Each reload stages the plugin through a fresh, uniquely-named copy
before loading it (`HotReloadContext::stagePathForLoad()`, needed for
reasons unrelated to this -- see that function's comment). On Linux/
macOS, the copy is deleted right after loading and nothing
accumulates. On Windows, that deletion fails for as long as the module
it backs stays loaded -- which, by this library's design, is for the
rest of the Host process's lifetime, so **one leftover file per reload
during a long editing session is expected**, not a leak to chase down.
`hot_reload_load()` sweeps up whatever a *previous* run of the same
Host left behind the next time it starts, so restarting the Host is
enough to clean up between sessions; within a single very long
session, some accumulation is simply the cost of every old version
staying safely addressable. Safe to delete by hand at any point the
Host isn't running, if it bothers you before then.
