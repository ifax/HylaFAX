/*	$Id$ */
/*
 * Copyright (c) 1995-1996 Sam Leffler
 * Copyright (c) 1995-1996 Silicon Graphics, Inc.
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
#ifndef _FileCache_
#define	_FileCache_

#include "Str.h"
#include <sys/stat.h>

/*
 * Cache to reduce the number of stat system calls.
 */
struct FileCache {
    fxStr	name;
    struct stat sb;
    u_int	serial;

    static u_int master;		// master serial # generator
    static FileCache* cache[4096];
					// statistics
    static u_int lookups;		// total # lookups
    static u_int hits;			// # lookups that hit in the cache
    static u_int probes;		// total # probes during lookups
    static u_int displaced;		// # entries reused
    static u_int flushed;		// # entries flushed
    static void printStats(FILE*);

    FileCache();
    ~FileCache();

    static u_int hash(const char* pathname);

    static fxBool lookup(const char* pathname, struct stat& sb,
	fxBool addToCache = TRUE);
    static fxBool update(const char* pathname, struct stat& sb,
	fxBool addToCache = TRUE);
    static void flush(const char* pathname);
    static fxBool chmod(const char* pathname, mode_t mode);
    static fxBool chown(const char* pathname, uid_t uid, gid_t gid);
    static void reset(void);
};
#endif /* _FileCache_ */
