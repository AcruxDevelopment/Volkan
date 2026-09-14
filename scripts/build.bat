@echo off
REM Configure (if needed) and build the project, preferably with Ninja.
REM
REM Usage (from the project root):
REM   scripts\build.bat [BUILD_TYPE] [extra cmake -D args...]
REM
REM   BUILD_TYPE  Debug^|Release^|RelWithDebInfo^|MinSizeRel (default: Release)
REM
REM Anything after BUILD_TYPE is passed straight through to cmake's
REM configure step, e.g.:
REM   scripts\build.bat Debug -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
REM CMake caches -D options across reconfigures, so passing them once and
REM then calling scripts\build.bat plain afterward for incremental builds
REM keeps them set -- no need to repeat them on every call.
REM
REM Set BUILD_DIR to build somewhere other than out\build (see Configuration.cmake).
setlocal enabledelayedexpansion

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"
if "%TOOLCACHE_DIR%"=="" set "TOOLCACHE_DIR=.cache\tools"

REM Deliberately NOT %1/%2/... for this: cmd.exe's own parameter
REM parser treats = (along with , and ;) as an argument separator, so
REM "-DBUILD_EXAMPLES=ON" typed as one argument arrives as %2=
REM "-DBUILD_EXAMPLES" and %3="ON" -- two arguments, with the = gone --
REM before this script ever sees it, corrupting any -D value passed
REM this way and silently breaking the configure step below. %* does
REM not go through that same parser and reflects the command line
REM as typed, and FOR /F's own tokenizer only splits on space/tab, so
REM routing %* through "tokens=1,*" correctly separates just the
REM first word from everything after it, = signs and all -- verified
REM directly (via Wine's cmd.exe) against exactly the failure the
REM old %2 %3 %4... version had, not just reasoned through.
set "BUILD_TYPE=Release"
set "EXTRA_CMAKE_ARGS="
for /f "tokens=1,*" %%A in ("%*") do (
    set "BUILD_TYPE=%%A"
    set "EXTRA_CMAKE_ARGS=%%B"
)
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

where cmake >nul 2>nul
if errorlevel 1 (
    echo error: cmake not found on PATH.
    exit /b 1
)

REM Prefer a pinned ninja (fetched by scripts\setup.bat -- see
REM cmake\FetchNinja.cmake) over a system one, same "always the pinned
REM copy" preference as Doxygen. Deliberately goto-based, and deliberately
REM `pushd` + a bare wildcard rather than `for /d %%D in ("path\*")` --
REM both tested directly; a path-prefixed wildcard didn't match here,
REM this form does, and it also sidesteps nesting a FOR loop inside an
REM if/else block, which separately corrupts cmd.exe's paren-matching for
REM the enclosing block (see scripts\refactor.bat for that one).
set "NINJA_BIN="
if not exist "%TOOLCACHE_DIR%" goto check_system_ninja

pushd "%TOOLCACHE_DIR%"
for /d %%D in (ninja-*) do if exist "%%D\ninja.exe" if not defined NINJA_BIN set "NINJA_BIN=%%~fD\ninja.exe"
popd

if defined NINJA_BIN goto have_pinned_ninja

:check_system_ninja
where ninja >nul 2>nul
if errorlevel 1 goto no_ninja
echo ==^> Using system ninja
set "GENERATOR_ARGS=-G Ninja"
goto configure

:have_pinned_ninja
echo ==^> Using pinned ninja: %NINJA_BIN%
set "GENERATOR_ARGS=-G Ninja -DCMAKE_MAKE_PROGRAM=%NINJA_BIN%"
goto configure

:no_ninja
echo warning: ninja not found ^(no pinned copy, none on PATH^) -- falling back to
echo          CMake's default generator for this platform. Run scripts\setup.bat
echo          to fetch a pinned ninja, or install ninja yourself, for a faster
echo          and more consistent build. Continuing without it.
set "GENERATOR_ARGS="

:configure
echo ==^> Configuring (%BUILD_TYPE%) into %BUILD_DIR%\
cmake %GENERATOR_ARGS% -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %EXTRA_CMAKE_ARGS%
if errorlevel 1 exit /b 1

echo ==^> Building
cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo ==^> Done. Run a module with:  scripts\run.bat ^<target-name^>
exit /b 0
