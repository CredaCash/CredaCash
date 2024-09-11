################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/depends/jsoncpp/jsoncpp.cpp 

CPP_DEPS += \
./src/jsoncpp/jsoncpp.d 

OBJS += \
./src/jsoncpp/jsoncpp.o 


# Each subdirectory must supply rules for building sources it contributes
src/jsoncpp/jsoncpp.o: $(CREDACASH_BUILD)/depends/jsoncpp/jsoncpp.cpp src/jsoncpp/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -DHAVE_UINT64_T=1 -DUNALIGNED_WORD_ACCESS=1 -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/boost -I$(CREDACASH_BUILD)/depends/blake2 -I$(CREDACASH_BUILD)/depends/ed25519 -I$(CREDACASH_BUILD)/depends/sqlite -I$(CREDACASH_BUILD)/source/3rdparty/src/sqlite -Wall -c -fmessage-length=0 -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-array-parameter $(CPPFLAGS) $(CXXFLAGS) -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src-2f-jsoncpp

clean-src-2f-jsoncpp:
	-$(RM) ./src/jsoncpp/jsoncpp.d ./src/jsoncpp/jsoncpp.o

.PHONY: clean-src-2f-jsoncpp

