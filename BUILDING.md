#Building the CredaCash&trade; Network Node (Server) Software

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

The CredaCash network node software is cross-platform, but the most recent release has only been built and tested under Windows x64.  There is currently no documentation on building for Linux.

##Windows x64

A Windows executable with instructions is available at [CredaCash.com](https://credacash.com/software/).  The following steps are only needed if you would like to rebuild from source.  The build process currently requires a number of manual steps; it will be better automated in the future.

###Setting up the build environment

####MinGW-w64

1. Download [MinGW-w64 4.8.5 Posix threads SEH]( http://downloads.sourceforge.net/project/mingw-w64/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/4.8.5/threads-posix/seh/x86_64-4.8.5-release-posix-seh-rt_v4-rev0.7z)

2. Extract to C:\mingw64

3. Check that the directory C:\mingw64 contains the subdirectories bin, include, lib, etc.

####MSYS

1. Download the [MSYS Installer](http://downloads.sourceforge.net/project/mingw/Installer/mingw-get-setup.exe)

2. Run mingw-get-setup.exe

3. Install to the directory C:\MinGW and include the graphical user interface.

4. In "Basic Setup", check the "msys-base" package and select "Mark for Installation".

5. Under the "Installation" menu, select "Apply Changes".

6. In "All Packages", select "MSYS", and then check and "Mark for Installation" the "bin" or "dll" version of the following packages:

 - msys-autoconf
 - msys-autogen
 - msys-automake
 - msys-binutils
 - msys-bison
 - msys-dash
 - msys-flex
 - msys-groff
 - msys-guile
 - msys-help2man
 - msys-libtool
 - msys-m4
 - msys-mksh
 - msys-mktemp
 - msys-patch
 - msys-perl
 - msys-rebase
 - msys-unzip
 - msys-wget
 - msys-zip

7. Under the "Installation" menu, select "Apply Changes".

8. Close the installer.

###Obtaining the CredaCash source

####CredaCash server

1. Download the CredaCash network node [source code](https://github.com/CredaCash/network-node/archive/master.zip)

2. Extract to C:\CredaCash

3. Check that the directory C:\CredaCash contains the subdirectories source and test.

####snarklib

1. Download [snarklib](https://github.com/CredaCash/snarklib/archive/master.zip)

2. Extract all files to C:\CredaCash\source\snarklib

####snarkfront

1. Download [snarkfront](https://github.com/CredaCash/snarkfront/archive/master.zip)

2. Extract all files to C:\CredaCash\source\snarkfront

###Obtaining and Building the Dependents

####GMP

1. Download the latest stable version of the GMP library from [https://gmplib.org/](https://gmplib.org/)

2. Extract to C:\CredaCash\depends\gmp

3. Check that the directory C:\CredaCash\depends\gmp contains the subdirectories cxx, doc, mpf, etc.

4. From a command prompt, run the following commands:

	``C:\CredaCash\set_path.bat``

	``cd C:\CredaCash\depends\gmp``

 	``perl ./configure NM=/c/mingw64/bin/nm --enable-cxx --enable-fat --disable-shared --disable-fft --with-gnu-ld``
 	``make``

#####Boost

1. Download the latest stable version of the Boost library source code from [https://sourceforge.net/projects/boost/files/boost/](https://sourceforge.net/projects/boost/files/boost/)

2. Extract to C:\CredaCash\depends\boost

3. Check that the directory C:\CredaCash\depends\boost contains the subdirectories boost, doc, libs, etc.

4. From a command prompt, run the following commands:

	``C:\CredaCash\set_path.bat``

	``cd C:\CredaCash\depends\boost``

	``bootstrap.bat``

5. Edit project-config.jam and change "using msvc" to "using gcc"

6. From the command prompt:

	``b2 --layout=system variant=release threading=multi link=static runtime-link=static``

	The output should be "The Boost C++ Libraries were successfully built!"

#####Other Dependents

1. Look in the directory C:\CredaCash\source\3rdparty\src, and for each subdirectory, obtain the source package described in the ORIGIN-*.txt file and place the required file into a corresponding subdirectory in C:\CredaCash\depends.  For example, the source package described in the file C:\CredaCash\source\3rdparty\src\blake2\ORIGIN-blake2.txt should be placed into the directory C:\CredaCash\depends\blake2

###Building the CredaCash source

1. From a command prompt (with the PATH set as described above), run the following commands:

	``cd C:\CredaCash``

	``set_path.bat``

	``make_release.bat``

This should build the binary distribution files ccnode.exe, cctx64.dll, and cctracker.exe, and place them in their respective "Release" subdirectories.