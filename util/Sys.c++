/*	$Id$ */
/*
 * Copyright (c) 1994-1996 Sam Leffler
 * Copyright (c) 1994-1996 Silicon Graphics, Inc.
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
#include "Sys.h"

/*
 * Wrapper functions for C library calls.
 */

bool
Sys::isRegularFile(const char* filename)
{
    struct stat sb;
    return (Sys::stat(filename, sb) >= 0 && (sb.st_mode&S_IFMT) == S_IFREG);
}

bool
Sys::isSocketFile(const char* filename)
{
#ifdef S_IFSOCK
    struct stat sb;
    return (Sys::stat(filename, sb) >= 0 && (sb.st_mode&S_IFMT) == S_IFSOCK);
#else
    return (false);
#endif
}

bool
Sys::isFIFOFile(const char* filename)
{
    struct stat sb;
    return (Sys::stat(filename, sb) >= 0 && (sb.st_mode&S_IFMT) == S_IFIFO);
}

bool
Sys::isFIFOFile(int fd)
{
    struct stat sb;
    return (Sys::fstat(fd, sb) >= 0 && (sb.st_mode&S_IFMT) == S_IFIFO);
}

bool
Sys::isCharSpecialFile(const char* filename, struct stat& sb)
{
#ifdef S_IFCHR
    return (Sys::stat(filename, sb) >= 0 && (sb.st_mode&S_IFMT) == S_IFCHR);
#else
    return (false);
#endif
}

bool
Sys::isCharSpecialFile(const char* filename)
{
    struct stat sb;
    return Sys::isCharSpecialFile(filename, sb);
}

#include <limits.h>

int
Sys::getOpenMax()
{
#if HAS_SYSCONF
    return (int) sysconf(_SC_OPEN_MAX);
#elif HAS_GETDTABLESIZE
    return getdtablesize();
#elif HAS_ULIMIT
    return (int) ulimit(UL_GDESLIM, 0);
#else
    return (_POSIX_OPEN_MAX);
#endif
}
