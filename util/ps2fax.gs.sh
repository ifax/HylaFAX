#! /bin/sh
#	$Id$
#
# HylaFAX Facsimile Software
#
# Copyright (c) 1990-1996 Sam Leffler
# Copyright (c) 1991-1996 Silicon Graphics, Inc.
# HylaFAX is a trademark of Silicon Graphics
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

#
# Convert PostScript to facsimile using Ghostscript.
#
# ps2fax [-o output] [-l pagelength] [-w pagewidth]
#	[-r resolution] [-m maxpages] [-*] [file ...]
#
# We need to process the arguments to extract the input
# files so that we can prepend a prologue file that sets
# up a non-interactive environment.
#
# NB: this shell script is assumed to be run from the
#     top of the spooling hierarchy -- s.t. the etc directory
#     is present.
#

test -f etc/setup.cache || {
    SPOOL=`pwd`
    cat<<EOF

FATAL ERROR: $SPOOL/etc/setup.cache is missing!

The file $SPOOL/etc/setup.cache is not present.  This
probably means the machine has not been setup using the faxsetup(1M)
command.  Read the documentation on setting up HylaFAX before you
startup a server system.

EOF
    exit 1
}
. etc/setup.cache

PS=$GSRIP

fil=
out=ps.fax		# default output filename
pagewidth=1728		# standard fax width
pagelength=297		# default to A4 
vres=98			# default to low res
device=tiffg3		# default to 1D
while test $# != 0
do
    case "$1" in
    -o)	shift; out="$1" ;;
    -w) shift; pagewidth="$1" ;;
    -l) shift; pagelength="$1" ;;
    -r)	shift; vres="$1" ;;
    -m) shift;;				# NB: not implemented
    -1) device=tiffg3 ;;
    -2) ($PS -h | grep tiffg32d >/dev/null 2>&1) \
	    && { device=tiffg32d; } \
	    || { device=tiffg3; }
	;;
    -*)	;;
    *)	fil="$fil $1" ;;
    esac
    shift
done
test -z "$fil" && fil="-"		# read from stdin
case "${pagewidth}x${pagelength}" in
1728x280|1728x279)		# 279.4mm is actually correct...
    paper=letter;;
1728x364) 
    paper=legal;;
*x296|*x297)			# more roundoff problems...
    paper=a4;;
*x364)
    paper=b4;;
*)
    echo "$0: Unsupported page size: $pagewidth x $pagelength";
    exit 254;;			# causes document to be rejected
esac

#
# The sed work fixes bug in Windows-generated
# PostScript that causes certain international
# character marks to be placed incorrectly.
#
#    | $SED -e 's/yAscent Ascent def/yAscent 0 def/g' \
#
# NB: unfortunately it appears to break valid PostScript;
#     so it's been disabled.
#
# Suggestion from "Alan Sparks" <asparks@nss.harris.com>,
# Add the -DFIXEDMEDIA argument to the last command in ps2fax
#
$CAT $fil | $PS -q \
    -sDEVICE=$device \
    -dNOPAUSE \
    -dSAFER=true \
    -sPAPERSIZE=$paper \
    -dFIXEDMEDIA \
    -r204x$vres \
    "-sOutputFile=$out" \
    -
