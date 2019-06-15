################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
$(CREDACASH_BUILD)/depends/sqlite/sqlite3.c 

OBJS += \
./src/sqlite/sqlite3.o 

C_DEPS += \
./src/sqlite/sqlite3.d 


# Each subdirectory must supply rules for building sources it contributes
src/sqlite/sqlite3.o: $(CREDACASH_BUILD)/depends/sqlite/sqlite3.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -O3 -Wall $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


