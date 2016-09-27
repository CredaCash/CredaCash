################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/CredaCash/source/cclib/src/encodings.c 

CPP_SRCS += \
C:/CredaCash/source/cclib/src/CCproof.cpp \
C:/CredaCash/source/cclib/src/jsoncmd.cpp \
C:/CredaCash/source/cclib/src/jsonutil.cpp \
C:/CredaCash/source/cclib/src/payspec.cpp \
C:/CredaCash/source/cclib/src/transaction.cpp \
C:/CredaCash/source/cclib/src/txquery.cpp \
C:/CredaCash/source/cclib/src/zkkeys.cpp 

OBJS += \
./import-cclib/CCproof.o \
./import-cclib/encodings.o \
./import-cclib/jsoncmd.o \
./import-cclib/jsonutil.o \
./import-cclib/payspec.o \
./import-cclib/transaction.o \
./import-cclib/txquery.o \
./import-cclib/zkkeys.o 

C_DEPS += \
./import-cclib/encodings.d 

CPP_DEPS += \
./import-cclib/CCproof.d \
./import-cclib/jsoncmd.d \
./import-cclib/jsonutil.d \
./import-cclib/payspec.d \
./import-cclib/transaction.d \
./import-cclib/txquery.d \
./import-cclib/zkkeys.d 


# Each subdirectory must supply rules for building sources it contributes
import-cclib/CCproof.o: C:/CredaCash/source/cclib/src/CCproof.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/encodings.o: C:/CredaCash/source/cclib/src/encodings.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsoncmd.o: C:/CredaCash/source/cclib/src/jsoncmd.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsonutil.o: C:/CredaCash/source/cclib/src/jsonutil.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/payspec.o: C:/CredaCash/source/cclib/src/payspec.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/transaction.o: C:/CredaCash/source/cclib/src/transaction.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/txquery.o: C:/CredaCash/source/cclib/src/txquery.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/zkkeys.o: C:/CredaCash/source/cclib/src/zkkeys.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG -DCC_DLL_EXPORTS=1 -IC:/CredaCash/source -IC:/CredaCash/source/ccdll/src -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


