/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * assembly.h - assembly language functions and prototypes for supported platforms
 *
 * MULSHIFT32(x, y)    signed multiply of two 32-bit integers (x and y), returns top 32 bits of 64-bit result
 * FASTABS(x)          branchless absolute value of signed integer x
 * CLZ(x)              count leading zeros in x
 * MADD64(sum, x, y)   sum [64-bit] += x [32-bit] * y [32-bit]
 * SHL64(sum, x, y)    64-bit left shift using __int64
 * SAR64(sum, x, y)    64-bit right shift using __int64
 */

#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H

#define ALWAYS_INLINE inline __attribute__((always_inline))

#if defined(__GNUC__) && defined(__arm__) && (__ARM_ARCH >= 7)

#pragma message("Using optimizations for ARM")

typedef long long Word64;

static ALWAYS_INLINE int MULSHIFT32(int x, int y)
{
	/* important rules for smull RdLo, RdHi, Rm, Rs:
	 *     RdHi and Rm can't be the same register
	 *     RdLo and Rm can't be the same register
	 *     RdHi and RdLo can't be the same register
	 * Note: Rs determines early termination (leading sign bits) so if you want to specify
	 *   which operand is Rs, put it in the SECOND argument (y)
	 * For inline assembly, x and y are not assumed to be R0, R1 so it shouldn't matter
	 *   which one is returned. (If this were a function call, returning y (R1) would 
	 *   require an extra "mov r0, r1")
	 */
	int zlow;

	__asm__ __volatile__("smull %0,%1,%2,%3" : "=&r" (zlow), "=r" (y) : "r" (x), "1" (y)) ;

	return y;
}

static ALWAYS_INLINE int FASTABS(int x) 
{
	int t = 0; /* Really is not necessary to initialize only to avoid warning */

	__asm__ __volatile__(
		"eor %0,%2,%2, asr #31;"
		"sub %0,%1,%2, asr #31;"
		: "=&r" (t) 
		: "0" (t), "r" (x)
	 );

	return t;
}

static ALWAYS_INLINE int CLZ(int x)
{
	int count;

	__asm__ __volatile__("clz %0, %1" : "=r" (count) : "r" (x));

	return count;
}

typedef union 
{
	Word64 w64;
	struct {
		unsigned lo32;
		signed hi32;
	} r;
} U64;

static ALWAYS_INLINE Word64 MADD64(Word64 sum64, int x, int y)
{
	U64 u;
	u.w64 = sum64;

	__asm__ __volatile__("smlal %0,%1,%2,%3" : "+&r" (u.r.lo32), "+&r" (u.r.hi32) : "r" (x), "r" (y));

	return u.w64;
}

static ALWAYS_INLINE Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static ALWAYS_INLINE Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#elif defined(__riscv)

#pragma message("Using optimizations for RISC-V")

typedef long long Word64;

static ALWAYS_INLINE int MULSHIFT32(int x, int y)
{
	unsigned int result = 0;

	__asm__ __volatile__("mulh %0, %1, %2" : "=r"(result): "r"(x), "r"(y));

	return result;
}

static ALWAYS_INLINE int FASTABS(int x) 
{
	int sign;

	sign = x >> (sizeof(int) * 8 - 1);
	x ^= sign;
	x -= sign;

	return x;
}

static ALWAYS_INLINE int CLZ(int x)
{
#if defined(__GNUC__)
	return __builtin_clz(x);
#else
	int count;

	if (x == 0) {
		return (sizeof(int) * 8);
	}

	count = 0;
	while (!(x & 0x80000000)) {
		count++;
		x <<= 1;
	} 

	return count;
#endif
}

static ALWAYS_INLINE Word64 MADD64(Word64 sum, int a, int b)
{
	unsigned result_hi = 0;
	unsigned result_lo = 0;
	__asm__ __volatile__("mulh %0, %1, %2" : "=r"(result_hi): "r"(a), "r"(b));
	__asm__ __volatile__("mul  %0, %1, %2" : "=r"(result_lo): "r"(a), "r"(b));

	Word64 result = result_hi;
	result <<= 32;
	result += result_lo;
	result += sum;
	return result;
}

static ALWAYS_INLINE Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static ALWAYS_INLINE Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#elif defined(__xtensa__)

#pragma message("Using optimizations for Xtensa")

typedef long long Word64;

static ALWAYS_INLINE int MULSHIFT32(int x, int y)
{
	int result;

	__asm__ __volatile__("mulsh %0, %1, %2" : "=r" (result) : "r" (x), "r" (y));
	
	return result;
}

static ALWAYS_INLINE int FASTABS(int x)
{
	int result;

	__asm__ __volatile__("abs %0, %1" : "=r" (result) : "r" (x));

	return result;
}

static ALWAYS_INLINE int CLZ(int x)
{
#if defined(__GNUC__)
	return __builtin_clz(x);
#else
	int count;

	if (x == 0) {
		return (sizeof(int) * 8);
	}

	count = 0;
	while (!(x & 0x80000000)) {
		count++;
		x <<= 1;
	} 

	return count;
#endif
}

static ALWAYS_INLINE Word64 MADD64(Word64 sum, int a, int b)
{
	sum += (Word64)a * (Word64)b;
	return sum;
}

static ALWAYS_INLINE Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static ALWAYS_INLINE Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#else

#pragma message("Platform-specific optimizations not found, using generic implementation")

typedef long long Word64;

static ALWAYS_INLINE int MULSHIFT32(int x, int y)
{
	Word64 tmp;
	tmp = ((Word64)x * (Word64)y);
	return tmp >> 32;
}

static ALWAYS_INLINE int FASTABS(int x)
{
	return (x > 0) ? x : -x;
}

static ALWAYS_INLINE int CLZ(int x)
{
#if defined(__GNUC__)
	return __builtin_clz(x);
#else
	int count;

	if (x == 0) {
		return (sizeof(int) * 8);
	}

	count = 0;
	while (!(x & 0x80000000)) {
		count++;
		x <<= 1;
	} 

	return count;
#endif
}

static ALWAYS_INLINE Word64 MADD64(Word64 sum, int a, int b)
{
	sum += (Word64)a * (Word64)b;
	return sum;
}

static ALWAYS_INLINE Word64 SHL64(Word64 x, int n)
{
	return x << n;
}

static ALWAYS_INLINE Word64 SAR64(Word64 x, int n)
{
	return x >> n;
}

#endif

#endif
