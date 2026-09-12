@echo off
REM Configure (if needed) and build the project, preferably with Ninja.
REM
REM Usage (from the project root):
REM   scripts\build.bat [BUILD_TYPE]
REM
REM   BUILD_TYPE  Debug^|Release^|RelWithDebInfo^|MinSizeRel (default: Release)
REM
REM Set BUILD_DIR to build somewhere other than out\build (see Configuration.cmake).
setlocal enabledelayedexpansion

cd /d "%~dp0.."

if "%BUILD_DIR%"=="" set "BUILD_DIR=out\build"
set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"
if "%TOOLCACHE_DIR%"=="" set "TOOLCACHE_DIR=.cache\tools"

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
cmake %GENERATOR_ARGS% -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

echo ==^> Building
cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

echo ==^> Done. Run a module with:  scripts\run.bat ^<target-name^>
exit /b 0
