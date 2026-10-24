@echo off
rem APImageIO regression harness: generate test files, build, run.
rem Needs Python with: pip install OpenEXR numpy pypng
rem Exit code = number of failed checks (0 = all passed).
setlocal
cd /d "%~dp0"
set V=C:\vcpkg\installed\x64-windows-static
set SRC=%~dp0..\src

python mk_kra.py || exit /b 90
python mk_exr.py || exit /b 91

call "E:\Microsoft\Visual Studio IDE\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build
cl /nologo /O2 /MT /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /I"%SRC%" /I"%V%\include" /I"%V%\include\OpenEXR" /I"%V%\include\Imath" ^
   qa_harness.c "%SRC%\exrbridge.cpp" "%SRC%\svgbridge.cpp" "%SRC%\krabridge.c" "%SRC%\crypto_encode.c" "%SRC%\exr_session.c" ^
   /Fo:build\ /Fe:build\qa_harness.exe ^
   /link /LIBPATH:"%V%\lib" OpenEXR-3_4.lib OpenEXRCore-3_4.lib Imath-3_2.lib Iex-3_4.lib IlmThread-3_4.lib ^
   deflatestatic.lib openjph.0.28.lib lunasvg.lib plutovg.lib minizip-ng.lib lodepng-c.lib zs.lib zstd.lib lzma.lib bz2.lib ^
   bcrypt.lib ncrypt.lib crypt32.lib advapi32.lib psapi.lib >build\build.log
if errorlevel 1 ( type build\build.log & exit /b 92 )

rem OpenEXR prints its own diagnostics for the deliberately corrupt file.
build\qa_harness.exe 2>nul
exit /b %errorlevel%
