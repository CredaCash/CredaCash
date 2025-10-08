################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
$(CREDACASH_BUILD)/depends/blake2/blake2b.c \
$(CREDACASH_BUILD)/depends/blake2/blake2s.c 

C_DEPS += \
./src/blake2/blake2b.d \
./src/blake2/blake2s.d 

OBJS += \
./src/blake2/blake2b.o \
./src/blake2/blake2s.o 


# Each subdirectory must supply rules for building sources it contributes
src/blake2/blake2b.o: $(CREDACASH_BUILD)/depends/blake2/blake2b.c src/blake2/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -c -fmessage-length=0 -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-array-parameter $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/blake2/blake2s.o: $(CREDACASH_BUILD)/depends/blake2/blake2s.c src/blake2/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -c -fmessage-length=0 -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-array-parameter $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-blake2

clean-src-2f-blake2:
	-$(RM) ./src/blake2/blake2b.d ./src/blake2/blake2b.o ./src/blake2/blake2s.d ./src/blake2/blake2s.o

.PHONY: clean-src-2f-blake2

