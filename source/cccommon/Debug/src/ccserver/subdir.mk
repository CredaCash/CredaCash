################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/ccserver/connection.cpp \
../src/ccserver/connection_manager.cpp \
../src/ccserver/connection_registry.cpp \
../src/ccserver/server.cpp \
../src/ccserver/service.cpp \
../src/ccserver/torservice.cpp 

OBJS += \
./src/ccserver/connection.o \
./src/ccserver/connection_manager.o \
./src/ccserver/connection_registry.o \
./src/ccserver/server.o \
./src/ccserver/service.o \
./src/ccserver/torservice.o 

CPP_DEPS += \
./src/ccserver/connection.d \
./src/ccserver/connection_manager.d \
./src/ccserver/connection_registry.d \
./src/ccserver/server.d \
./src/ccserver/service.d \
./src/ccserver/torservice.d 


# Each subdirectory must supply rules for building sources it contributes
src/ccserver/%.o: ../src/ccserver/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


