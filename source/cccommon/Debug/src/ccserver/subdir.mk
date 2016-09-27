################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/ccserver/connection.cpp \
../src/ccserver/connection_manager.cpp \
../src/ccserver/connection_registry.cpp \
../src/ccserver/server.cpp \
../src/ccserver/service.cpp 

OBJS += \
./src/ccserver/connection.o \
./src/ccserver/connection_manager.o \
./src/ccserver/connection_registry.o \
./src/ccserver/server.o \
./src/ccserver/service.o 

CPP_DEPS += \
./src/ccserver/connection.d \
./src/ccserver/connection_manager.d \
./src/ccserver/connection_registry.d \
./src/ccserver/server.d \
./src/ccserver/service.d 


# Each subdirectory must supply rules for building sources it contributes
src/ccserver/%.o: ../src/ccserver/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/boost -O0 -g3 -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


