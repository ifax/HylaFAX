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
#include "FileCache.h"
#include "Sys.h"

u_int FileCache::master = 0;		// master serial number
FileCache* FileCache::cache[4096];	// cache of stat results
#define	CACHESIZE	(sizeof (cache) / sizeof (cache[0]))

					// statistics
u_int FileCache::lookups = 0;		// total # lookups
u_int FileCache::hits = 0;		// # lookups that hit in the cache
u_int FileCache::probes = 0;		// total # probes during lookups
u_int FileCache::displaced = 0;		// # entries reused
u_int FileCache::flushed = 0;		// # entries flushed

void
FileCache::printStats(FILE* fd)
{
    fprintf(fd, "    File cache: %u lookups, %u hits (%.1f%%), %.1f avg probes\r\n"
	, lookups
	, hits
#define	NZ(v)	((v) == 0 ? 1 : (v))
	, (100.*hits)/NZ(lookups)
	, float(lookups+probes)/float(NZ(lookups))
    );
    u_int n = 0;
    u_int space = 0;
    for (u_int i = 0; i < CACHESIZE; i++) {
	const FileCache* fi = cache[i];
	if (fi) {
	    n++;
	    space += sizeof (*fi) + fi->name.length();
	}
    }
    fprintf(fd, "        %u entries (%.1f KB), %u entries displaced, %u entries flushed\r\n"
	, n
	, space / 1024.
	, displaced
	, flushed
    );
}

FileCache::FileCache() {}
FileCache::~FileCache() {}

void
FileCache::reset(void)
{
    for (u_int i = 0; i < CACHESIZE; i++)
	delete cache[i];
    memset(cache, 0, sizeof (cache));
    master = 0;					// doesn't matter???
}

u_int
FileCache::hash(const char* pathname)
{
    u_int h = 0;
    while (*pathname)
	h ^= *pathname++;
    return (h % CACHESIZE);
}

fxBool
FileCache::lookup(const char* pathname, struct stat& sb, fxBool addToCache)
{
    lookups++;
    u_int h = hash(pathname);
    u_int maxprobes = 5;
    FileCache* fi = cache[h];
    FileCache* oldest = fi;
    while (fi && --maxprobes) {
	probes++;
	if (fi->name == pathname) {
	    fi->serial = master++;
	    sb = fi->sb;
	    hits++;
	    return (TRUE);
	}
	if (fi->serial < oldest->serial)
	    oldest = fi;
	h = (u_int)(h*h) % CACHESIZE;
	fi = cache[h];
    }
    /*
     * Pathname not found in the cache.
     */
    if (Sys::stat(pathname, sb) < 0)
	return (FALSE);
    if (addToCache && pathname[0] != '.') {
	if (fi) {
	    fi = oldest;
	    displaced++;
	} else
	    fi = cache[h] = new FileCache;
	fi->name = pathname;
	fi->serial = master++;
	fi->sb = sb;
    }
    return (TRUE);
}

/*
 * Update the file mode for any in-cache entry.
 */
fxBool
FileCache::chmod(const char* pathname, mode_t mode)
{
    if (Sys::chmod(pathname, mode) < 0)
	return (FALSE);
    lookups++;
    u_int h = hash(pathname);
    u_int maxprobes = 5;
    FileCache* fi = cache[h];
    while (fi && --maxprobes) {
	probes++;
	if (fi->name == pathname) {
	    hits++;
	    fi->sb.st_mode = (fi->sb.st_mode&~0777) | (mode&0777);
	    break;
	}
	h = (u_int)(h*h) % CACHESIZE;
	fi = cache[h];
    }
    return (TRUE);
}

/*
 * Update the file ownership for any in-cache entry.
 */
fxBool
FileCache::chown(const char* pathname, uid_t uid, gid_t gid)
{
    /*
     * For BSD-based systems chown is only permitted
     * by the super-user.  We could optimize this work
     * to not swap the effective uid on System V-based
     * systems but it's not worth it since the majority
     * the calls to chown a file will be done only for
     * BSD-based systems (to deal with the filesystem
     * semantics forcing us to manually set the gid on
     * newly created files.
     */
    uid_t ouid = geteuid();
    (void) seteuid(0);
    fxBool ok = (Sys::chown(pathname, uid, gid) >= 0);
    (void) seteuid(ouid);
    if (ok) {
	lookups++;
	u_int h = hash(pathname);
	u_int maxprobes = 5;
	FileCache* fi = cache[h];
	while (fi && --maxprobes) {
	    probes++;
	    if (fi->name == pathname) {
		hits++;
		fi->sb.st_uid = uid;
		fi->sb.st_gid = gid;
		break;
	    }
	    h = (u_int)(h*h) % CACHESIZE;
	    fi = cache[h];
	}
    }
    return (ok);
}

/*
 * Like lookup, but if found in the cache, re-do the stat.
 */
fxBool
FileCache::update(const char* pathname, struct stat& sb, fxBool addToCache)
{
    lookups++;
    u_int h = hash(pathname);
    u_int maxprobes = 5;
    FileCache* fi = cache[h];
    FileCache* oldest = fi;
    while (fi && --maxprobes) {
	probes++;
	if (fi->name == pathname) {
	    if (Sys::stat(pathname, sb) >= 0) {
		hits++;
		fi->serial = master++;
		fi->sb = sb;
		return (TRUE);
	    } else {
		flushed++;
		cache[h] = NULL;
		delete fi;
		return (FALSE);
	    }
	}
	if (fi->serial < oldest->serial)
	    oldest = fi;
	h = (u_int)(h*h) % CACHESIZE;
	fi = cache[h];
    }
    /*
     * Pathname not found in the cache.
     */
    if (Sys::stat(pathname, sb) < 0)
	return (FALSE);
    if (addToCache && pathname[0] != '.') {
	if (fi) {
	    fi = oldest;
	    displaced++;
	} else
	    fi = cache[h] = new FileCache;
	fi->name = pathname;
	fi->serial = master++;
	fi->sb = sb;
    }
    return (TRUE);
}

void
FileCache::flush(const char* pathname)
{
    u_int h = hash(pathname);
    u_int maxprobes = 5;
    FileCache* fi = cache[h];
    while (fi && --maxprobes) {
	if (fi->name == pathname) {
	    flushed++;
	    cache[h] = NULL;
	    delete fi;
	    break;
	}
	h = (u_int)(h*h) % CACHESIZE;
	fi = cache[h];
    }
}
