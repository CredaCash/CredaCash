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

CPP_DEPS += \
./src/ccserver/connection.d \
./src/ccserver/connection_manager.d \
./src/ccserver/connection_registry.d \
./src/ccserver/server.d \
./src/ccserver/service.d \
./src/ccserver/torservice.d 

OBJS += \
./src/ccserver/connection.o \
./src/ccserver/connection_manager.o \
./src/ccserver/connection_registry.o \
./src/ccserver/server.o \
./src/ccserver/service.o \
./src/ccserver/torservice.o 


# Each subdirectory must supply rules for building sources it contributes
src/ccserver/%.o: ../src/ccserver/%.cpp src/ccserver/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-ccserver

clean-src-2f-ccserver:
	-$(RM) ./src/ccserver/connection.d ./src/ccserver/connection.o ./src/ccserver/connection_manager.d ./src/ccserver/connection_manager.o ./src/ccserver/connection_registry.d ./src/ccserver/connection_registry.o ./src/ccserver/server.d ./src/ccserver/server.o ./src/ccserver/service.d ./src/ccserver/service.o ./src/ccserver/torservice.d ./src/ccserver/torservice.o

.PHONY: clean-src-2f-ccserver

