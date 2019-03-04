################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/encodings.c 

CPP_SRCS += \
../src/CCbigint.cpp \
../src/CCproof.cpp \
../src/encode.cpp \
../src/jsoncmd.cpp \
../src/jsonutil.cpp \
../src/payspec.cpp \
../src/transaction.cpp \
../src/txquery.cpp \
../src/zkkeys.cpp 

OBJS += \
./src/CCbigint.o \
./src/CCproof.o \
./src/encode.o \
./src/encodings.o \
./src/jsoncmd.o \
./src/jsonutil.o \
./src/payspec.o \
./src/transaction.o \
./src/txquery.o \
./src/zkkeys.o 

C_DEPS += \
./src/encodings.d 

CPP_DEPS += \
./src/CCbigint.d \
./src/CCproof.d \
./src/encode.d \
./src/jsoncmd.d \
./src/jsonutil.d \
./src/payspec.d \
./src/transaction.d \
./src/txquery.d \
./src/zkkeys.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O3 -Wall -Wextra $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


