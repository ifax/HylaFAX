#!/usr/bin/awk -f
# Copyright 2006 Patrice Fournier
# Copyright 2006 iFAX Solutions Inc.

function qp_lc(c)
{
  # Space (32) and tab (9) must be encoded at the end of line.
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

  # Space (32) and tab (9) are only encoded at the end of line.
  for (i = 0; i < 256; i++) {
    c = sprintf("%c", i);
    if (i == 9 || (i >= 32 && i <= 60) || (i >= 62 && i <= 126))
      _qp[c] = c;
    else
      _qp[c] = sprintf("=%02X", i);
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
    while (length(out) > 75)
    {
      end = 75;
      while (substr(out, end, 1) == "=" || substr(out, end-1, 1) == "=")
        end--;
      print substr(out, 1, end) "=";
      out = substr(out, end+1);
    }
  }
  print out;
}

