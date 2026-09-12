@echo off
REM Applies clang-format and clang-tidy's --fix across this project's own
REM C/C++ code. The pre-commit hook only CHECKS; this is the "just fix it"
REM counterpart -- review the result with `git diff` afterward, same as any
REM auto-formatter.
REM
REM Defaults to source\ and c-api\, this template's own-code roots -- NEVER
REM walks out\, .cache\, vendor\, or .git\ by default, so it can't
REM accidentally spend minutes reformatting/analyzing a fetched dependency's
REM entire source tree the way scanning the whole repo from "." would.
REM
REM Usage, from the project root: scripts\refactor.bat [optional path]
REM   scripts\refactor.bat              every file under source\ and c-api\
REM   scripts\refactor.bat source\Core  just one file or directory -- taken
REM                                     as given, with no exclusion filter,
REM                                     so pick a path that doesn't overlap
REM                                     with out\/.cache\/vendor\/.git\
setlocal enabledelayedexpansion

cd /d "%~dp0.."

where clang-format >nul 2>nul
set "HAVE_FORMAT=0"
if not errorlevel 1 set "HAVE_FORMAT=1"
where clang-tidy >nul 2>nul
set "HAVE_TIDY=0"
if not errorlevel 1 set "HAVE_TIDY=1"

if "%HAVE_FORMAT%"=="1" goto have_a_tool
if "%HAVE_TIDY%"=="1" goto have_a_tool
echo error: neither clang-format nor clang-tidy found on PATH. Run scripts\setup.bat for install guidance.
exit /b 1
:have_a_tool

set "FILELIST=%TEMP%\refactor_files_%RANDOM%.txt"
if exist "%FILELIST%" del "%FILELIST%"

REM Collect the file list. Deliberately NOT using an if/else block here --
REM a for /f or for /r loop nested inside a parenthesized if(...) else(...)
REM block confuses cmd.exe's paren-matching for the enclosing block (this
REM was tested and confirmed, not a guess); goto-based branching sidesteps
REM the problem entirely by never nesting a FOR loop inside another block.
if not "%~1"=="" goto collect_custom

set "SCOPE_DESC=source and c-api"
if exist "source" for /r "source" %%F in (*.c *.h *.cpp *.hpp *.cc *.cxx *.tpp) do echo %%F>>"%FILELIST%"
if exist "c-api" for /r "c-api" %%F in (*.c *.h *.cpp *.hpp *.cc *.cxx *.tpp) do echo %%F>>"%FILELIST%"
goto collected

:collect_custom
set "SCOPE_DESC=%~1"
if not exist "%~1" goto collected
for /r "%~1" %%F in (*.c *.h *.cpp *.hpp *.cc *.cxx *.tpp) do echo %%F>>"%FILELIST%"

:collected
if exist "%FILELIST%" goto have_files
echo No C/C++ files found under: %SCOPE_DESC%
exit /b 0
:have_files

echo ==^> Files in scope: %SCOPE_DESC%

if "%HAVE_FORMAT%"=="1" goto do_format
echo ==^> Skipping clang-format ^(not found on PATH -- see scripts\setup.bat^)
goto after_format
:do_format
echo ==^> Running clang-format -i
for /f "usebackq delims=" %%F in ("%FILELIST%") do clang-format -i "%%F"
:after_format

if "%HAVE_TIDY%"=="1" goto do_tidy
echo ==^> Skipping clang-tidy ^(not found on PATH -- see scripts\setup.bat^)
goto after_tidy
:do_tidy
if exist compile_commands.json goto run_tidy
echo ==^> Skipping clang-tidy ^(no compile_commands.json -- run scripts\build.bat first^)
goto after_tidy
:run_tidy
echo ==^> Running clang-tidy --fix ^(auto-fixable issues only; build once first if this is stale^)
for /f "usebackq delims=" %%F in ("%FILELIST%") do clang-tidy --quiet --fix "%%F"
:after_tidy

del "%FILELIST%" 2>nul
echo ==^> Done. Review the changes: git diff
exit /b 0
