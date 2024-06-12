/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2024 Creda Foundation, Inc., or its contributors
 *
 * CCapi.h
*/

#ifndef CCAPI_H_
#define CCAPI_H_

#ifndef CCRESULT
#define CCRESULT std::int32_t
#endif

#ifndef CCAPI
#ifdef CC_DLL_IMPORTS
#ifdef _WIN32
#define CCAPI extern "C" __stdcall __declspec(dllimport) CCRESULT
#else
#define CCAPI extern "C" CCRESULT
#endif // _WIN32
#else
#define CCAPI CCRESULT
#endif // CC_DLL_IMPORTS
#endif // CCAPI

#endif
