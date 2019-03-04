################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/source/ccnode/src/dbconn-explain.cpp 

OBJS += \
./import-ccnode/dbconn-explain.o 

CPP_DEPS += \
./import-ccnode/dbconn-explain.d 


# Each subdirectory must supply rules for building sources it contributes
import-ccnode/dbconn-explain.o: $(CREDACASH_BUILD)/source/ccnode/src/dbconn-explain.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccnode/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


