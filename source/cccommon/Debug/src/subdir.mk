################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/CCassert.cpp \
../src/CCcrypto.cpp \
../src/CCmalloc.cpp \
../src/CCobjects.cpp \
../src/CCticks.cpp \
../src/CCutil.cpp \
../src/SmartBuf.cpp \
../src/apputil.cpp \
../src/dblog.cpp \
../src/osutil.cpp \
../src/socks.cpp \
../src/tor.cpp \
../src/unifloat.cpp 

CPP_DEPS += \
./src/CCassert.d \
./src/CCcrypto.d \
./src/CCmalloc.d \
./src/CCobjects.d \
./src/CCticks.d \
./src/CCutil.d \
./src/SmartBuf.d \
./src/apputil.d \
./src/dblog.d \
./src/osutil.d \
./src/socks.d \
./src/tor.d \
./src/unifloat.d 

OBJS += \
./src/CCassert.o \
./src/CCcrypto.o \
./src/CCmalloc.o \
./src/CCobjects.o \
./src/CCticks.o \
./src/CCutil.o \
./src/SmartBuf.o \
./src/apputil.o \
./src/dblog.o \
./src/osutil.o \
./src/socks.o \
./src/tor.o \
./src/unifloat.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/CCassert.d ./src/CCassert.o ./src/CCcrypto.d ./src/CCcrypto.o ./src/CCmalloc.d ./src/CCmalloc.o ./src/CCobjects.d ./src/CCobjects.o ./src/CCticks.d ./src/CCticks.o ./src/CCutil.d ./src/CCutil.o ./src/SmartBuf.d ./src/SmartBuf.o ./src/apputil.d ./src/apputil.o ./src/dblog.d ./src/dblog.o ./src/osutil.d ./src/osutil.o ./src/socks.d ./src/socks.o ./src/tor.d ./src/tor.o ./src/unifloat.d ./src/unifloat.o

.PHONY: clean-src

