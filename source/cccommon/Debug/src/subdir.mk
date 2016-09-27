################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/CCassert.cpp \
../src/CCcrypto.cpp \
../src/CCobjects.cpp \
../src/CCticks.cpp \
../src/CCutil.cpp \
../src/SmartBuf.cpp \
../src/dblog.cpp \
../src/socks.cpp 

OBJS += \
./src/CCassert.o \
./src/CCcrypto.o \
./src/CCobjects.o \
./src/CCticks.o \
./src/CCutil.o \
./src/SmartBuf.o \
./src/dblog.o \
./src/socks.o 

CPP_DEPS += \
./src/CCassert.d \
./src/CCcrypto.d \
./src/CCobjects.d \
./src/CCticks.d \
./src/CCutil.d \
./src/SmartBuf.d \
./src/dblog.d \
./src/socks.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/boost -O0 -g3 -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


