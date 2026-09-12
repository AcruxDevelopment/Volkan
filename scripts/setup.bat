@echo off
REM One-time (or re-run any time) environment setup for this template:
REM
REM   1. Fetches the pinned ninja (cmake\FetchNinja.cmake) -- used by
REM      scripts\build.bat if found (see there); a system ninja still
REM      works fine too, this just avoids needing one installed at all.
REM   2. Fetches the pinned Doxygen (cmake\FetchDoxygen.cmake) -- NOT
REM      required to build the project itself (see cmake\Documentation.cmake),
REM      only to run scripts\docs.bat afterward.
REM   3. Checks for clang-format/clang-tidy (used by scripts\refactor.bat and
REM      .githooks\pre-commit) and prints install guidance if either is
REM      missing -- these aren't auto-installed, since doing so would need
REM      assumptions about your package manager/installer this script
REM      shouldn't assume it has.
REM   4. Activates the pre-commit hook (.githooks\pre-commit) for this
REM      clone, if it's a git repository.
REM
REM Usage (from the project root):
REM   scripts\setup.bat
setlocal enabledelayedexpansion

cd /d "%~dp0.."

where cmake >nul 2>nul
if errorlevel 1 (
    echo error: cmake not found on PATH.
    exit /b 1
)

echo ==^> Fetching pinned ninja (cmake\FetchNinja.cmake)
cmake -P cmake\FetchNinja.cmake
if errorlevel 1 exit /b 1

echo.
echo ==^> Fetching pinned Doxygen (cmake\FetchDoxygen.cmake)
cmake -P cmake\FetchDoxygen.cmake
if errorlevel 1 exit /b 1

echo.
echo ==^> Checking for clang-format / clang-tidy
set "_missing=0"

where clang-format >nul 2>nul
if errorlevel 1 (
    echo     clang-format: not found
    set "_missing=1"
) else (
    for /f "delims=" %%V in ('where clang-format') do echo     clang-format: %%V
)

where clang-tidy >nul 2>nul
if errorlevel 1 (
    echo     clang-tidy:   not found
    set "_missing=1"
) else (
    for /f "delims=" %%V in ('where clang-tidy') do echo     clang-tidy:   %%V
)

if "%_missing%"=="1" (
    echo.
    echo     Install the missing tool(s^) with one of:
    echo       winget install LLVM.LLVM
    echo       choco install llvm
    echo       or the installer from https://releases.llvm.org/
    echo     Not required to build this project -- only for scripts\refactor.bat
    echo     and the pre-commit hook.
)

echo.
git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo ==^> Skipping pre-commit hook activation ^(not a git repository yet^)
    echo     Once you've run "git init", re-run this script or:
    echo       git config core.hooksPath .githooks
) else (
    echo ==^> Activating pre-commit hook
    git config core.hooksPath .githooks
    echo     core.hooksPath = .githooks
)

echo.
echo ==^> Setup complete. scripts\build.bat and scripts\docs.bat are ready to use.
exit /b 0
