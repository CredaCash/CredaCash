# Building the CredaCash&trade; Software

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

Windows executables are available at [CredaCash.com](https://CredaCash.com/software/).  The following steps are only needed to build the software for Linux, or to rebuild for Windows from source.

## Supported Platforms

The CredaCash software is intended to be cross-platform, and has been built and run under 64-bit versions of Windows and Linux (specifically, Windows 7 x64, Debian Stretch v9.8, and the most recent Amazon Linux 2 AMI for Amazon EC2).  Compatibility with other platforms is unknown.

## Dependencies

The following dependencies are required.  Specific instructions are provided below for Debian Linux and Windows x64.

Required to fetch the source dependencies:

- curl
- unzip
- Python (any version, however Python 2.7.x 64-bit is required to run the test scripts)

Required to build:

- GNU g++ v4.8 or higher.  The software has been specifically tested with v4.9.4, v6.4.0 and v7.3.1.  The earlier version works just as well as the more recent versions.  Under Linux, the default version provided with your distribution is recommended.
- Boost library, roughly version v1.50 or higher.  The software has been specifically tested with v1.62 and v1.69.  Under Linux, the version provided with your distribution is recommended, if it is compatible.
- GNU gmplib, any recent version.  Under Linux, the version provided with your distribution is recommended, if it is compatible.

Required to run:

- Tor v0.3.2 or higher

Required to run the test scripts:

- Python 2.7.x 64-bit

## Linux

The following instructions are specifically for Debian Stretch v9.8.  They may be adaptable to other Linux distributions.

1. Install the following packages:

	- curl
	- unzip
	- make
	- g++
	- libboost-all-dev
	- libgmp-dev

	Note that in Redhat-based distribtions, the above packages are named curl, unzip, make, gcc-c++, boost-devel, and gmp-devel

2. Tor v0.3.2 or higher is required to run CredaCash.  Tor binary packages for various Linux distributions, and source that is straightforward to build for any distribution, are available at the [Tor Project website](https://www.torproject.org/download/download-unix.html).  Older versions can be found at the [Tor Project archive](https://dist.torproject.org/).

	For Debian Stretch, Tor v0.3.2+ is also available in the backports repository.  If the backports repository has not yet been enabled, it can be enabled with:

	``sudo sh -c "echo 'deb http://deb.debian.org/debian stretch-backports main' >> /etc/apt/sources.list.d/stretch-backports.list"``

	``sudo apt-get update``

	Tor v0.3.2+ can then be installed with:

	``sudo apt-get -t stretch-backports install tor``

	``sudo systemctl stop tor``

	``sudo systemctl disable tor``

3. Open a new terminal window and fetch the CredaCash source code:

	``mkdir CredaCash``

	``cd CredaCash``

	``curl -L https://github.com/CredaCash/CredaCash/archive/master.zip -o credacash_source.zip``

	``unzip credacash_source.zip``

	``mv CredaCash-master/* .``

	``rmdir CredaCash-master``

	``rm *.bat``

	``chmod +x *.sh``

4. Fetch the dependencies:

	``./get_depends.sh``

5. Fetch the genesis files:

	``./get_genesis.sh``

6. Fetch the zero knowledge proof keys:

	``./get_zkkeys.sh``

7. Build the executables:

	``./make_release.sh``

	This should build the network node server (ccnode.exe), wallet server (ccwallet.exe) and transaction library (cctx64.dll) and place them into the current directory.

	If the compiler does not accept the "-m64" flag, it can be removed from the makefiles with the command:

	``find source -name subdir.mk -exec sed -i 's/ -m64//g' {} +``

##### Boost

If the build is not successful due to incompatibilities with Boost, Boost can be built from source as static libraries under Linux as follows:

1. Download the latest stable version of the Boost library source code from [Sourceforge](https://sourceforge.net/projects/boost/files/boost/)

2. Extract to CredaCash/depends/boost

3. Check that the directory CredaCash/depends/boost contains the subdirectories boost, doc, libs, etc.

4. From a command prompt, run the following commands:

	``cd CredaCash/depends/boost``

	``./bootstrap.sh``

	``./b2 --layout=system variant=release threading=multi link=static runtime-link=shared``

	The output should be "The Boost C++ Libraries were successfully built!"

5. The CredaCash executables can then be built using the static boost libraries as follows:

	``cd CredaCash``

	``./make_clean.sh``

	``./make_release.sh static-boost``

## Windows x64

Windows executables are available at [CredaCash.com](https://CredaCash.com/software/).  The following steps are only needed to rebuild the executables from source.

### Set up the environment

#### MinGW-w64

1. Download [MinGW-w64 4.9.4 Posix threads SEH](http://downloads.sourceforge.net/project/mingw-w64/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/4.9.4/threads-posix/seh/x86_64-4.9.4-release-posix-seh-rt_v5-rev0.7z)

2. Extract to C:\mingw64

3. Check that the directory C:\mingw64 contains the subdirectories bin, include, lib, etc.

#### MSYS

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
 - msys-flex
 - msys-guile
 - msys-help2man
 - msys-libtool
 - msys-m4
 - msys-mksh
 - msys-mktemp
 - msys-patch
 - msys-perl

7. Under the "Installation" menu, select "Apply Changes".

8. Close the installer.

#### Python

1. In a web browser, go to https://www.python.org/downloads/windows/

2. Click "Latest Python 2 Release"

3. Click "Windows x86-64 MSI installer"

4. Download and install.

#### Curl

1. In a web browser, go to https://curl.haxx.se/windows/

2. Select "curl for 32 bit"

3. Download and extract the files to any directory.

#### Unzip

1. Download [unzip.zip](https://credacash.s3.amazonaws.com/unzip.zip)
 
2. Extract the single file (unzip.exe) to any directory.

#### Tor

1. In a web browser, go to https://www.torproject.org/download/download.html.en

2. Click "Microsoft Windows"

3. Download the Tor Expert Bundle

4. Extract the files to any new directory location.

### Obtain the CredaCash source

1. Download the CredaCash network node [source code](https://github.com/CredaCash/CredaCash/archive/master.zip)

2. Extract it to C:\CredaCash

3. Check that C:\CredaCash contains the subdirectories source and test.

4. Open a command prompt and execute:

	``cd C:\CredaCash``

	``set_path.bat``

5. Add the directories containing curl.exe and unzip.exe to the PATH:

	``set PATH=%PATH%;<directory containing curl.exe>;<directory containing unzip.exe>

6. Check that python is in your path by executing:

	``python --version``

	This should print the python version.

7. Check that curl.exe is in your path by executing:

	``curl --version``

	This should print the curl version.

8. Check that unzip.exe is in your path by executing:

	``unzip -h``

	This should print the unzip help text.

9. Fetch the dependencies by executing:

	``get_depends.bat``
 
### Build the Dependents

#### GMP

1. Download the latest stable version of the GMP library from [https://gmplib.org/](https://gmplib.org/)

2. Extract to C:\CredaCash\depends\gmp

3. Check that the directory C:\CredaCash\depends\gmp contains the subdirectories cxx, doc, mpf, etc.

4. From a command prompt, run the following commands:

	``C:\CredaCash\set_path.bat``

	``cd C:\CredaCash\depends\gmp``

 	``perl ./configure NM=/c/mingw64/bin/nm --enable-cxx --enable-fat --disable-shared --disable-fft --with-gnu-ld``

 	``make``

##### Boost

1. Download the latest stable version of the Boost library source code from [Sourceforge](https://sourceforge.net/projects/boost/files/boost/)

2. Extract to C:\CredaCash\depends\boost

3. Check that the directory C:\CredaCash\depends\boost contains the subdirectories boost, doc, libs, etc.

4. From a command prompt, run the following commands:

	``C:\CredaCash\set_path.bat``

	``cd C:\CredaCash\depends\boost``

	``bootstrap.bat gcc``

	``b2 -j4 --layout=system toolset=gcc address-model=64 cxxflags="-std=c++11 -D_hypot=hypot" define=BOOST_USE_WINAPI_VERSION=0x0502 variant=release threading=multi link=static runtime-link=static``

	The output should be "The Boost C++ Libraries were successfully built!"

### Build CredaCash

1. From a command prompt, run the following commands:

	``cd C:\CredaCash``

	``set_path.bat``

	``make_release.bat``

	This should build the binary distribution files ccnode.exe, ccwallet.exe and cctx64.dll and place them in their respective "Release" subdirectories.

2. Fetch the genesis files:

	``get_genesis.bat``

3. Create or edit the configuration files cnode.conf and ccwallet.conf and set the value of tor-exe to the full path of tor.exe in the directory created above.
