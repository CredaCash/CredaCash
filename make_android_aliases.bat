@echo off
rmdir/s/q aliases
mkdir aliases
cd aliases
if "%NDKGCC%"=="1" (
mklink %1gcc.exe %NDKPATH%\bin\%NDKTARGET%-gcc.exe
mklink %1g++.exe %NDKPATH%\bin\%NDKTARGET%-g++.exe
mklink %1ld.exe %NDKPATH%\bin\%NDKTARGET%-ld.gold.exe
mklink %1ar.exe %NDKPATH%\bin\%NDKTARGET%-ar.exe
mklink %1nm.exe %NDKPATH%\bin\%NDKTARGET%-nm.exe
mklink %1ranlib.exe %NDKPATH%\bin\%NDKTARGET%-ranlib.exe
mklink %1strip.exe %NDKPATH%\bin\%NDKTARGET%-strip.exe
) else (
mklink %NDKTARGET%%NDKminSdkVersion%-%1clang.exe %NDKPATH%\bin\clang.exe
mklink %NDKTARGET%%NDKminSdkVersion%-%1clang++.exe %NDKPATH%\bin\clang++.exe
mklink %NDKTARGET%%NDKminSdkVersion%-%1gcc.exe %NDKPATH%\bin\clang.exe
mklink %NDKTARGET%%NDKminSdkVersion%-%1g++.exe %NDKPATH%\bin\clang++.exe
mklink %1clang.exe %NDKPATH%\bin\clang.exe
mklink %1clang++.exe %NDKPATH%\bin\clang++.exe
mklink %1gcc.exe %NDKPATH%\bin\clang.exe
mklink %1g++.exe %NDKPATH%\bin\clang++.exe
mklink %1yasm.exe %NDKPATH%\bin\yasm.exe
mklink %1ld.exe %NDKPATH%\bin\ld.exe
mklink %1ar.exe %NDKPATH%\bin\llvm-ar.exe
mklink %1nm.exe %NDKPATH%\bin\llvm-nm.exe
mklink %1ranlib.exe %NDKPATH%\bin\llvm-ranlib.exe
mklink %1strip.exe %NDKPATH%\bin\llvm-strip.exe
)
cd ..
