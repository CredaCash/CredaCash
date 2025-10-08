################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/accounts.cpp \
../src/billets.cpp \
../src/btc_block.cpp \
../src/ccwallet.cpp \
../src/ccwallet_dll.cpp \
../src/exchange.cpp \
../src/interactive.cpp \
../src/jsonrpc.cpp \
../src/lpcserve.cpp \
../src/polling.cpp \
../src/rpcserve.cpp \
../src/secrets.cpp \
../src/totals.cpp \
../src/transactions.cpp \
../src/txbuildlist.cpp \
../src/txconn.cpp \
../src/txparams.cpp \
../src/txquery.cpp \
../src/txrpc.cpp \
../src/txrpc_btc.cpp \
../src/walletdb-accounts.cpp \
../src/walletdb-billet-spends.cpp \
../src/walletdb-billets.cpp \
../src/walletdb-exchange.cpp \
../src/walletdb-parameters.cpp \
../src/walletdb-secrets.cpp \
../src/walletdb-totals.cpp \
../src/walletdb-transactions.cpp \
../src/walletdb.cpp \
../src/walletutil.cpp 

CPP_DEPS += \
./src/accounts.d \
./src/billets.d \
./src/btc_block.d \
./src/ccwallet.d \
./src/ccwallet_dll.d \
./src/exchange.d \
./src/interactive.d \
./src/jsonrpc.d \
./src/lpcserve.d \
./src/polling.d \
./src/rpcserve.d \
./src/secrets.d \
./src/totals.d \
./src/transactions.d \
./src/txbuildlist.d \
./src/txconn.d \
./src/txparams.d \
./src/txquery.d \
./src/txrpc.d \
./src/txrpc_btc.d \
./src/walletdb-accounts.d \
./src/walletdb-billet-spends.d \
./src/walletdb-billets.d \
./src/walletdb-exchange.d \
./src/walletdb-parameters.d \
./src/walletdb-secrets.d \
./src/walletdb-totals.d \
./src/walletdb-transactions.d \
./src/walletdb.d \
./src/walletutil.d 

OBJS += \
./src/accounts.o \
./src/billets.o \
./src/btc_block.o \
./src/ccwallet.o \
./src/exchange.o \
./src/interactive.o \
./src/jsonrpc.o \
./src/lpcserve.o \
./src/polling.o \
./src/rpcserve.o \
./src/secrets.o \
./src/totals.o \
./src/transactions.o \
./src/txbuildlist.o \
./src/txconn.o \
./src/txparams.o \
./src/txquery.o \
./src/txrpc.o \
./src/txrpc_btc.o \
./src/walletdb-accounts.o \
./src/walletdb-billet-spends.o \
./src/walletdb-billets.o \
./src/walletdb-exchange.o \
./src/walletdb-parameters.o \
./src/walletdb-secrets.o \
./src/walletdb-totals.o \
./src/walletdb-transactions.o \
./src/walletdb.o \
./src/walletutil.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -DBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccnode/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -c -fmessage-length=0 -Wno-unused-parameter $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/accounts.d ./src/accounts.o ./src/billets.d ./src/billets.o ./src/btc_block.d ./src/btc_block.o ./src/ccwallet.d ./src/ccwallet.o ./src/ccwallet_dll.d ./src/ccwallet_dll.o ./src/exchange.d ./src/exchange.o ./src/interactive.d ./src/interactive.o ./src/jsonrpc.d ./src/jsonrpc.o ./src/lpcserve.d ./src/lpcserve.o ./src/polling.d ./src/polling.o ./src/rpcserve.d ./src/rpcserve.o ./src/secrets.d ./src/secrets.o ./src/totals.d ./src/totals.o ./src/transactions.d ./src/transactions.o ./src/txbuildlist.d ./src/txbuildlist.o ./src/txconn.d ./src/txconn.o ./src/txparams.d ./src/txparams.o ./src/txquery.d ./src/txquery.o ./src/txrpc.d ./src/txrpc.o ./src/txrpc_btc.d ./src/txrpc_btc.o ./src/walletdb-accounts.d ./src/walletdb-accounts.o ./src/walletdb-billet-spends.d ./src/walletdb-billet-spends.o ./src/walletdb-billets.d ./src/walletdb-billets.o ./src/walletdb-exchange.d ./src/walletdb-exchange.o ./src/walletdb-parameters.d ./src/walletdb-parameters.o ./src/walletdb-secrets.d ./src/walletdb-secrets.o ./src/walletdb-totals.d ./src/walletdb-totals.o ./src/walletdb-transactions.d ./src/walletdb-transactions.o ./src/walletdb.d ./src/walletdb.o ./src/walletutil.d ./src/walletutil.o

.PHONY: clean-src

