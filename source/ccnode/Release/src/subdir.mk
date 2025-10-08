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
../src/dbconn-explain.cpp \
../src/dbconn-persistent.cpp \
../src/dbconn-processq.cpp \
../src/dbconn-relay.cpp \
../src/dbconn-tempserials.cpp \
../src/dbconn-validobjs.cpp \
../src/dbconn-wal.cpp \
../src/dbconn-xreqs.cpp \
../src/dbconn.cpp \
../src/exchange.cpp \
../src/exchange_mining.cpp \
../src/expire.cpp \
../src/foreign-conn.cpp \
../src/foreign-query-btc.cpp \
../src/foreign-query.cpp \
../src/foreign-rpc.cpp \
../src/hostdir.cpp \
../src/mints.cpp \
../src/process-xreq.cpp \
../src/processblock.cpp \
../src/processtx.cpp \
../src/relay.cpp \
../src/seqnum.cpp \
../src/transact.cpp \
../src/witness.cpp 

CPP_DEPS += \
./src/block.d \
./src/blockchain.d \
./src/blockserve.d \
./src/blocksync.d \
./src/ccnode.d \
./src/commitments.d \
./src/dbconn-explain.d \
./src/dbconn-persistent.d \
./src/dbconn-processq.d \
./src/dbconn-relay.d \
./src/dbconn-tempserials.d \
./src/dbconn-validobjs.d \
./src/dbconn-wal.d \
./src/dbconn-xreqs.d \
./src/dbconn.d \
./src/exchange.d \
./src/exchange_mining.d \
./src/expire.d \
./src/foreign-conn.d \
./src/foreign-query-btc.d \
./src/foreign-query.d \
./src/foreign-rpc.d \
./src/hostdir.d \
./src/mints.d \
./src/process-xreq.d \
./src/processblock.d \
./src/processtx.d \
./src/relay.d \
./src/seqnum.d \
./src/transact.d \
./src/witness.d 

OBJS += \
./src/block.o \
./src/blockchain.o \
./src/blockserve.o \
./src/blocksync.o \
./src/ccnode.o \
./src/commitments.o \
./src/dbconn-explain.o \
./src/dbconn-persistent.o \
./src/dbconn-processq.o \
./src/dbconn-relay.o \
./src/dbconn-tempserials.o \
./src/dbconn-validobjs.o \
./src/dbconn-wal.o \
./src/dbconn-xreqs.o \
./src/dbconn.o \
./src/exchange.o \
./src/exchange_mining.o \
./src/expire.o \
./src/foreign-conn.o \
./src/foreign-query-btc.o \
./src/foreign-query.o \
./src/foreign-rpc.o \
./src/hostdir.o \
./src/mints.o \
./src/process-xreq.o \
./src/processblock.o \
./src/processtx.o \
./src/relay.o \
./src/seqnum.o \
./src/transact.o \
./src/witness.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/block.d ./src/block.o ./src/blockchain.d ./src/blockchain.o ./src/blockserve.d ./src/blockserve.o ./src/blocksync.d ./src/blocksync.o ./src/ccnode.d ./src/ccnode.o ./src/commitments.d ./src/commitments.o ./src/dbconn-explain.d ./src/dbconn-explain.o ./src/dbconn-persistent.d ./src/dbconn-persistent.o ./src/dbconn-processq.d ./src/dbconn-processq.o ./src/dbconn-relay.d ./src/dbconn-relay.o ./src/dbconn-tempserials.d ./src/dbconn-tempserials.o ./src/dbconn-validobjs.d ./src/dbconn-validobjs.o ./src/dbconn-wal.d ./src/dbconn-wal.o ./src/dbconn-xreqs.d ./src/dbconn-xreqs.o ./src/dbconn.d ./src/dbconn.o ./src/exchange.d ./src/exchange.o ./src/exchange_mining.d ./src/exchange_mining.o ./src/expire.d ./src/expire.o ./src/foreign-conn.d ./src/foreign-conn.o ./src/foreign-query-btc.d ./src/foreign-query-btc.o ./src/foreign-query.d ./src/foreign-query.o ./src/foreign-rpc.d ./src/foreign-rpc.o ./src/hostdir.d ./src/hostdir.o ./src/mints.d ./src/mints.o ./src/process-xreq.d ./src/process-xreq.o ./src/processblock.d ./src/processblock.o ./src/processtx.d ./src/processtx.o ./src/relay.d ./src/relay.o ./src/seqnum.d ./src/seqnum.o ./src/transact.d ./src/transact.o ./src/witness.d ./src/witness.o

.PHONY: clean-src

