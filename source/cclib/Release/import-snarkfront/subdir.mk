################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/source/snarkfront/EnumOps.cpp \
$(CREDACASH_BUILD)/source/snarkfront/InitPairing.cpp 

CPP_DEPS += \
./import-snarkfront/EnumOps.d \
./import-snarkfront/InitPairing.d 

OBJS += \
./import-snarkfront/EnumOps.o \
./import-snarkfront/InitPairing.o 


# Each subdirectory must supply rules for building sources it contributes
import-snarkfront/EnumOps.o: $(CREDACASH_BUILD)/source/snarkfront/EnumOps.cpp import-snarkfront/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-snarkfront/InitPairing.o: $(CREDACASH_BUILD)/source/snarkfront/InitPairing.cpp import-snarkfront/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-import-2d-snarkfront

clean-import-2d-snarkfront:
	-$(RM) ./import-snarkfront/EnumOps.d ./import-snarkfront/EnumOps.o ./import-snarkfront/InitPairing.d ./import-snarkfront/InitPairing.o

.PHONY: clean-import-2d-snarkfront

