################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
$(CREDACASH_BUILD)/source/cclib/src/encodings.c 

CPP_SRCS += \
$(CREDACASH_BUILD)/source/cclib/src/CCbigint.cpp \
$(CREDACASH_BUILD)/source/cclib/src/CCproof.cpp \
$(CREDACASH_BUILD)/source/cclib/src/encode.cpp \
$(CREDACASH_BUILD)/source/cclib/src/jsoncmd.cpp \
$(CREDACASH_BUILD)/source/cclib/src/jsonutil.cpp \
$(CREDACASH_BUILD)/source/cclib/src/payspec.cpp \
$(CREDACASH_BUILD)/source/cclib/src/transaction.cpp \
$(CREDACASH_BUILD)/source/cclib/src/txquery.cpp \
$(CREDACASH_BUILD)/source/cclib/src/zkkeys.cpp 

OBJS += \
./import-cclib/CCbigint.o \
./import-cclib/CCproof.o \
./import-cclib/encode.o \
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
./import-cclib/CCbigint.d \
./import-cclib/CCproof.d \
./import-cclib/encode.d \
./import-cclib/jsoncmd.d \
./import-cclib/jsonutil.d \
./import-cclib/payspec.d \
./import-cclib/transaction.d \
./import-cclib/txquery.d \
./import-cclib/zkkeys.d 


# Each subdirectory must supply rules for building sources it contributes
import-cclib/CCbigint.o: $(CREDACASH_BUILD)/source/cclib/src/CCbigint.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/CCproof.o: $(CREDACASH_BUILD)/source/cclib/src/CCproof.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/encode.o: $(CREDACASH_BUILD)/source/cclib/src/encode.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/encodings.o: $(CREDACASH_BUILD)/source/cclib/src/encodings.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsoncmd.o: $(CREDACASH_BUILD)/source/cclib/src/jsoncmd.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsonutil.o: $(CREDACASH_BUILD)/source/cclib/src/jsonutil.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/payspec.o: $(CREDACASH_BUILD)/source/cclib/src/payspec.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/transaction.o: $(CREDACASH_BUILD)/source/cclib/src/transaction.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/txquery.o: $(CREDACASH_BUILD)/source/cclib/src/txquery.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/zkkeys.o: $(CREDACASH_BUILD)/source/cclib/src/zkkeys.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


