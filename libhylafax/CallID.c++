/*	$Id$ */
/*
 * Copyright (c) 2004 iFAX Solutions, Inc.
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
#include "CallID.h"

const u_int CallID::NUMBER = 0;
const u_int CallID::NAME = 1;

CallID::CallID (int howmany)
    : _id(howmany)
{
}

CallID::~CallID (void)
{
}

/*
 * We need to manyally copy the array, becasue fxArray implementation doesn't
 * do deep copy on the array, and we get double frees/corruption
 */
void CallID::operator= (const CallID& a)
{
    _id.resize(a._id.length());
    for (u_int i = 0; i < _id.length(); i++)
	_id[i] = a._id[i];
}

void CallID::resize(int i)
{
    _id.resize(i);
}

size_t CallID::makeString(fxStr& output)
{
    output.resize(0);
    for (size_t i = 0; i < _id.length(); i++)
    {
	if (i)
	    output.append('\n');
	output.append(_id[i]);
    }
    return _id.length();
}


const char* CallID::id (int i) const
{
    fxAssert((u_int)i<_id.length(), "Invalid CallID[] index");
    return _id[i];
}

int CallID::length (int i) const
{
    fxAssert((u_int)i<_id.length(), "Invalid CallID[] index");
    return _id[i].length();
}

fxStr& CallID::operator [](int i)
{
    fxAssert((u_int)i<_id.length(), "Invalid CallID[] index");
    return _id[i];
}

bool CallID::isEmpty (void) const
{
    for (u_int i = 0; i < _id.length(); i++)
    {
	if (_id[i].length() != 0)
	    return false;
    }
    return true;
}
size_t CallID::size (void) const
{
    return _id.length();
}
