/*      $Id$ */
/*
 * Copyright (c) 2008 iFAX Solutions, Inc.
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
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "Range.h"
#include "StackBuffer.h"
#include "SystemLog.h"

#ifdef DEBUGGING
#define TRACE(fmt, args...) logDebug(fmt, ##args)
#else
#define TRACE(...) do {} while (0)
#endif

Range::Range (unsigned int start, unsigned int end)
  : min(start)
  , max(end)
  , set(false)
{
	unsigned int size = end - start;

	TRACE("Making map for %lu of %u bytes\n", size, (size / 8 ));

	map = (unsigned char*)malloc((size+7) / 8);
	TRACE("malloc returned a map of %p", map);

	fxAssert(map != NULL, "Couldn't malloc range map");
}

bool Range::parse (const char* parse_string)
{
	unsigned int size = max - min;

	TRACE("Zeroing map");
	memset(map, 0, size/8);
	TRACE("Zeroing done");

	char* endptr;
	long val;
	int last;

	char op = ',';

	do {
		errno = 0;
		val = strtol(parse_string, &endptr, 10);
		TRACE("---> start(%p): \"%s\"    end(%p): \"%s\" val: %ld	errno: %d\n",
				parse_string, parse_string, endptr, endptr, val, errno);
		if (errno)
			break;

		if (val > max)
			val = max;
		if (val < min)
			val = min;

		switch (op) {
		case ',':
			setMapBit(val - min);
			break;
		case '-':
			for (int i = last; i <= val; i++)
				setMapBit(i-min);
			break;
		default:
			TRACE("Don't understand op: %c[%X]\n", op, op);
		}

		while (isspace(*endptr))
			endptr++;
		op = *endptr++;
		while (isspace(*endptr))
			endptr++;


		parse_string = endptr;
		last = val;
	} while (op);
	set = true;
	return true;
}

Range::~Range (void)
{
	if (map)
		free(map);
}


void Range::setMapBit(unsigned int b)
{
	unsigned bb = b / 8;
	unsigned bc = (b % 8) + 1;

	TRACE(" - %2d: Flipping bit %d of index %d\n",
			b, bc, bb);

	map[bb] |= (1<<(bc-1));
}

bool Range::getMapBit(unsigned int b)
{
	if (b > max-min)
	    return false;
	unsigned bb = b / 8;
	unsigned bc = (b % 8) + 1;

#ifdef DEBUGGING
	fxStackBuffer buf;
	buf.fput("MAP: ");
	for (int i = 0; i < ((max-min)+7)/8; i++)
		buf.fput("   %02X   ", map[i] & 0xFF);
	TRACE("%s", (const char*)buf);
#endif

	TRACE(" - %2d: Checking bit %d of index %d: %X\n",
			b, bc, bb, map[bb]);


	return (map[bb] & (1<<(bc-1)));
}


#define PRINT_OR_SYSLOG(fd, buf)                                    \
	if (fd)                                                     \
	{                                                           \
		fwrite((const char*)buf, buf.getLength(), 1, fd);   \
		fputc('\n', fd);                                    \
	} else logDebug("%s", (const char*)buf);

void Range::dump (FILE* fd)
{
	unsigned int size = max - min;
	fxStackBuffer buf;

	buf.fput("RANGE: %u - %u (%d bits)", min, max, size);
	PRINT_OR_SYSLOG(fd, buf);

	buf.reset();
	buf.fput("MAP: ");
	for (int i = 0; i < ((size+9)/10); i++)
		buf.fput("/  %3d   \\", (i+1)*10);
	PRINT_OR_SYSLOG(fd, buf);

	buf.reset();
	buf.fput("MAP: ");
	for (int i = 0; i < size; i++)
	{
		int b = i/8;
		int c = i%8;
		buf.fput("%c", (map[b] & (1<<c)) ? '1' : '0');
	}
	PRINT_OR_SYSLOG(fd, buf);

	buf.reset();
	buf.fput("MAP: ");
	for (int i = 0; i < ((size+7)/8); i++)
	{
		buf.fput("\\  %2.2X  /", i);
	}
	PRINT_OR_SYSLOG(fd, buf);
}

bool Range::contains (unsigned int v)
{
	if (! set)
		return true;
	return getMapBit(v-min);
}
