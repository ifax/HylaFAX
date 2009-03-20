#!/usr/bin/awk -f
#	$Id$
#
# HylaFAX Facsimile Software
#
# Copyright (c) 2006 Aidan Van Dyk
# Copyright (c) 2006 iFAX Solutions Inc.
#
# Permission to use, copy, modify, distribute, and sell this software and 
# its documentation for any purpose is hereby granted without fee, provided
# that (i) the above copyright notices and this permission notice appear in
# all copies of the software and related documentation, and (ii) the names of
# Sam Leffler and Silicon Graphics may not be used in any advertising or
# publicity relating to the software without the specific, prior written
# permission of Sam Leffler and Silicon Graphics.
# 
# THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
# EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
# WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
# 
# IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
# ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
# OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
# LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
# OF THIS SOFTWARE.
#

function asc(char, l_found)
{
    l_found = 0
    for (i=0; i < 256; i++)
    {
	if (sprintf("%c", i) == char)
	    l_found = i;
    }
    return l_found;
}

function private_and (a, b, l_res, l_i)
{
    l_res = 0;
    for (l_i = 0; l_i < 8; l_i++)
    {
	if (a%2 == 1 && b%2 == 1)
	    l_res = l_res/2 + 128;
	else
	    l_res /= 2;
	a=int(a/2);
	b=int(b/2);
    }
    return l_res;
}

function private_lshift(x, n)
{
    while (n > 0)
    {
	x *= 2;
	n--;
    }
    return x;
}

function private_rshift(x,n)
{
    while (n > 0)
    {
	x =int(x/2);
	n--;
    };
    return x;
}


# Using B encoding, the actual number of bytes may be less than n if the
# last bytes are part of a multi-byte character according to CHARSET and
# the whole character doesn't fit.
function readbytes(n,   m, s, line, str, __RS) {
#	RS = "\x00";

	m = n;
	while ((s = length(__readbuffer)) == 0 || m > 0) {
		if (s == 0) {
                        ## Some (SCO) awk fill __readbuffer with junk on reading EOF
                        ## So we must check the return of getline and set __readbuffer
			if (getline __readbuffer <= 0)
                                __readbuffer = "";

			if (RT != "")
				__readbuffer = __readbuffer RT;

			if ((s = length(__readbuffer)) == 0)
				break;

			}

		if (s > 0) {
			if (m > s) {
				str = str __readbuffer;
				m = m - s;
				__readbuffer = "";
				}
			else {
				str = str substr(__readbuffer, 1, m);
				__readbuffer = substr(__readbuffer, m+1);
				m = 0;
				}
			}
		}

	# Only multi-byte charset currently supported is 'UTF-8'
	if (ENCODING == "B" && CHARSET == "UTF-8")
	{
	    while (str != "" && private_rshift(asc(substr(__readbuffer,1,1)), 6) == 2)
            {
                l = length(str);
                c = substr(str, l, 1);
                str = substr(str, 1, l-1);
                __readbuffer = c __readbuffer;
            }
	}

	return (str);
}

function base64_write(b1, b2, b3, n,	 r1,r2,r3,r4)
{
    r1 = private_rshift(b1,2)
    r2 = private_lshift(private_and(b1,3),4) + private_rshift(b2,4);
    printf "%c", substr(BASE64,r1+1,1);
    printf "%c", substr(BASE64,r2+1,1);

    if (n > 1)
    {
	r3 = private_lshift(private_and(b2,15),2) + private_rshift(b3,6);
	printf "%c", substr(BASE64,r3+1,1);
    } else
    	printf "="
    if (n > 2)
    {
	r4 = private_and(b3,63);
	printf "%c", substr(BASE64,r4+1,1);
    } else
    	printf "="
}

function base64_encode(input)
{
    while (length(input) > 0)
    {
	if (length(input) == 1)
	{
	    byte1=asc(substr(input,1,1));
	    byte2=0;
	    byte3=0;
	    base64_write(byte1,byte2,byte3, 1);
	}
	if (length(input) == 2)
	{
	    byte1=asc(substr(input,1,1));
	    byte2=asc(substr(input,2,1));
	    byte3=0;
	    base64_write(byte1,byte2,byte3, 2);
	}
	if (length(input) >= 3)
	{
	    byte1=asc(substr(input,1,1));
	    byte2=asc(substr(input,2,1));
	    byte3=asc(substr(input,3,1));
	    base64_write(byte1,byte2,byte3, 3);
	}
	input=substr(input,4);
    }
}

function base64(   __RS, data)
{
	__RS = RS;
	RS = "\xF1\xF2\x00";
	data = readbytes(WIDTH*3/4);
	while (length(data) > 0)
	{
		if (ENCODING == "B")
		    printf "=?" CHARSET "?B?";
		base64_encode(data);
		if (ENCODING == "B")
		    printf "?=";
		printf "\n";
		data = readbytes(WIDTH*3/4);
	}
	RS = __RS;
}

BEGIN {
    if (ENCODING == "")
	ENCODING = "BASE64";
    if (CHARSET == "")
	CHARSET = "UTF-8";
    if (WIDTH == 0)
    {
	if (ENCODING == "B")
	    WIDTH = 75 - 7 - length(CHARSET);
	else
	    WIDTH = 72;
    }

    WIDTH -= (WIDTH % 4)

    BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    base64()
    exit (0);
}
