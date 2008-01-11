/*	$Id: */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
 * Copyright (c) 2003 iFax Solutions, Inc.
 *
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

#include "config.h"
#include "Str.h"
#include "Sys.h"
#include "SystemLog.h"
#include "Sequence.h"

#include <sys/file.h>
#include <errno.h>


const fxStr Sequence::format("%09u");

u_long Sequence::getNext(const char* name, fxStr& emsg)
{
    struct stat sb;
    int fd;
    int rtn = lstat(name, &sb);
    if (rtn != 0 && errno == ENOENT) {
        fd = Sys::open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    } else if (rtn == 0 && S_ISREG(sb.st_mode)) {
        fd = Sys::open(name, O_RDWR, 0600);
        struct stat sb2;
        if (fd < 0 || fstat(fd, &sb2)) {
            //XXX some kind of error opening file
            fd = -1;
        } else if (sb.st_ino != sb2.st_ino || sb.st_dev != sb2.st_dev) {
            //XXX something wrong with file
            fd = -1;
        }
    } else {
        //XXX some kind of error opening file
        fd = -1;
    }
    if (fd < 0) {
        emsg = fxStr::format(_("Unable to open sequence number file %s; %s."),
            name, strerror(errno));
        logError("%s: open: %s", name, strerror(errno));
        return ((u_long) -1);
    }
    flock(fd, LOCK_EX);
    u_long seqnum = 1;
    char line[1024];
    int n = read(fd, line, sizeof (line));
    line[n < 0 ? 0 : n] = '\0';
    if (n > 0) {
        seqnum = atol(line);
    }
    if (seqnum < 1 || seqnum >= MAXSEQNUM) {
        logWarning(_("%s: Invalid sequence number \"%s\", resetting to 1"),
            name, line);
        seqnum = 1;
    }
    fxStr line2 = fxStr::format("%u", NEXTSEQNUM(seqnum));
    lseek(fd, 0, SEEK_SET);
    int len = line2.length();
    if (Sys::write(fd, (const char*)line2, len) != len ||
            ftruncate(fd, len)) {
        emsg = fxStr::format(
            _("Unable update sequence number file %s; write failed."), name);
        logError("%s: Problem updating sequence number file", name);
        return ((u_long) -1);
    }
    Sys::close(fd);			// NB: implicit unlock
    return (seqnum);
}
