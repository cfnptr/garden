// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "garden/utf.hpp"

using namespace garden;

/***********************************************************************************************************************
 * Begining of the unicode.org ICU copyrighted code.
 *
 * © 2016 and later: Unicode, Inc. and others.
 * Copyright (C) 1999-2015, International Business Machines
 * Corporation and others.  All Rights Reserved.
 */

/**
 * \def UPRV_BLOCK_MACRO_BEGIN
 * Defined as the "do" keyword by default.
 * @internal
 */
#ifndef UPRV_BLOCK_MACRO_BEGIN
#define UPRV_BLOCK_MACRO_BEGIN do
#endif

/**
 * \def UPRV_BLOCK_MACRO_END
 * Defined as "while (false)" by default.
 * @internal
 */
#ifndef UPRV_BLOCK_MACRO_END
#define UPRV_BLOCK_MACRO_END while (false)
#endif

/**
 * Define UChar32 as a type for single Unicode code points.
 * UChar32 is a signed 32-bit integer (same as int32_t).
 *
 * The Unicode code point range is 0..0x10ffff.
 * All other values (negative or >=0x110000) are illegal as Unicode code points.
 * They may be used as sentinel values to indicate "done", "error"
 * or similar non-code point conditions.
 *
 * Before ICU 2.4 (Jitterbug 2146), UChar32 was defined
 * to be wchar_t if that is 32 bits wide (wchar_t may be signed or unsigned)
 * or else to be uint32_t.
 * That is, the definition of UChar32 was platform-dependent.
 *
 * @see U_SENTINEL
 * @stable ICU 2.4
 */
typedef int32 UChar32;

/**
 * This value is intended for sentinel values for APIs that
 * (take or) return single code points (UChar32).
 * It is outside of the Unicode code point range 0..0x10ffff.
 * 
 * For example, a "done" or "error" value in a new API
 * could be indicated with U_SENTINEL.
 *
 * ICU APIs designed before ICU 2.4 usually define service-specific "done"
 * values, mostly 0xffff.
 * Those may need to be distinguished from
 * actual U+ffff text contents by calling functions like
 * CharacterIterator::hasNext() or UnicodeString::length().
 *
 * @return -1
 * @see UChar32
 * @stable ICU 2.4
 */
#define U_SENTINEL (-1)

/**
 * Internal bit vector for 3-byte UTF-8 validity check, for use in U8_IS_VALID_LEAD3_AND_T1.
 * Each bit indicates whether one lead byte + first trail byte pair starts a valid sequence.
 * Lead byte E0..EF bits 3..0 are used as byte index,
 * first trail byte bits 7..5 are used as bit index into that byte.
 * @see U8_IS_VALID_LEAD3_AND_T1
 * @internal
 */
#define U8_LEAD3_T1_BITS "\x20\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x10\x30\x30"
/**
 * Internal bit vector for 4-byte UTF-8 validity check, for use in U8_IS_VALID_LEAD4_AND_T1.
 * Each bit indicates whether one lead byte + first trail byte pair starts a valid sequence.
 * First trail byte bits 7..4 are used as byte index,
 * lead byte F0..F4 bits 2..0 are used as bit index into that byte.
 * @see U8_IS_VALID_LEAD4_AND_T1
 * @internal
 */
#define U8_LEAD4_T1_BITS "\x00\x00\x00\x00\x00\x00\x00\x00\x1E\x0F\x0F\x0F\x00\x00\x00\x00"

/**
 * Does this code unit (byte) encode a code point by itself (US-ASCII 0..0x7f)?
 * @param c 8-bit code unit (byte)
 * @return true or false
 * @stable ICU 2.4
 */
#define U8_IS_SINGLE(c) ((int8)(c)>=0)


/**
 * Is this code point a Unicode noncharacter?
 * https://www.unicode.org/glossary/#noncharacter
 *
 * @param c 32-bit code point
 * @return true or false
 * @stable ICU 2.4
 */
#define U_IS_UNICODE_NONCHAR(c) \
    ((c)>=0xfdd0 && \
     ((c)<=0xfdef || ((c)&0xfffe)==0xfffe) && (c)<=0x10ffff)

/**
 * Is c a Unicode code point value (0..U+10ffff)
 * that can be assigned a character?
 *
 * Code points that are not characters include:
 * - single surrogate code points (U+d800..U+dfff, 2048 code points)
 * - the last two code points on each plane (U+__fffe and U+__ffff, 34 code points)
 * - U+fdd0..U+fdef (new with Unicode 3.1, 32 code points)
 * - the highest Unicode code point value is U+10ffff
 *
 * This means that all code points below U+d800 are character code points,
 * and that boundary is tested first for performance.
 *
 * @param c 32-bit code point
 * @return true or false
 * @stable ICU 2.4
 */
#define U_IS_UNICODE_CHAR(c) \
    ((uint32)(c)<0xd800 || \
        (0xe000<=(c) && (c)<=0x10ffff && !U_IS_UNICODE_NONCHAR(c)))

/** @internal */
#define U8_INTERNAL_NEXT_OR_SUB(s, i, length, c, sub) UPRV_BLOCK_MACRO_BEGIN { \
    (c)=(uint8)(s)[(i)++]; \
    if(!U8_IS_SINGLE(c)) { \
        uint8 __t = 0; \
        if((i)!=(length) && \
            /* fetch/validate/assemble all but last trail byte */ \
            ((c)>=0xe0 ? \
                ((c)<0xf0 ?  /* U+0800..U+FFFF except surrogates */ \
                    U8_LEAD3_T1_BITS[(c)&=0xf]&(1<<((__t=(s)[i])>>5)) && \
                    (__t&=0x3f, 1) \
                :  /* U+10000..U+10FFFF */ \
                    ((c)-=0xf0)<=4 && \
                    U8_LEAD4_T1_BITS[(__t=(s)[i])>>4]&(1<<(c)) && \
                    ((c)=((c)<<6)|(__t&0x3f), ++(i)!=(length)) && \
                    (__t=(s)[i]-0x80)<=0x3f) && \
                /* valid second-to-last trail byte */ \
                ((c)=((c)<<6)|__t, ++(i)!=(length)) \
            :  /* U+0080..U+07FF */ \
                (c)>=0xc2 && ((c)&=0x1f, 1)) && \
            /* last trail byte */ \
            (__t=(s)[i]-0x80)<=0x3f && \
            ((c)=((c)<<6)|__t, ++(i), 1)) { \
        } else { \
            (c)=(sub);  /* ill-formed*/ \
        } \
    } \
} UPRV_BLOCK_MACRO_END

/**
 * Get a code point from a string at a code point boundary offset,
 * and advance the offset to the next code point boundary.
 * (Post-incrementing forward iteration.)
 * "Safe" macro, checks for illegal sequences and for string boundaries.
 *
 * The length can be negative for a NUL-terminated string.
 *
 * The offset may point to the lead byte of a multi-byte sequence,
 * in which case the macro will read the whole sequence.
 * If the offset points to a trail byte or an illegal UTF-8 sequence, then
 * c is set to a negative value.
 *
 * @param s const uint8_t * string
 * @param i int32_t string offset, must be i<length
 * @param length int32_t string length
 * @param c output UChar32 variable, set to <0 in case of an error
 * @see U8_NEXT_UNSAFE
 * @stable ICU 2.4
 */
#define U8_NEXT(s, i, length, c) U8_INTERNAL_NEXT_OR_SUB(s, i, length, c, U_SENTINEL)

/**
 * Append a code point to a string, overwriting 1 to 4 bytes.
 * The offset points to the current end of the string contents
 * and is advanced (post-increment).
 * "Safe" macro, checks for a valid code point.
 * If a non-ASCII code point is written, checks for sufficient space in the string.
 * If the code point is not valid or trail bytes do not fit,
 * then isError is set to true.
 *
 * @param s const uint8_t * string buffer
 * @param i int32_t string offset, must be i<capacity
 * @param capacity int32_t size of the string buffer
 * @param c UChar32 code point to append
 * @param isError output UBool set to true if an error occurs, otherwise not modified
 * @see U8_APPEND_UNSAFE
 * @stable ICU 2.4
 */
#define U8_APPEND(s, i, capacity, c, isError) UPRV_BLOCK_MACRO_BEGIN { \
    uint32 __uc=(c); \
    if(__uc<=0x7f) { \
        (s)[(i)++]=(uint8)__uc; \
    } else if(__uc<=0x7ff && (i)+1<(capacity)) { \
        (s)[(i)++]=(uint8)((__uc>>6)|0xc0); \
        (s)[(i)++]=(uint8)((__uc&0x3f)|0x80); \
    } else if((__uc<=0xd7ff || (0xe000<=__uc && __uc<=0xffff)) && (i)+2<(capacity)) { \
        (s)[(i)++]=(uint8)((__uc>>12)|0xe0); \
        (s)[(i)++]=(uint8)(((__uc>>6)&0x3f)|0x80); \
        (s)[(i)++]=(uint8)((__uc&0x3f)|0x80); \
    } else if(0xffff<__uc && __uc<=0x10ffff && (i)+3<(capacity)) { \
        (s)[(i)++]=(uint8)((__uc>>18)|0xf0); \
        (s)[(i)++]=(uint8)(((__uc>>12)&0x3f)|0x80); \
        (s)[(i)++]=(uint8)(((__uc>>6)&0x3f)|0x80); \
        (s)[(i)++]=(uint8)((__uc&0x3f)|0x80); \
    } else { \
        (isError)=true; \
    } \
} UPRV_BLOCK_MACRO_END

/**
 * End of the unicode.org ICU copyrighted code.
 **********************************************************************************************************************/

psize UTF::utf32toUtf8(u32string_view utf32, string& utf8)
{
	if (utf32.empty())
	{
		utf8.resize(0);
		return 0;
	}

	auto srcLength = utf32.length(); auto src = utf32.data();
	auto dstCapacity = utf32.size() * 4;
	utf8.resize(dstCapacity); auto dst = (uint8*)utf8.data();

	psize dstLength = 0; bool isError = false;
	for (psize i = 0; i < srcLength; i++)
	{
		auto c = (UChar32)src[i];
		U8_APPEND(dst, dstLength, dstCapacity, c, isError);
		if (isError) return i;
	}

	utf8.resize(dstLength);
	return 0;
}
psize UTF::validateUTF8(string_view utf8)
{
	auto length = utf8.length();
	auto data = (const uint8*)utf8.data();

	psize i = 0; UChar32 c = 0;
	while (i < length)
	{
		U8_NEXT(data, i, length, c);
		if (c < 0) return i;
	}
	return 0;
}

//**********************************************************************************************************************
psize UTF::utf8toUtf32(string_view utf8, u32string& utf32)
{
	if (utf8.empty())
	{
		utf32.resize(0);
		return 0;
	}

	auto srcLength = utf8.length(); auto src = (const uint8*)utf8.data();
	utf32.resize(srcLength); auto dst = utf32.data();

	psize i = 0, dstLength = 0; UChar32 c = 0;
	while (i < srcLength)
	{
		U8_NEXT(src, i, srcLength, c);
		if (c < 0) return i;
		dst[dstLength++] = (char32_t)c;
	}

	utf32.resize(dstLength);
	return 0;
}
psize UTF::validateUTF32(u32string_view utf32)
{
	auto length = utf32.length(); auto data = utf32.data();
	for (psize i = 0; i < length; i++)
	{
		if (!U_IS_UNICODE_CHAR(data[i]))
			return i;
	}
	return 0;
}