################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/accounts.cpp \
../src/amounts.cpp \
../src/billets.cpp \
../src/btc_block.cpp \
../src/ccwallet.cpp \
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
../src/walletdb-parameters.cpp \
../src/walletdb-secrets.cpp \
../src/walletdb-totals.cpp \
../src/walletdb-transactions.cpp \
../src/walletdb.cpp \
../src/walletutil.cpp 

OBJS += \
./src/accounts.o \
./src/amounts.o \
./src/billets.o \
./src/btc_block.o \
./src/ccwallet.o \
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
./src/walletdb-parameters.o \
./src/walletdb-secrets.o \
./src/walletdb-totals.o \
./src/walletdb-transactions.o \
./src/walletdb.o \
./src/walletutil.o 

CPP_DEPS += \
./src/accounts.d \
./src/amounts.d \
./src/billets.d \
./src/btc_block.d \
./src/ccwallet.d \
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
./src/walletdb-parameters.d \
./src/walletdb-secrets.d \
./src/walletdb-totals.d \
./src/walletdb-transactions.d \
./src/walletdb.d \
./src/walletutil.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	g++ -std=c++11 -D_DEBUG=1 -I$(CREDACASH_BUILD)/source -I$(CREDACASH_BUILD)/source/ccnode/src -I$(CREDACASH_BUILD)/source/cclib/src -I$(CREDACASH_BUILD)/source/cccommon/src -I$(CREDACASH_BUILD)/source/3rdparty/src -I$(CREDACASH_BUILD)/depends -I$(CREDACASH_BUILD)/depends/gmp -I$(CREDACASH_BUILD)/depends/boost -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra $(CPPFLAGS) -c -fmessage-length=0 -Wno-unused-parameter -Werror=sign-compare -isystem $(CREDACASH_BUILD)/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


