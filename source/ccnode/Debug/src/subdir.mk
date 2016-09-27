################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/block.cpp \
../src/blockchain.cpp \
../src/blockserve.cpp \
../src/blocksync.cpp \
../src/ccnode.cpp \
../src/commitments.cpp \
../src/dbconn-persistent.cpp \
../src/dbconn-processq.cpp \
../src/dbconn-relay.cpp \
../src/dbconn-tempserials.cpp \
../src/dbconn-validobjs.cpp \
../src/dbconn-wal.cpp \
../src/dbconn.cpp \
../src/expire.cpp \
../src/hostdir.cpp \
../src/processblock.cpp \
../src/processtx.cpp \
../src/relay.cpp \
../src/service_base.cpp \
../src/tor.cpp \
../src/transact.cpp \
../src/util.cpp \
../src/witness.cpp 

OBJS += \
./src/block.o \
./src/blockchain.o \
./src/blockserve.o \
./src/blocksync.o \
./src/ccnode.o \
./src/commitments.o \
./src/dbconn-persistent.o \
./src/dbconn-processq.o \
./src/dbconn-relay.o \
./src/dbconn-tempserials.o \
./src/dbconn-validobjs.o \
./src/dbconn-wal.o \
./src/dbconn.o \
./src/expire.o \
./src/hostdir.o \
./src/processblock.o \
./src/processtx.o \
./src/relay.o \
./src/service_base.o \
./src/tor.o \
./src/transact.o \
./src/util.o \
./src/witness.o 

CPP_DEPS += \
./src/block.d \
./src/blockchain.d \
./src/blockserve.d \
./src/blocksync.d \
./src/ccnode.d \
./src/commitments.d \
./src/dbconn-persistent.d \
./src/dbconn-processq.d \
./src/dbconn-relay.d \
./src/dbconn-tempserials.d \
./src/dbconn-validobjs.d \
./src/dbconn-wal.d \
./src/dbconn.d \
./src/expire.d \
./src/hostdir.d \
./src/processblock.d \
./src/processtx.d \
./src/relay.d \
./src/service_base.d \
./src/tor.d \
./src/transact.d \
./src/util.d \
./src/witness.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++0x -D_DEBUG=1 -IC:/CredaCash/source -IC:/CredaCash/source/cclib/src -IC:/CredaCash/source/cccommon/src -IC:/CredaCash/depends -IC:/CredaCash/depends/gmp -IC:/CredaCash/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter -Wstrict-overflow=4 -Werror=sign-compare -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


