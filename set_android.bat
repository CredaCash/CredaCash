set cc_cpu=%1
if "%cc_cpu%" == "" set cc_cpu=arm

if "%cc_cpu%" == "arm" (
	set NDKTARGET=aarch64-linux-android
) else if "%cc_cpu%" == "x64" (
	set NDKTARGET=x86_64-linux-android
) else exit/b

set NDKVERSION=28.2.13676358
set NDKminSdkVersion=21

set NDKPREFIX=%LOCALAPPDATA%\Android\Sdk\ndk\%NDKVERSION%
set NDKPATH=%NDKPREFIX%\toolchains\llvm\prebuilt\windows-x86_64

if not exist "%NDKPREFIX%" (
	echo Directory not found %NDKPREFIX%
	exit/b
)

if not exist "%NDKPATH%" (
	echo Directory not found %NDKPATH%
	exit/b
)

set PATH=%cd%\aliases;C:\msys64\usr\bin;\mingw64\bin;%WINDIR%\system32;%WINDIR%

set ACNOWARN=-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-private-field -Wno-unused-function ^
 -Wno-misleading-indentation -Wno-implicit-function-declaration -Wno-deprecated-declarations ^
 -Wno-overloaded-virtual -Wno-array-parameter -Wno-vla-cxx-extension ^
 -Wno-int-conversion -Wno-sign-compare ^
 -Wno-constant-logical-operand -Wno-switch
set ACPPFLAGS=--target=%NDKTARGET%%NDKminSdkVersion% -O3 -g1 %ACNOWARN% ^
 -DANDROID -D__ANDROID__ -DANDROID_PLATFORM=android-32 -DNDEBUG -D_REENTRANT ^
 -D__GLIBC__ -D_GLIBCXX__PTHREADS -pthread ^
 -fPIC -frtti -fexceptions -funwind-tables -fomit-frame-pointer -fno-strict-aliasing -fstack-protector-strong ^
 -fdata-sections -ffunction-sections -fvisibility=hidden
if "%cc_cpu%" == "arm" set ACPPFLAGS=-march=armv8-a -mtune=cortex-a53 %ACPPFLAGS%
if "%cc_cpu%" == "x64" set ACPPFLAGS=-march=x86-64-v2 %ACPPFLAGS%
set "ACFLAGS= "
set "ACXXFLAGS= "
set ALDFLAGS=--target=%NDKTARGET%%NDKminSdkVersion% -pthread -fstack-protector
if "%cc_cpu%" == "arm" set ALDFLAGS=-march=armv8-a %ALDFLAGS%
if "%cc_cpu%" == "x64" set ALDFLAGS=-march=x86-64-v2 %ALDFLAGS%
if "%cc_cpu%" == "arm" set B2FLAGS=architecture=arm abi=aapcs
if "%cc_cpu%" == "x64" set B2FLAGS=architecture=x86 abi=sysv
