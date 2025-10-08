################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/cctracker.cpp \
../src/dir.cpp \
../src/dirserver.cpp 

CPP_DEPS += \
./src/cctracker.d \
./src/dir.d \
./src/dirserver.d 

OBJS += \
./src/cctracker.o \
./src/dir.o \
./src/dirserver.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/cctracker.d ./src/cctracker.o ./src/dir.d ./src/dir.o ./src/dirserver.d ./src/dirserver.o

.PHONY: clean-src

