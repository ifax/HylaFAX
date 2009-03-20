#!/usr/bin/awk -f
#	$Id$
#
# HylaFAX Facsimile Software
#
# Copyright 2006 Patrice Fournier
# Copyright 2006 iFAX Solutions Inc.
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


function private_rshift(x,n)
{
    while (n > 0)
    {
	x =int(x/2);
	n--;
    };
    return x;
}

function qp_lc(c)
{
  # No special last character handling in Q mode.
  if (ENCODING == "Q")
    return _qp[c];

  # In QP mode, space (32) and tab (9) must be encoded at the end of line.
  if (c == " ")
    return "=20";
  else if (c == "\t")
    return "=09";
  else
    return _qp[c];
}

function qp(c)
{
  return _qp[c];
}

BEGIN {
  FS = "\n";
  if (ENCODING == "")
    ENCODING = "QUOTED-PRINTABLE";
  if (POSITION == "")
    POSITION = "TEXT";
  if (CHARSET == "")
    CHARSET = "UTF-8";
  if (WIDTH == 0)
  {
    if (ENCODING == "Q")
      WIDTH = 75 - 7 - length(CHARSET);
    else
      WIDTH = 75;
  }

  if (ENCODING != "Q")
    POSITION = "";
  # In QP mode, space (32) and tab (9) are only encoded at the end of line.
  for (i = 0; i < 256; i++) {
    c = sprintf("%c", i);
    if (POSITION == "phrase" && (i == 33 || i == 42 || i == 43 || i == 45 || (i >= 47 && i <= 57) || (i >= 65 && i <= 90) || i == 95 || (i >= 97 && i <= 122)))
      _qp[c] = c;
    else if (POSITION != "phrase" && (i == 9 || (i >= 32 && i <= 60) || (i >= 62 && i <= 126)))
      _qp[c] = c;
    else
      _qp[c] = sprintf("=%02X", i);
  }

  # Fixup the encoding for some of the Q encoding characters.
  if (ENCODING == "Q")
  {
    _qp[" "] = "_";
    _qp["\t"] = "=09";
    _qp["?"] = "=3F";
    _qp["_"] = "=5F";

    if (POSITION == "comment")
    {
      _qp["("] = "=28";
      _qp[")"] = "=29";
      _qp["\""] = "=22";
    } 
  }
}

{
  l=0;
  out="";
  len=length();
  if (len > 0)
  {
    for (i=1; i<len; i++)
      out = out qp(substr($0, i, 1));
    out = out qp_lc(substr($0, len, 1));

    # Quoted-Printable lines must be no more than 76 characters
    # (including soft break)
    while (length(out) > WIDTH)
    {
      end = WIDTH;
      while (substr(out, end, 1) == "=" || substr(out, end-1, 1) == "=")
        end--;
      if (ENCODING == "Q" && CHARSET == "UTF-8")
        while (length(out) > 2 && substr(out, end-2, 1) == "=" && private_rshift(substr(out, end-1, 2), 6) == 2)
          end -= 3;

      if (ENCODING == "Q")
        print "=?" CHARSET "?Q?" substr(out, 1, end) "?=";
      else
        print substr(out, 1, end) "=";

      out = substr(out, end+1);
    }
  }
  if (ENCODING == "Q")
    print "=?" CHARSET "?Q?" out "?=";
  else
    print out;
}

