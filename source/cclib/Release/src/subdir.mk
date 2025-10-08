################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/CCbigint.cpp \
../src/CCproof.cpp \
../src/amounts.cpp \
../src/encode.cpp \
../src/jsoncmd.cpp \
../src/jsonutil.cpp \
../src/map_values.cpp \
../src/payspec.cpp \
../src/transaction.cpp \
../src/txquery.cpp \
../src/xmatch.cpp \
../src/xtransaction-xpay.cpp \
../src/xtransaction-xreq.cpp \
../src/xtransaction.cpp \
../src/zkkeys.cpp 

C_SRCS += \
../src/encodings.c 

CPP_DEPS += \
./src/CCbigint.d \
./src/CCproof.d \
./src/amounts.d \
./src/encode.d \
./src/jsoncmd.d \
./src/jsonutil.d \
./src/map_values.d \
./src/payspec.d \
./src/transaction.d \
./src/txquery.d \
./src/xmatch.d \
./src/xtransaction-xpay.d \
./src/xtransaction-xreq.d \
./src/xtransaction.d \
./src/zkkeys.d 

C_DEPS += \
./src/encodings.d 

OBJS += \
./src/CCbigint.o \
./src/CCproof.o \
./src/amounts.o \
./src/encode.o \
./src/encodings.o \
./src/jsoncmd.o \
./src/jsonutil.o \
./src/map_values.o \
./src/payspec.o \
./src/transaction.o \
./src/txquery.o \
./src/xmatch.o \
./src/xtransaction-xpay.o \
./src/xtransaction-xreq.o \
./src/xtransaction.o \
./src/zkkeys.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/CCbigint.d ./src/CCbigint.o ./src/CCproof.d ./src/CCproof.o ./src/amounts.d ./src/amounts.o ./src/encode.d ./src/encode.o ./src/encodings.d ./src/encodings.o ./src/jsoncmd.d ./src/jsoncmd.o ./src/jsonutil.d ./src/jsonutil.o ./src/map_values.d ./src/map_values.o ./src/payspec.d ./src/payspec.o ./src/transaction.d ./src/transaction.o ./src/txquery.d ./src/txquery.o ./src/xmatch.d ./src/xmatch.o ./src/xtransaction-xpay.d ./src/xtransaction-xpay.o ./src/xtransaction-xreq.d ./src/xtransaction-xreq.o ./src/xtransaction.d ./src/xtransaction.o ./src/zkkeys.d ./src/zkkeys.o

.PHONY: clean-src

