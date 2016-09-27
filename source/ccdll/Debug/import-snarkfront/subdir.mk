################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
C:/CredaCash/source/snarkfront/EnumOps.cpp \
C:/CredaCash/source/snarkfront/InitPairing.cpp 

OBJS += \
./import-snarkfront/EnumOps.o \
./import-snarkfront/InitPairing.o 

CPP_DEPS += \
./import-snarkfront/EnumOps.d \
./import-snarkfront/InitPairing.d 


# Each subdirectory must supply rules for building sources it contributes
import-snarkfront/EnumOps.o: C:/CredaCash/source/snarkfront/EnumOps.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-snarkfront/InitPairing.o: C:/CredaCash/source/snarkfront/InitPairing.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


