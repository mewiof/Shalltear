@echo off
setlocal enableextensions

rem one command

set "ROOT=%~dp0"
set "VERB=%~1"
if "%VERB%"=="" set "VERB=editor"

rem set SHALLTEAR_NO_PAUSE=1 to avoid waiting for a key
set "HOLD_WINDOW="
if not defined SHALLTEAR_NO_PAUSE echo %CMDCMDLINE% | findstr /i /c:"%~nx0" >nul 2>&1 && set "HOLD_WINDOW=1"

if /i "%VERB%"=="help"   goto :usage
if /i "%VERB%"=="/?"     goto :usage
if /i "%VERB%"=="-h"     goto :usage
if /i "%VERB%"=="--help" goto :usage
if /i "%VERB%"=="clean"  goto :clean

rem findstr because Git Bash puts own find ahead on PATH

rem verb is checked before setup steps + their downloads
echo [editor][tests][run][sdk][shell]| findstr /i /c:"[%VERB%]" >nul || (
	echo.
	echo   "%VERB%" is not something this script knows how to build.
	goto :usage
)

call :find_python || goto :failed
call :submodules  || goto :failed
call :prebuilts   || goto :failed
call :put_on_path || goto :failed

if /i "%VERB%"=="shell" goto :shell
if /i "%VERB%"=="sdk"   goto :sdk
if /i "%VERB%"=="run"   goto :run
goto :editor

:find_python
rem Store stub opens the Store (check runs python)
py -3 -c "import sys" >nul 2>&1 && set "PY=py -3" && goto :found_python
python -c "import sys" >nul 2>&1 && set "PY=python" && goto :found_python
echo.
echo   Python 3 was not found on PATH.
echo   Install from https://www.python.org/downloads/ and tick
echo   "Add python.exe to PATH" -^> then run this again.
exit /b 1
:found_python
echo [1/4] python      %PY%
exit /b 0

:submodules
if not exist "%ROOT%.git" (
	echo [2/4] submodules  SKIPPED: not a git checkout
	echo.
	echo   Downloading the ZIP off GitHub doesnt bring submodules with
	echo   it, the engine doesn't build without them. Clone instead:
	echo     git clone --recursive https://github.com/mewiof/Shalltear.git
	exit /b 1
)
git --version >nul 2>&1 || (
	echo   git was not found on PATH ^(submodules cannot be fetched^).
	exit /b 1
)
echo [2/4] submodules  googletest, Vulkan-Headers, VMA, freetype, ufbx, box3d
git -C "%ROOT%." submodule update --init --recursive || exit /b 1
exit /b 0

:prebuilts
rem both skip once pinned ver is staged
echo [3/4] prebuilts   dxc, llvm-mingw, cmake, ninja
%PY% "%ROOT%Scripts\fetch_prebuilt.py" --toolchain || exit /b 1
exit /b 0

:put_on_path
set "PATH=%ROOT%External\Prebuilt\llvm-mingw\bin;%ROOT%External\Prebuilt\cmake\bin;%ROOT%External\Prebuilt\ninja;%PATH%"
for /f "delims=" %%i in ('where x86_64-w64-mingw32-clang++ 2^>nul') do set "CXX_FOUND=%%i" & goto :have_compiler
echo   compiler is still not on PATH after the fetch -^> expected it at
echo   %ROOT%External\Prebuilt\llvm-mingw\bin
exit /b 1
:have_compiler
echo [4/4] toolchain   %CXX_FOUND%
echo.
exit /b 0

:editor
echo ==== Configuring build\ ====
rem arguments are a variable because cache reset reruns without them
set "CFG=-S "%ROOT%." -B "%ROOT%build" -G Ninja -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang -DCMAKE_BUILD_TYPE=Debug"
cmake %CFG% || goto :failed
findstr /b /c:"CMAKE_BUILD_TYPE:STRING=Debug" "%ROOT%build\CMakeCache.txt" >nul || (
	echo.
	echo   ==== build\ was configured for another toolchain ^(applying Debug again^) ====
	cmake %CFG% || goto :failed
)
echo.
echo ==== Building ====
rem running editor/orphaned test that holds the DLL will fail the link
taskkill /IM Shalltear.exe /F >nul 2>&1
taskkill /IM ShalltearTests.exe /F >nul 2>&1
cmake --build "%ROOT%build" --parallel || goto :failed
echo.
echo   Editor:  %ROOT%build\bin\Shalltear.exe
echo   Run it:  Build.bat run
if /i "%VERB%"=="tests" goto :tests
goto :done

:tests
echo.
echo ==== Tests ====
rem run from bin, not ctest
pushd "%ROOT%build\bin" || goto :failed
rem working directory matters, executable should keep full path
"%ROOT%build\bin\ShalltearTests.exe"
set "TEST_CODE=%ERRORLEVEL%"
popd
if not "%TEST_CODE%"=="0" (
	echo.
	echo   the suite exited %TEST_CODE%
	goto :failed
)
goto :done

:run
if not exist "%ROOT%build\bin\Shalltear.exe" (
	echo   build\bin\Shalltear.exe doesn't exist yet -^> run Build.bat.
	goto :failed
)
rem launched with toolchain on PATH so the editor can compile C++
echo ==== Launching the editor ====
start "" "%ROOT%build\bin\Shalltear.exe" %2 %3 %4 %5
goto :done

:sdk
echo ==== Packaging the SDK ====
%PY% "%ROOT%Scripts\build_sdk.py" || goto :failed
goto :done

:shell
echo   Pinned toolchain is on PATH for this shell. cmake, ninja and
echo   x86_64-w64-mingw32-clang++ resolve to External\Prebuilt\.
echo   Type `exit` to leave it.
echo.
cmd /k cd /d "%ROOT%"
goto :done

:clean
echo   deleting %ROOT%build
echo   ...including the SDK trees build\SHARED and build\STATIC (if they exist).
rem choice returns the key's index (y is errorlevel 1)
choice /c yn /n /m "   continue? [y/n] "
if errorlevel 2 (
	echo   nothing deleted.
	goto :done
)
if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
echo   gone. Run Build.bat to configure/build again.
goto :done

:usage
echo.
echo   Build.bat            build the editor into build\ ^(Debug^)
echo   Build.bat tests      ...run the test suite
echo   Build.bat run        launch the editor
echo   Build.bat sdk        package ShalltearSDK\ ^(Release, shared + static^)
echo   Build.bat shell      shell with pinned toolchain on PATH
echo   Build.bat clean      delete build\, start over
echo.
echo   README.md has the same steps
goto :done

:failed
echo.
echo   BUILD FAILED.
if defined HOLD_WINDOW pause
exit /b 1

:done
if defined HOLD_WINDOW pause
exit /b 0
