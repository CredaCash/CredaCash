################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
$(CREDACASH_BUILD)/depends/siphash/src/siphash.c 

C_DEPS += \
./src/siphash/siphash.d 

OBJS += \
./src/siphash/siphash.o 


# Each subdirectory must supply rules for building sources it contributes
src/siphash/siphash.o: $(CREDACASH_BUILD)/depends/siphash/src/siphash.c src/siphash/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -c -fmessage-length=0 -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-array-parameter $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-siphash

clean-src-2f-siphash:
	-$(RM) ./src/siphash/siphash.d ./src/siphash/siphash.o

.PHONY: clean-src-2f-siphash

