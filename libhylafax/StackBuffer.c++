/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
 * HylaFAX is a trademark of Silicon Graphics
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#include "StackBuffer.h"
#include "Str.h"
#include <stdlib.h>

fxStackBuffer::fxStackBuffer(u_int grow)
{
    base = next = buf;
    end = &buf[sizeof(buf)];
    amountToGrowBy = grow ? grow : 500;
}

fxStackBuffer::~fxStackBuffer()
{
    if (base != buf) free(base);
}

fxStackBuffer::fxStackBuffer(const fxStackBuffer& other)
{
    u_int size = other.end - other.base;
    u_int len = other.getLength();
    if (size > sizeof(buf)) {
	base = (char*) malloc(size);
    } else {
	base = &buf[0];
    }
    end = base + size;
    next = base + len;
    memcpy(base, other.base, len);
}

void
fxStackBuffer::addc(char c)
{
    if (next >= end) {
	grow(amountToGrowBy);
    }
    *next++ = c;
}

void
fxStackBuffer::grow(u_int amount)
{
    // insure an acceptable amount of growth
    if (amount < amountToGrowBy) amount = amountToGrowBy;

    // move data into larger piece of memory
    u_int size = end - base;
    u_int len = getLength();
    u_int newSize = size + amount;
    if (base == buf) {
	base = (char*) malloc(newSize);
	memcpy(base, buf, sizeof(buf));
    } else {
	base = (char*) realloc(base, newSize);
    }

    // update position pointers
    end = base + newSize;
    next = base + len;
}

void
fxStackBuffer::put(char const* c, u_int len)
{
    u_int remainingSpace = end - next;
    if  (len > remainingSpace) {
	grow(len - remainingSpace);
    }
    memcpy(next, c, len);
    next += len;
}

void
fxStackBuffer::put(const fxStr& s)
{
    put(s, s.length());
}

void
fxStackBuffer::vput(const char* fmt, va_list ap)
{
    put(fxStr::vformat(fmt, ap));
}

void
fxStackBuffer::fput(const char* fmt ...)
{
    va_list ap;
    va_start(ap, fmt);
    vput(fmt, ap);
    va_end(ap);
}

fxStackBuffer& fxStackBuffer::operator=(const fxStackBuffer& other)
{
    if (&other != this) {
        u_int size = other.end - other.base;
        u_int len = other.getLength();
        if (base != buf) free(base);
        base = (size > sizeof(buf)) ? (char*) malloc(size) : &buf[0];
        end = base + size;
        next = base + len;
        memcpy(base, other.base, len);
   }
   return *this;
}
