#! /bin/sh
#	$Id$
#
# HylaFAX Facsimile Software
#
# Copyright (c) 1994-1996 Sam Leffler
# Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
# wedged deviceID device 
#
# Do something when a modem looks irretrievably wedged.
#
if [ $# != 2 ]; then
    echo "Usage: $0 deviceID device"
    exit 1
fi

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

devID=$1
device=$2
tty=`basename $device`

($CAT<<EOF
To: FaxMaster
From: The HylaFAX Receive Agent <fax>
Subject: modem on $device appears wedged

The HylaFAX software thinks that there is a problem with the modem
on device $device that needs attention; repeated attempts to
initialize the modem have failed.
EOF

#
# NB: this is for an System V-style system.
#
if [ -f /etc/inittab ]; then
    ed - /etc/inittab<<EOF
/^[^#].*:respawn:.*faxgetty[ ][ ]*$tty/s/respawn/off/
w
q
EOF
    #
    # ed doesn't appear to have consistent exit
    # status under SysV-style systems (does under BSD);
    # this means checking if the above succeeded is
    # problematic.
    #
    if [ $? -ne 0 ] && /bin/kill -1 1; then
	cat<<EOF

The $tty entry in /etc/inittab that spawns faxgetty on $device has
been disabled.  After you have figured out what is wrong you may
want to restart this process.
EOF
    fi
fi

echo ""
echo "Consult the server trace logs for more information on what is happening."
) | 2>&1 $SENDMAIL -t -ffax -oi
