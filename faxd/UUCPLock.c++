/*	$Id$ */
/*
 * Copyright (c) 1990-1996 Sam Leffler
 * Copyright (c) 1991-1996 Silicon Graphics, Inc.
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
#include "UUCPLock.h"
#include "faxApp.h"
#include "Sys.h"
#include "config.h"

#include <sys/param.h>
#include <sys/types.h>
#include <errno.h>
#ifdef HAS_MKDEV
extern "C" {
#include <sys/mkdev.h>
}
#endif
#include <pwd.h>

/*
 * UUCP Device Locking Support.
 *
 * NB: There is some implicit understanding of sizeof (pid_t).
 */

/*
 * Lock files with ascii contents (System V style).
 */
class AsciiUUCPLock : public UUCPLock {
private:
    fxStr	data;			// data to write record in lock file

    void setPID(pid_t);
    bool writeData(int fd);
    bool readData(int fd, pid_t& pid);
public:
    AsciiUUCPLock(const fxStr&, mode_t);
    ~AsciiUUCPLock();
};

/*
 * Lock files with binary contents (BSD style).
 */
class BinaryUUCPLock : public UUCPLock {
private:
    pid_t	data;			// data to write record in lock file

    void setPID(pid_t);
    bool writeData(int fd);
    bool readData(int fd, pid_t& pid);
public:
    BinaryUUCPLock(const fxStr&, mode_t);
    ~BinaryUUCPLock();
};

UUCPLock*
UUCPLock::newLock(const char* type,
    const fxStr& dir, const fxStr& device, mode_t mode)
{
    fxStr pathname(dir);

    if (type[0] == '+') {		// SVR4-style lockfile names
#if defined(HAS_MKDEV) || (defined(major) && defined(minor))
	/*
	 * SVR4-style lockfile names are of the form LK.xxx.yyy.zzz
	 * where xxx is the major device of the filesystem on which
	 * the device resides and yyy and zzz are the major & minor
	 * numbers of the locked device.  This format is used if the
	 * lockfile type is specified with a leading '+'; e.g.
	 * ``+ascii'' or ``+binary''. 
	 */
	struct stat sb;
	Sys::stat(device, sb);
	pathname.append(fxStr::format("/LK.%03d.%03d.%03d",
	    major(sb.st_dev), major(sb.st_rdev), minor(sb.st_rdev)));
	type++;
#else
	faxApp::fatal("No support for SVR4-style UUCP lockfiles");
#endif
    } else {				// everybody else's lockfile names
	u_int l = device.nextR(device.length(), '/');
	pathname.append("/LCK.." | device.token(l, '/'));
	if (type[0] == '-') {		// SCO-style lockfile names
	    /*
	     * Some systems (e.g. SCO) uses upper case letters on modem
	     * control devices, but require that the locking be done on
	     * the lower case device.  If the lock file type is specified
	     * as ``-ascii'' or ``-binary'', etc. then we convert the
	     * generated pathname to reflect this convention.
	     */
	    pathname.lowercase(dir.length()+6);
	    type++;
	}
    }
    if (streq(type, "ascii"))
	return new AsciiUUCPLock(pathname, mode);
    else if (streq(type, "binary"))
	return new BinaryUUCPLock(pathname, mode);
    else
	faxApp::fatal("Unknown UUCP lock file type \"%s\"", type);
    return (NULL);
}

UUCPLock::UUCPLock(const fxStr& pathname, mode_t m) : file(pathname)
{
    mode = m;
    locked = false;

    setupIDs();
}

UUCPLock::~UUCPLock()
{
    unlock();
}

uid_t UUCPLock::UUCPuid = (uid_t) -1;
gid_t UUCPLock::UUCPgid = (gid_t) -1;

void
UUCPLock::setupIDs()
{
    if (UUCPuid == (uid_t) -1) {
	const passwd *pwd = getpwnam("uucp");
	if (!pwd)
	    faxApp::fatal("Can not deduce identity of UUCP");
	UUCPuid = pwd->pw_uid;
	UUCPgid = pwd->pw_gid;
	endpwent();			// paranoia
    }
}
uid_t UUCPLock::getUUCPUid() { setupIDs(); return UUCPuid; }
gid_t UUCPLock::getUUCPGid() { setupIDs(); return UUCPgid; }

time_t UUCPLock::lockTimeout = UUCP_LCKTIMEOUT;
void UUCPLock::setLockTimeout(time_t t) { lockTimeout = t; }

/*
 * Create a lock file.
 */
bool
UUCPLock::create()
{	
    /*
     * We create a separate file and link it to
     * the destination to avoid a race condition.
     */
    fxStr templ = file.head(file.nextR(file.length(), '/'));
    templ.append("/TM.faxXXXXXX");
    char* buff = strcpy(new char[templ.length() + 1], templ);
    int fd = Sys::mkstemp(buff);
    if (fd >= 0) {
	writeData(fd);
#if HAS_FCHMOD
	fchmod(fd, mode);
#else
	Sys::chmod(buff, mode);
#endif
#if HAS_FCHOWN
	fchown(fd, UUCPuid, UUCPgid);
#else
	Sys::chown(buff, UUCPuid, UUCPgid);
#endif
	Sys::close(fd);

	locked = (Sys::link(buff, file) == 0);
	Sys::unlink(buff);
    }
    delete [] buff;
    return (locked);
}

/*
 * Check if the lock file is
 * newer than the specified age.
 */
bool
UUCPLock::isNewer(time_t age)
{
    struct stat sb;
    return (Sys::stat(file, sb) != 0 ? false : Sys::now() - sb.st_mtime < age);
}

/*
 * Create a lock file.  If one already exists, the create
 * time is checked for older than the age time (atime).
 * If it is older, an attempt is made to unlink it and
 * create a new one.
 */
bool
UUCPLock::lock()
{
    if (locked)
	return (false);
    uid_t ouid = geteuid();
    seteuid(0);				// need to be root
    bool ok = create();
    if (!ok)
	ok = check() && create();
    seteuid(ouid);
    return (ok);
}

/*
 * Unlock the device.
 */
void
UUCPLock::unlock()
{
    if (locked) {
	uid_t ouid = geteuid();
	seteuid(0);			// need to be root
	Sys::unlink(file);
	seteuid(ouid);
	locked = false;
    }
}

/*
 * Set a particular owner process
 * pid for an already locked device.
 */
bool
UUCPLock::setOwner(pid_t pid)
{
    bool ok = false;
    if (locked) {
	uid_t ouid = geteuid();
	seteuid(0);			// need to be root
	int fd = Sys::open(file, O_WRONLY);
	if (fd != -1) {
	    if (pid)
		setPID(pid);
	    ok = writeData(fd);
	    Sys::close(fd);
	}
	seteuid(ouid);
    }
    return (ok);
}

/*
 * Check if the owning process exists.
 */
bool
UUCPLock::ownerExists(int fd)
{
    pid_t pid;
    return (readData(fd, pid) && (kill(pid, 0) == 0 || errno != ESRCH));
}

/*
 * Check to see if the lock exists and is still active.
 * Locks are automatically expired after
 * UUCPLock::lockTimeout seconds if the process owner
 * is no longer around.
 */
bool
UUCPLock::check()
{
    int fd = Sys::open(file, O_RDONLY);
    if (fd != -1) {
	if (lockTimeout > 0) {
	    if (isNewer(lockTimeout) || ownerExists(fd)) {
		Sys::close(fd);
		return (false);
	    }
	    Sys::close(fd);
	    logInfo("Purge stale UUCP lock %s", (const char*) file);
	    return (Sys::unlink(file) == 0);
	} else {
	    Sys::close(fd);
	    return (false);
	}
    }
    return (true);
}

/*
 * ASCII lock file interface.
 */
AsciiUUCPLock::AsciiUUCPLock(const fxStr& path, mode_t m)
    : UUCPLock(path, m)
    , data(UUCP_PIDDIGITS+2)
{ setPID(getpid()); }
AsciiUUCPLock::~AsciiUUCPLock() {}

void
AsciiUUCPLock::setPID(pid_t pid)
{
    // XXX should this be %d or %ld? depends on pid_t
    data = fxStr::format("%*d\n", UUCP_PIDDIGITS, pid);
}

bool
AsciiUUCPLock::writeData(int fd)
{
    return (Sys::write(fd, data, UUCP_PIDDIGITS+1) == (UUCP_PIDDIGITS+1));
}

bool
AsciiUUCPLock::readData(int fd, pid_t& pid)
{
    char buf[UUCP_PIDDIGITS+1];
    if (Sys::read(fd, buf, UUCP_PIDDIGITS) == UUCP_PIDDIGITS) {
	buf[UUCP_PIDDIGITS] = '\0';
	pid = atol(buf);	// NB: assumes pid_t is <= 32-bits
	return (true);
    } else
	return (false);
}

/*
 * Binary lock file interface.
 */
BinaryUUCPLock::BinaryUUCPLock(const fxStr& path, mode_t m)
    : UUCPLock(path, m)
{ setPID(getpid()); }
BinaryUUCPLock::~BinaryUUCPLock() {}

void
BinaryUUCPLock::setPID(pid_t pid)
{
    data = pid;			// binary pid of lock holder
}

bool
BinaryUUCPLock::writeData(int fd)
{
    return (Sys::write(fd, (char*) &data, sizeof (data)) == sizeof (data));
}

bool
BinaryUUCPLock::readData(int fd, pid_t& pid)
{
    pid_t data;
    if (Sys::read(fd, (char*) &data, sizeof (data)) == sizeof (data)) {
	pid = data;
	return (true);
    } else
	return (false);
}
