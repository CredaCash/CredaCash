################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/ed25519/ed25519-hash-custom.c \
../src/ed25519/ed25519-randombytes-custom.c \
C:/CredaCash/depends/ed25519/ed25519.c 

OBJS += \
./src/ed25519/ed25519-hash-custom.o \
./src/ed25519/ed25519-randombytes-custom.o \
./src/ed25519/ed25519.o 

C_DEPS += \
./src/ed25519/ed25519-hash-custom.d \
./src/ed25519/ed25519-randombytes-custom.d \
./src/ed25519/ed25519.d 


# Each subdirectory must supply rules for building sources it contributes
src/ed25519/%.o: ../src/ed25519/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -IC:/CredaCash/depends -IC:/CredaCash/depends/boost -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/blake2 -IC:/CredaCash/depends/sqlite -IC:/CredaCash/depends/keccak -IC:/CredaCash/depends/ed25519 -IC:/CredaCash/source/3rdparty/src/sqlite -IC:/CredaCash/source/3rdparty/src/ed25519 -IC:/CredaCash/source/cccommon/src -O3 -Wall -c -fmessage-length=0 -Wstrict-overflow=4 -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/ed25519/ed25519.o: C:/CredaCash/depends/ed25519/ed25519.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -IC:/CredaCash/depends -IC:/CredaCash/depends/boost -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/blake2 -IC:/CredaCash/depends/sqlite -IC:/CredaCash/depends/keccak -IC:/CredaCash/depends/ed25519 -IC:/CredaCash/source/3rdparty/src/sqlite -IC:/CredaCash/source/3rdparty/src/ed25519 -IC:/CredaCash/source/cccommon/src -O3 -Wall -c -fmessage-length=0 -Wstrict-overflow=4 -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


