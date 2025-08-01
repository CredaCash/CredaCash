/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * CCassert.h
*/

#pragma once

#include <stdint.h>

#define CCASSERT(x) ((void)( (x)||(__ccassert(#x, __FILE__, __LINE__),0) ))
#define CCASSERTZ(x) ((void)( (!(x))||(__ccassertz(#x, (uintptr_t)(x), __FILE__, __LINE__),0) ))

#ifdef __cplusplus
extern "C" {
#endif
extern unsigned cc_thread_id();
extern void __ccassert(const char *msg, const char *file, int line);
extern void __ccassertz(const char *msg, uintptr_t x, const char *file, int line);
#ifdef __cplusplus
}
#endif
