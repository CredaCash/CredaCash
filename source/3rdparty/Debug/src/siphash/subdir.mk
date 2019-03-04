################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
$(CREDACASH_BUILD)/depends/siphash/src/siphash.c 

OBJS += \
./src/siphash/siphash.o 

C_DEPS += \
./src/siphash/siphash.d 


# Each subdirectory must supply rules for building sources it contributes
src/siphash/siphash.o: $(CREDACASH_BUILD)/depends/siphash/src/siphash.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall $(CPPFLAGS) -c -m64 -fmessage-length=0 -Wno-unused-parameter -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


