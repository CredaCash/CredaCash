################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/depends/jsoncpp/jsoncpp.cpp 

OBJS += \
./src/jsoncpp/jsoncpp.o 

CPP_DEPS += \
./src/jsoncpp/jsoncpp.d 


# Each subdirectory must supply rules for building sources it contributes
src/jsoncpp/jsoncpp.o: $(CREDACASH_BUILD)/depends/jsoncpp/jsoncpp.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


