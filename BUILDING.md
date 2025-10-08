# Building the CredaCash&trade; Software

<!--- NOTE: This file is in Markdown format, and is intended to be viewed in a Markdown viewer. -->

Windows executables are available at [CredaCash.com](https://CredaCash.com/software/).  The following steps are only needed to build the software for Linux, or to rebuild for Windows from source.

## Supported Platforms

The CredaCash software is intended to be cross-platform, and has been built and run under 64-bit versions of Windows and 64-bit little-endian versions of Linux.  Compatibility with other platforms is unknown.

## Dependencies

The following dependencies are required.  Specific instructions are provided below for Debian Linux and Windows x64.

Required to fetch the source dependencies:

- curl
- unzip
- Python (v2.7, or v3.8 or higher)

Required to build:

- GNU g++ v4.8 or higher.  The CredaCash software has been tested with g++ up to v12.2.  The earlier versions work just as well as the more recent versions.  Under Linux, the default version provided with your distribution is recommended.
- Boost library, version 1.81 or earlier, but no earlier than version v1.66.  Under Linux, the version provided with your distribution is recommended, if it is compatible.
- GNU gmplib, any recent version.  Under Linux, the version provided with your distribution is recommended, if it is compatible.

Required to run:

- Tor v0.3.2 or higher

## Linux

The following instructions are specifically for Debian 10.  They may be adaptable to other Linux distributions.

1. Install the following packages:

		sudo apt-get install curl unzip make g++ libboost-all-dev libgmp-dev

	Note that in Redhat-based distributions, the above packages are named:

		sudo yum install curl unzip make gcc-c++ boost-devel gmp-devel

2. Tor v0.3.2 or higher is required to run CredaCash.  For Debian 10, simply install the tor package.  For earlier Debian versions, Tor v0.3.2+ is available in the backports repository.  If the backports repository has not yet been enabled, it can be enabled with:

		sudo sh -c "echo 'deb http://deb.debian.org/debian stretch-backports main' >> /etc/apt/sources.list.d/stretch-backports.list"
		sudo apt-get update

	Tor v0.3.2+ can then be installed with:

		sudo apt-get -t stretch-backports install tor
		sudo systemctl stop tor
		sudo systemctl disable tor

	It is also possible to build Tor from source.  The source code is available at the [Tor Distribution site](https://dist.torproject.org/).  You may need to also first install:
	
		sudo apt-get install libevent-dev libssl-dev libzstd-dev zlib1g-dev liblzma-dev libsystemd-dev
		
	or in a Redhat-based distribution:
	
		sudo yum install libevent-devel openssl-devel libzstd-devel zlib-devel xz-devel systemd-devel
		
	Tor can then be built using the commands:

		./configure --enable-systemd --disable-module-relay --disable-unittests --disable-asciidoc
		make
		sudo make install

	Note that --enable-systemd can be omitted if your environment does not use systemd.

3. Open a new terminal window and fetch the CredaCash source code:

		mkdir CredaCash
		cd CredaCash
		curl -L https://github.com/CredaCash/CredaCash/archive/master.zip -o credacash_source.zip
		unzip credacash_source.zip
		mv CredaCash-master/* .
		rmdir CredaCash-master
		rm *.bat
		chmod +x *.sh

4. Check that the command "python" will run Python:

		python --version

	If this command does not report the Python version, then run the command:

		sudo ln /usr/bin/python3 /usr/bin/python

	or, if you are using Python v2.7, run the command:

		sudo ln /usr/bin/python2 /usr/bin/python

	and then recheck that "python --version" works.

5. Fetch the dependencies:

		./get_depends.sh

6. Fetch the genesis files:

		./get_genesis.sh

7. Fetch the zero knowledge proof keys:

		./get_zkkeys.sh

8. Build the executables:

		./make_release.sh

	This should build the network node server (ccnode.exe) and wallet server (ccwallet.exe) and place them into the current directory.

#### Boost

If the build is not successful due to incompatibilities with Boost, Boost can be built from source as static libraries under Linux as follows:

1. Download version 1.81 or earlier of the Boost library source code from [boost.org](https://www.boost.org/).

2. Extract the Boost source code to CredaCash/depends/boost

3. Check that the directory CredaCash/depends/boost contains the subdirectories boost, doc, libs, etc.

4. From a command prompt, run the following commands:

		cd CredaCash/depends/boost
		./bootstrap.sh
		./b2 --layout=system --ignore-site-config --without-python cflags=-fPIC variant=release threading=multi link=static runtime-link=shared

	The output should be "The Boost C++ Libraries were successfully built!"

5. The CredaCash executables can then be built using the static boost libraries as follows:

		cd CredaCash
		./make_clean.sh
		./make_release.sh static-boost

### Python Requests Module

CredaCash's Python scripts require the Python requests module which can be installed as follows:

	python -m ensurepip --upgrade
	python -m pip install requests

## Windows x64

Windows executables are available at [CredaCash.com](https://CredaCash.com/software/).  The following steps are only needed to rebuild the executables from source.

### Set up the environment

#### MinGW-w64

1. Download [WinLibs GCC 7.5.0 + MinGW-w64 7.0.0 (MSVCRT)](https://github.com/brechtsanders/winlibs_mingw/releases/download/7.5.0-7.0.0-r1/winlibs-x86_64-posix-seh-gcc-7.5.0-mingw-w64-7.0.0-r1.7z) or another compatible version from [WinLibs](https://winlibs.com/)

2. Extract to C:\mingw64

3. Check that the directory C:\mingw64 contains the subdirectories bin, include, lib, etc.

#### MSYS

1. Download the [MSYS Installer](http://downloads.sourceforge.net/project/mingw/Installer/mingw-get-setup.exe)

2. Run mingw-get-setup.exe

3. Install to the directory C:\MinGW and include the graphical user interface.

4. In "Basic Setup", check the "msys-base" package and select "Mark for Installation".

5. Under the "Installation" menu, select "Apply Changes".

6. In "All Packages", select "MSYS", and then check and "Mark for Installation" the "bin" or "dll" version of the following packages:

	msys-autoconf
	msys-autogen
	msys-automake
	msys-binutils
	msys-bison
	msys-flex
	msys-guile
	msys-help2man
	msys-libtool
	msys-m4
	msys-mksh
	msys-mktemp
	msys-patch
	msys-perl

7. Under the "Installation" menu, select "Apply Changes".

8. Close the installer.

#### Python

1. In a web browser, go to [https://www.python.org/downloads/](https://www.python.org/downloads/)

2. Download and install Python for Windows x86-64

3. Check that python is in your path by executing:

		python --version

	This should print the Python version.

4. Install the Python requests module by opening a command window and executing these commands:

		python -m ensurepip --upgrade
		python -m pip install requests

#### Curl

1. In a web browser, go to https://curl.haxx.se/windows/

2. Select "curl for 32 bit"

3. Download and extract the files to any directory.

#### Unzip

1. Download [unzip.zip](https://credacash.s3.dualstack.us-east-1.amazonaws.com/unzip.zip)
 
2. Extract the single file (unzip.exe) to any directory.

#### Tor

1. In a web browser, go to [https://www.torproject.org/download/tor/](https://www.torproject.org/download/tor/)

2. Click "Microsoft Windows"

3. Download the Tor Expert Bundle

4. Extract the files to any new directory location.

### Obtain the CredaCash source

1. Download the CredaCash network node [source code](https://github.com/CredaCash/CredaCash/archive/master.zip)

2. Extract it to C:\CredaCash

3. Check that C:\CredaCash contains the subdirectories source and test.

4. Open a command prompt and execute:

		cd C:\CredaCash
		set_path.bat

5. Add the directories containing curl.exe and unzip.exe to the PATH:

		set PATH=%PATH%;<directory containing curl.exe>;<directory containing unzip.exe>

6. Check that curl.exe is in your path by executing:

		curl --version

	This should print the curl version.

7. Check that unzip.exe is in your path by executing:

		unzip -h

	This should print the unzip help text.

8. Fetch the dependencies by executing:

		get_depends.bat
 
### Build the Dependents

#### GMP

1. Download the latest stable version of the GMP library from [https://gmplib.org/download/](https://gmplib.org/download/)

2. Extract to C:\CredaCash\depends\gmp

3. Check that the directory C:\CredaCash\depends\gmp contains the subdirectories cxx, doc, mpf, etc.

4. From a command prompt, run the following commands:

		C:\CredaCash\set_path.bat
		cd C:\CredaCash\depends\gmp
		perl ./configure NM=/c/mingw64/bin/nm --enable-cxx --enable-fat --disable-shared --disable-fft --with-gnu-ld
		make

#### Boost

1. Download version 1.81 or earlier of the Boost library source code from [boost.org](https://www.boost.org/).

2. Extract the Boost source code to C:\CredaCash\depends\boost

3. Check that the directory C:\CredaCash\depends\boost contains the subdirectories boost, doc, libs, etc.

4. From a command prompt, run the following commands:

		C:\CredaCash\set_path.bat
		cd C:\CredaCash\depends\boost
		bootstrap.bat gcc
		b2 --layout=system --without-python define=_WIN32_WINNT=0x0502 cxxflags="-std=c++11 -D_hypot=hypot" toolset=gcc address-model=64 variant=release threading=multi link=static runtime-link=static

	The output should be "The Boost C++ Libraries were successfully built!"

### Build CredaCash

1. From a command prompt, run the following commands:

		cd C:\CredaCash
		set_path.bat
		make_release.bat

	This should build the binary distribution files ccnode.exe, ccwallet.exe and cctx64.dll and place them in their respective "Release" subdirectories.

2. Fetch the genesis files:

		get_genesis.bat

3. Create or edit the configuration files cnode.conf and ccwallet.conf and set the value of tor-exe to the full path of tor.exe in the directory created above.
