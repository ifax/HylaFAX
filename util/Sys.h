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
#ifndef _Sys_
#define	_Sys_

#include "Types.h"

#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include "port.h"			// for anything not in system includes

#if HAS_OSFCN_H
#include <osfcn.h>
#endif

/*
 * Wrapper functions for C library calls.
 *
 * These exist to isolate system dependencies and to insure that
 * proper type casts are done at the call sites.  Note that the
 * actual number of functions in this class could be a lot larger;
 * only those functions that potentially cause portability problems
 * due to missing implicit casts of function parameters are included
 * here.
 */
class Sys {
public:
    static DIR* opendir(const char* dirname)
	{ return ::opendir(dirname); }
    static int chdir(const char* s)	{ return ::chdir(s); }

    static int stat(const char* file, struct stat& sb)
	{ return ::stat(file, &sb); }
    static int fstat(int fd, struct stat& sb)
	{ return ::fstat(fd, &sb); }
    static bool isRegularFile(const char* filename);
    static bool isFIFOFile(const char* filename);
    static bool isSocketFile(const char* filename);
    static bool isFIFOFile(int fd);
    static bool isCharSpecialFile(const char* filename);
    static bool isCharSpecialFile(const char* filename, struct stat&);
    static int link(const char* file1, const char* file2)
	{ return ::link(file1, file2); }
    static int rename(const char* file1, const char* file2)
	{ return ::rename(file1, file2); }
    static int unlink(const char* filename)
	{ return  ::unlink(filename); }
    static int access(const char* path, int mode)
	{ return ::access(path, mode); }

    static int chmod(const char* file, mode_t m)
	{ return ::chmod(file, m); }
    static int chown(const char* file, uid_t u, gid_t g)
	{ return ::chown(file, u, g); }
    static int mkfifo(const char* path, mode_t mode)
	{ return ::mkfifo(path, mode); }
    static int open(const char* filename, int flags, mode_t m = 0)
	{ return ::open(filename, flags, m); }
    static int close(int fd)		{ return ::close(fd); }
    // NB: char* param to read+write for implicit cast when using fxStr's
    static int write(int fd, const char *buf, u_int cc)
	{ return ::write(fd, buf, cc); }
    static int read(int fd, char* buf, unsigned cc)
	{ return ::read(fd, buf, cc); }

    static time_t now(void)		{ return ::time(0); }

    static void execv(const char* path, char* const* argv)
#ifdef CONFIG_BADEXECVPROTO
	{ ::execv(path, (const char**) argv); }
#else
	{ ::execv(path, argv); }
#endif
    static void execve(const char* path, char* const* argv, char* const* envp)
#ifdef CONFIG_BADEXECVEPROTO
	{ ::execve(path, (const char**) argv, (const char**) envp); }
#else
	{ ::execve(path, argv, envp); }
#endif
    static pid_t waitpid(pid_t pid, int& status, int options = 0)
	{ return ::waitpid(pid, &status, options); }
    static void waitpid(pid_t pid)	{ ::waitpid(pid, NULL, 0); }

    static int getopt(int argc, char* const* argv, const char* optstring)
#ifdef CONFIG_BADGETOPTPROTO
	{ return ::getopt(argc, (char**) argv, (char*) optstring); }
#else
	{ return ::getopt(argc, argv, optstring); }
#endif

    static int gethostname(char* name, int namelen)
	{ return ::gethostname(name, namelen); }

    static char* mktemp(char* templ)	{ return ::mktemp(templ); }

    static int mkstemp(char* templ)	{
        int fd = ::mkstemp(templ);
#if defined __GLIBC__ && __GLIBC__ <= 2 && __GLIBC_MINOR__ <= 0
        // Hack for older versions of glibc which do not set the file
        // permissions correctly
        if (fchmod(fd, S_IRUSR | S_IWUSR) == -1) return -1;
#endif
        return fd;
    }

    static FILE* tmpfile()	{ return ::tmpfile(); }
    static FILE* fopen(const char* filename, const char* mode)
	{ return ::fopen(filename, mode); }

    static int getOpenMax();
};
#endif /* _Sys_ */
