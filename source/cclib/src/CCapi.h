/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
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
#define CCAPI extern "C" __stdcall __declspec(dllimport) CCRESULT
#else
#define CCAPI CCRESULT
#endif
#endif

#endif
