################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/CCassert.cpp \
../src/CCcrypto.cpp \
../src/CCobjects.cpp \
../src/CCticks.cpp \
../src/CCutil.cpp \
../src/SmartBuf.cpp \
../src/dblog.cpp \
../src/osutil.cpp \
../src/socks.cpp \
../src/tor.cpp 

OBJS += \
./src/CCassert.o \
./src/CCcrypto.o \
./src/CCobjects.o \
./src/CCticks.o \
./src/CCutil.o \
./src/SmartBuf.o \
./src/dblog.o \
./src/osutil.o \
./src/socks.o \
./src/tor.o 

CPP_DEPS += \
./src/CCassert.d \
./src/CCcrypto.d \
./src/CCobjects.d \
./src/CCticks.d \
./src/CCutil.d \
./src/SmartBuf.d \
./src/dblog.d \
./src/osutil.d \
./src/socks.d \
./src/tor.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


