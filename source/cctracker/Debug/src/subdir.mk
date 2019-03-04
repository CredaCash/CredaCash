################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/cctracker.cpp \
../src/dir.cpp \
../src/dirserver.cpp 

OBJS += \
./src/cctracker.o \
./src/dir.o \
./src/dirserver.o 

CPP_DEPS += \
./src/cctracker.d \
./src/dir.d \
./src/dirserver.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


