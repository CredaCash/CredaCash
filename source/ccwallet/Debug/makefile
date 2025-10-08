################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/subdir.mk
-include import-ccnode/subdir.mk
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C++M_DEPS)),)
-include $(C++M_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(CCM_DEPS)),)
-include $(CCM_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(CXXM_DEPS)),)
-include $(CXXM_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := ccwallet
BUILD_ARTIFACT_EXTENSION := exe
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: main-build

# Main-build Target
main-build: ccwallet.exe

# Tool invocations
ccwallet.exe: ./src/ccwallet_dll.o $(OBJS) $(USER_OBJS) makefile $(OPTIONAL_TOOL_DEPS)
	@echo 'Building target: $@'
	@echo 'Invoking: Cross G++ Linker'
	g++ $(LDFLAGS) -L$(CREDACASH_BUILD)/source/cclib/Debug -L$(CREDACASH_BUILD)/source/cccommon/Debug -L$(CREDACASH_BUILD)/source/3rdparty/Debug -L$(CREDACASH_BUILD)/depends/boost/stage/lib -L$(CREDACASH_BUILD)/depends/gmp/.libs         -o "ccwallet.exe"                      $(OBJS) $(USER_OBJS) $(LIBS) -lcc -lcccommon -l3rdparty -lboost_program_options -lboost_log -lboost_filesystem -lboost_system -lboost_thread -lgmpxx -lgmp $(LDLIBS)
	g++ $(LDFLAGS) -L$(CREDACASH_BUILD)/source/cclib/Debug -L$(CREDACASH_BUILD)/source/cccommon/Debug -L$(CREDACASH_BUILD)/source/3rdparty/Debug -L$(CREDACASH_BUILD)/depends/boost/stage/lib -L$(CREDACASH_BUILD)/depends/gmp/.libs -shared -o "ccwallet.dll" ./src/ccwallet_dll.o $(OBJS) $(USER_OBJS) $(LIBS) -lcc -lcccommon -l3rdparty -lboost_program_options -lboost_log -lboost_filesystem -lboost_system -lboost_thread -lgmpxx -lgmp $(LDLIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) ccwallet.exe
	-@echo ' '

.PHONY: all clean dependents main-build

-include ../makefile.targets
