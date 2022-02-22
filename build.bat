@REM Build for Visual Studio compiler. vcvarsall.bat must be in path.
@if "%~1"=="" (set arg1=x86) else (set arg1=%1)
@call vcvarsall %arg1%
if %arg1%==x86 (set ext=32) else (set ext=64)
@set OUT_DIR=output
@set OUT_EXE=aptcontroller
@set INCLUDES=/I .\include /I imgui\include /I "C:\Program Files\Thorlabs\APT\APT Server" /I "%DXSDK_DIR%/Include"
@set SOURCES=main.cpp
@set LIBS=/LIBPATH:"%DXSDK_DIR%/Lib/%arg1%" /LIBPATH:"C:\Program Files\Thorlabs\APT\APT Server" d3d9.lib imgui\win32_lib\libimgui_win%ext%.lib
mkdir %OUT_DIR%
cl /nologo /Zi /MD /EHsc /wd4005 %INCLUDES% /D UNICODE /D _UNICODE %SOURCES% /Fe%OUT_DIR%/%OUT_EXE%.exe /Fo%OUT_DIR%/ /link %LIBS%