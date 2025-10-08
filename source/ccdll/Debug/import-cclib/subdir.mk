################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
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

C_SRCS += \
$(CREDACASH_BUILD)/source/cclib/src/encodings.c 

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

C_DEPS += \
./import-cclib/encodings.d 

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


# Each subdirectory must supply rules for building sources it contributes
import-cclib/CCbigint.o: $(CREDACASH_BUILD)/source/cclib/src/CCbigint.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/CCproof.o: $(CREDACASH_BUILD)/source/cclib/src/CCproof.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/encode.o: $(CREDACASH_BUILD)/source/cclib/src/encode.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/encodings.o: $(CREDACASH_BUILD)/source/cclib/src/encodings.c import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsoncmd.o: $(CREDACASH_BUILD)/source/cclib/src/jsoncmd.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/jsonutil.o: $(CREDACASH_BUILD)/source/cclib/src/jsonutil.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/payspec.o: $(CREDACASH_BUILD)/source/cclib/src/payspec.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/transaction.o: $(CREDACASH_BUILD)/source/cclib/src/transaction.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/txquery.o: $(CREDACASH_BUILD)/source/cclib/src/txquery.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

import-cclib/zkkeys.o: $(CREDACASH_BUILD)/source/cclib/src/zkkeys.cpp import-cclib/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DCC_DLL_EXPORTS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccdll/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-import-2d-cclib

clean-import-2d-cclib:
	-$(RM) ./import-cclib/CCbigint.d ./import-cclib/CCbigint.o ./import-cclib/CCproof.d ./import-cclib/CCproof.o ./import-cclib/encode.d ./import-cclib/encode.o ./import-cclib/encodings.d ./import-cclib/encodings.o ./import-cclib/jsoncmd.d ./import-cclib/jsoncmd.o ./import-cclib/jsonutil.d ./import-cclib/jsonutil.o ./import-cclib/payspec.d ./import-cclib/payspec.o ./import-cclib/transaction.d ./import-cclib/transaction.o ./import-cclib/txquery.d ./import-cclib/txquery.o ./import-cclib/zkkeys.d ./import-cclib/zkkeys.o

.PHONY: clean-import-2d-cclib

