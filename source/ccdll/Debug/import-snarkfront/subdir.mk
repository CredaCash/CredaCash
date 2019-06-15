################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/source/snarkfront/EnumOps.cpp \
$(CREDACASH_BUILD)/source/snarkfront/InitPairing.cpp 

OBJS += \
./import-snarkfront/EnumOps.o \
./import-snarkfront/InitPairing.o 

CPP_DEPS += \
./import-snarkfront/EnumOps.d \
./import-snarkfront/InitPairing.d 


# Each subdirectory must supply rules for building sources it contributes
import-snarkfront/EnumOps.o: $(CREDACASH_BUILD)/source/snarkfront/EnumOps.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-snarkfront/InitPairing.o: $(CREDACASH_BUILD)/source/snarkfront/InitPairing.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


