################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
$(CREDACASH_BUILD)/source/ccnode/src/dbconn-explain.cpp 

CPP_DEPS += \
./import-ccnode/dbconn-explain.d 

OBJS += \
./import-ccnode/dbconn-explain.o 


# Each subdirectory must supply rules for building sources it contributes
import-ccnode/dbconn-explain.o: $(CREDACASH_BUILD)/source/ccnode/src/dbconn-explain.cpp import-ccnode/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccnode/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-import-2d-ccnode

clean-import-2d-ccnode:
	-$(RM) ./import-ccnode/dbconn-explain.d ./import-ccnode/dbconn-explain.o

.PHONY: clean-import-2d-ccnode

