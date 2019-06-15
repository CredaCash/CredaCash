/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2019 Creda Software, Inc.
 *
 * RandTest.h
*/

#pragma once

#define RandTest(x)		((x) && !(rand() % ((x) ? (x) : 1)))
