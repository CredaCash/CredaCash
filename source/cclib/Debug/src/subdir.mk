################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/encodings.c 

CPP_SRCS += \
../src/CCproof.cpp \
../src/jsoncmd.cpp \
../src/jsonutil.cpp \
../src/payspec.cpp \
../src/transaction.cpp \
../src/txquery.cpp \
../src/zkkeys.cpp 

OBJS += \
./src/CCproof.o \
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
./src/CCproof.d \
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
	g++ -std=c++0x -D_DEBUG -IC:/CredaCash/source -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG -IC:/CredaCash/source -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


