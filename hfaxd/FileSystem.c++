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

/*
 * File commands other than transfer.
 */
#include "HylaFAXServer.h"
#include "Sys.h"
#include "tiffio.h"

#include <limits.h>
#include <ctype.h>

/*
 * Delete a file (submitted document or received facsimile).
 */
void
HylaFAXServer::deleCmd(const char* pathname)
{
    struct stat sb;
    SpoolDir* dir = fileAccess(pathname, W_OK, sb);
    if (dir) {
	if (!IS(PRIVILEGED)) {
	    if (!S_ISREG(sb.st_mode)) {
		reply(550, "%s: not a plain file.", pathname);
		return;
	    }
	    if (!dir->deleAble) {
		perror_reply(550, pathname, EPERM);
		return;
	    }
	}
	/*
	 * XXX must decide what to do with jobs/documents/etc...
	 * XXX e.g. should job be treated like jdele; should we
	 * XXX cross-check document use for consistency....
	 */
	if (Sys::unlink(pathname) < 0)
	    perror_reply(550, pathname, errno);
	else {
	    const char* cp = strrchr(pathname, '/');
	    logNotice("%s of %s [%s] deleted %s%s"
		, (const char*) the_user
		, (const char*) remotehost
		, (const char*) remoteaddr
		, dir->pathname, cp ? cp+1 : pathname
	    );
	    ack(250, cmdToken(T_DELE));
	    FileCache::flush(pathname);
	}
    }
}

/*
 * Set ownership of a file.
 */
void
HylaFAXServer::chownCmd(const char* pathname, const char* user)
{
    struct stat sb;
    SpoolDir* dir = fileAccess(pathname, W_OK, sb);
    if (dir) {
	u_int id;
	if (isdigit(user[0]))
	    id = (u_int) atoi(user);
	else if (!userID(user, id))
	    return;
	if (!FileCache::chown(pathname, sb.st_uid, (gid_t) id))
	    perror_reply(550, pathname, errno);
	else
	    ack(250, cmdToken(T_CHOWN));
    }
}

/*
 * Set protection of a file.
 */
void
HylaFAXServer::chmodCmd(const char* pathname, u_int mode)
{
    struct stat sb;
    SpoolDir* dir = fileAccess(pathname, W_OK, sb);
    if (dir) {
	mode = 0600 | (mode&066);
	if (!FileCache::chmod(pathname, mode))
	    perror_reply(550, pathname, errno);
	else
	    ack(250, cmdToken(T_CHMOD));
    }
}

/*
 * Return the last modification time for a file.
 */
void
HylaFAXServer::mdtmCmd(const char* pathname)
{
    struct stat sb;
    SpoolDir* dir = fileAccess(pathname, X_OK, sb);
    if (dir) {
	struct tm* t = cvtTime(sb.st_mtime);
	reply(213, "%d%02d%02d%02d%02d%02d"
	    , t->tm_year+1900
	    , t->tm_mon+1
	    , t->tm_mday
	    , t->tm_hour
	    , t->tm_min
	    , t->tm_sec
	);
    }
}

/*
 * NB: this array is ordered by expected frequency of access.
 */
SpoolDir HylaFAXServer::dirs[] = {
{ "/status/",	false, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listStatus,	&HylaFAXServer::listStatusFile,
  &HylaFAXServer::nlstStatus,	&HylaFAXServer::nlstUnixFile, },
{ "/sendq/",	false, false, false, 0,
  HylaFAXServer::isVisibleSendQFile,
  &HylaFAXServer::listSendQ,	&HylaFAXServer::listSendQFile,
  &HylaFAXServer::nlstSendQ,	&HylaFAXServer::nlstSendQFile, },
{ "/doneq/",	false, false, false, 0,
  HylaFAXServer::isVisibleSendQFile,
  &HylaFAXServer::listSendQ,	&HylaFAXServer::listSendQFile,
  &HylaFAXServer::nlstSendQ,	&HylaFAXServer::nlstSendQFile, },
{ "/docq/",	false,  true,  true, 0,
  HylaFAXServer::isVisibleDocQFile,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/tmp/",	false,  true,  true, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/log/",	false, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/recvq/",	false, false,  true, 0,
  HylaFAXServer::isVisibleRecvQFile,
  &HylaFAXServer::listRecvQ,	&HylaFAXServer::listRecvQFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/archive/",	false, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/pollq/",	false,  true,  true, 0,
  HylaFAXServer::isVisibleRecvQFile,
  &HylaFAXServer::listRecvQ,	&HylaFAXServer::listRecvQFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/",		false, false, false, 0,
  HylaFAXServer::isVisibleRootFile,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/etc/",	 true, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/info/",	false, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/bin/",	 true, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/config/",	false, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
{ "/client/",	 true, false, false, 0,
  HylaFAXServer::isVisibletrue,
  &HylaFAXServer::listDirectory,	&HylaFAXServer::listUnixFile,
  &HylaFAXServer::nlstDirectory,	&HylaFAXServer::nlstUnixFile, },
};
#define	N(a)	(sizeof (a) / sizeof (a[0]))

/*
 * Initialize the directory handling.
 */
void
HylaFAXServer::dirSetup(void)
{
    for (u_int i = 0, n = N(dirs); i < n; i++) {
	struct stat sb;
	if (!FileCache::lookup(dirs[i].pathname, sb) || !S_ISDIR(sb.st_mode)) {
	    logError("%s: Not a directory.", dirs[i].pathname);
	    continue;
	}
	dirs[i].ino = sb.st_ino;
	if (streq(dirs[i].pathname, "/"))
	    cwd = &dirs[i];
    }
}

/*
 * Return a directory handle given a pathname.
 */
SpoolDir*
HylaFAXServer::dirLookup(const char* path)
{
    struct stat sb;
    return (!FileCache::lookup(path, sb) || !S_ISDIR(sb.st_mode) ?
	(SpoolDir*) NULL : dirLookup(sb.st_ino));
}

/*
 * Return a directory handle given an inode number.
 */
SpoolDir*
HylaFAXServer::dirLookup(ino_t ino)
{
    for (u_int i = 0, n = N(dirs); i < n; i++)
	if (dirs[i].ino == ino)
	    return (&dirs[i]);
    return (NULL);
}

/*
 * Check if the client is permitted to do the
 * request operation on the specified file.
 * Operations are: X_OK (list/stat), R_OK (look
 * at the contents), W_OK (write/delete contents).
 *
 * If the operation is permitted the stat result
 * for the target file is returned to the caller
 * for use in futher checks (e.g. type of file).
 */
SpoolDir*
HylaFAXServer::fileAccess(const char* path, int op, struct stat& sb)
{
    if (!FileCache::lookup(path, sb)) {			// file not found
	if (op != W_OK || errno != ENOENT) {
	    perror_reply(550, path, errno);
	    return (NULL);
	}
	/*
	 * The file does not exist, fake up the
	 * information that would result from a
	 * successful file creation--for use below.
	 */
	sb.st_mode = S_IFREG|S_IRGRP|S_IWGRP;
	sb.st_gid = (gid_t) uid;
	sb.st_ino = 0;					// NB: to be created
    }
    /*
     * Validate containing directory and target file.
     * The directory must exist and the client must
     * have permission to access it.  The target file
     * must not be a directory if the client is about
     * to do a read/write-style operation or the target
     * file must be owned by the client (other checks
     * are left to the caller).
     */
    struct stat db;
    const char* cp = strrchr(path, '/');
    if (cp) {
	cp++;						// include "/"
	if (!FileCache::lookup(fxStr(path, cp-path), db)) {
	    perror_reply(550, path, ENOENT);
	    return (NULL);
	}
    } else {
	if (!FileCache::lookup(cwd->pathname, db)) {	// implicit ref to "."
	    perror_reply(550, path, ENOENT);
	    return (NULL);
	}
	cp = path;
    }
    SpoolDir* dir = dirLookup(db.st_ino);
    if (!dir) {						// no containing dir
	perror_reply(550, path, ENOENT);
	return (NULL);
    } else if (!IS(PRIVILEGED)) {			// unprivileged client
	if (dir->adminOnly) {				// requires admin priv's
	    perror_reply(550, path, EPERM);
	    return (NULL);
	} else if (!fileVisible(*dir, cp, sb)) {	// not visible w/o admin
	    perror_reply(550, path, EPERM);
	    return (NULL);
	} else if (op != X_OK) {			// client wants to r/w
	    if (S_ISDIR(sb.st_mode)) {			// cannot r/w directory
		reply(550, "%s: not a plain file.", path);
		return (NULL);
	    } else if (sb.st_ino == 0 && !dir->storAble) {
		perror_reply(550, path, EPERM);		// cannot stor in dir.
		return (NULL);
	    }
	    /*
	     * Check file protection mode; we use the
	     * normal ``other'' bits for public access
	     * and the group bits for the ``fax uid''.
	     */
	    if ((sb.st_mode&op) == 0 &&			// !pubicly accessible
	      (sb.st_gid != (gid_t) uid ||		// !owner
	       ((sb.st_mode>>3)&op) == 0)) {		// !owner acessible
		perror_reply(550, path, EPERM);
		return (NULL);
	    }
	}
    } else {						// privileged client
	if (op != X_OK && S_ISDIR(sb.st_mode)) {	// cannot r/w directory
	    reply(550, "%s: not a plain file.", path);
	    return (NULL);
	}
    }
    return (dir);
}

/*
 * Like fileAccess, but when the target is a directory.
 * The implicit operation is X_OK and the directory handle
 * returned is for the target, not the containing directory.
 */
SpoolDir*
HylaFAXServer::dirAccess(const char* path)
{
    struct stat sb;
    if (!FileCache::lookup(path, sb)) {			// file not found
	perror_reply(550, path, errno);
	return (NULL);
    }
    if (!S_ISDIR(sb.st_mode)) {
	perror_reply(550, path, ENOTDIR);
	return (NULL);
    }
    // NB: this assumes all directories are top-level
    SpoolDir* dir = dirLookup(sb.st_ino);
    if (!dir) {						// XXX should not happen
	perror_reply(550, path, ENOENT);
	return (NULL);
    } else if (dir->adminOnly && !IS(PRIVILEGED)) {	// requires admin priv's
	perror_reply(550, path, EPERM);
	return (NULL);
    }
    return (dir);
}

/*
 * Force the specified file to have the GID setup as
 * the effective GID of the process.  On System-V-style
 * filesystems this happens automatically and there is
 * nothing to do.  On BSD-style filesystems however the
 * file's GID is set from the GID of the containing
 * directory so we must explicitly set the value.  Note
 * that we must auto-detect the appropriate semantics
 * because some systems support both styles and the
 * specific scheme is selectable on a per-filesystem
 * basis (e.g. IRIX).
 */
void
HylaFAXServer::setFileOwner(const char* file)
{
    if (IS(CHECKGID)) {				// auto-detect how GID handled
        struct stat sb;
        if (!FileCache::lookup(file, sb)) {
            fatal("setFileOwner called for non-existent file (check)");
        }
        if (sb.st_gid != (gid_t) uid) {
            state |= S_SETGID;			// not set, must force it
        } else {
            state &= ~S_SETGID;			// set by OS, no work to do
        }
        if (TRACE(SERVER)) {
            logDebug("Filesystem has %s-style file creation semantics.",
                IS(SETGID) ? "BSD" : "SysV");
        }
        state &= ~S_CHECKGID;
    }
    if (IS(SETGID)) {
        struct stat sb;
        if (!FileCache::lookup(file, sb)) {
            fatal("setFileOwner called for non-existent file (set)");
        }
        if (!FileCache::chown(file, sb.st_uid, (gid_t) uid)) {
            logError("%s: chown: %s", file, strerror(errno));
        }
    }
}

/*
 * Is the specified file visible to the client.
 */
bool
HylaFAXServer::fileVisible(const SpoolDir& dir, const char* filename, const struct stat& sb)
{
    return (IS(PRIVILEGED) || (*dir.isVisibleFile)(filename, sb));
}
bool
HylaFAXServer::isVisibletrue(const char*, const struct stat&)
    { return (true); }
bool
HylaFAXServer::isVisibleDocQFile(const char* filename, const struct stat&)
    { return (strncmp(filename, "doc", 3) == 0); }
bool
HylaFAXServer::isVisibleRootFile(const char*, const struct stat& sb)
    { return (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode)); }

/*
 * Change the working (pseudo) directory.
 */
void
HylaFAXServer::cwdCmd(const char* path)
{
    SpoolDir* dir = dirAccess(path);
    if (dir) {
	if (Sys::chdir(path) >= 0) {
	    ack(250, cmdToken(T_CWD));
	    cwd = dir;
	} else
	    perror_reply(550, path, errno);
    }
}

/*
 * Return the path of the current working (pseudo) directory.
 */
void
HylaFAXServer::pwdCmd(void)
{
    u_int len = strlen(cwd->pathname)-1;		// strip trailing "/"
    reply(257, "\"%.*s\" is the current directory.",
	len ? len : len+1, cwd->pathname);
}

/*
 * LIST a directory.
 */
void
HylaFAXServer::listCmd(const char* pathname)
{
    SpoolDir* sd = dirAccess(pathname);
    if (sd) {
	DIR* dir = opendir(pathname);
	if (dir != NULL) {
	    int code;
	    FILE* dout = openDataConn("w", code);
	    if (dout != NULL) {
		reply(code, "%s for \"%s\".", dataConnMsg(code), pathname);
		if (setjmp(urgcatch) == 0) {
		    state |= S_TRANSFER;
		    (this->*sd->listDirectory)(dout, *sd, dir);
		    fflush(dout);
		    reply(226, "Transfer complete.");
		}
		state &= ~S_TRANSFER;
		closeDataConn(dout);
	    }
	    closedir(dir);
	} else if (errno != 0)
	    perror_reply(550, pathname, errno);
	else
	    reply(550, "%s: Cannot open directory.", pathname);
    }
}

void
HylaFAXServer::listDirectory(FILE* fd, const SpoolDir& sd, DIR* dir)
{
    /*
     * Use an absolute pathname when doing file
     * lookups to improve cache locality.
     */
    fxStr path(sd.pathname);
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	if (dp->d_name[0] == '.' &&
	  (dp->d_name[1] == '\0' || strcmp(dp->d_name, "..") == 0))
	    continue;
	struct stat sb;
	if (!FileCache::update(path | dp->d_name, sb))
	    continue;
	if ((*sd.isVisibleFile)(dp->d_name, sb)) {
	    (this->*sd.listFile)(fd, sd, dp->d_name, sb);
	    fputs("\r\n", fd);
	}
    }
}

void
HylaFAXServer::listUnixFile(FILE* fd, const SpoolDir&,
    const char* filename, const struct stat& sb)
{
    Fprintf(fd, fileFormat, filename, sb);
}

static const char fformat[] = {
    's',		// a (last access time)
    'b',		// b
    's',		// c (create time)
    'o',		// d (device)
    'e',		// e
    's',		// f (filename)
    'u',		// g (GID of file)
    'h',		// h
    'u',		// i (inode number)
    'j',		// j
    'k',		// k
    'u',		// l (link count)
    's',		// m (last modification time)
    'n',		// n
    's',		// o (owner based on file GID)
    's',		// p (fax-style protection flags, no group bits)
    's',		// q (UNIX-style protection flags)
    'o',		// r (root device)
    'u',		// s (file size in bytes)
    't',		// t
    'u',		// u (UID of file)
    'v',		// v
    'w',		// w
    'x',		// x
    'y',		// y
    'z'			// z
};

/*
 * Print a formatted string with fields filled in from
 * a file's stat buffer.  This functionality is
 * used to permit clients to get modem status listings
 * in preferred formats.
 */
void
HylaFAXServer::Fprintf(FILE* fd, const char* fmt,
    const char* filename, const struct stat& sb)
{
    for (const char* cp = fmt; *cp; cp++) {
	if (*cp == '%') {
#define	MAXSPEC	20
	    char fspec[MAXSPEC];
	    char* fp = fspec;
	    *fp++ = '%';
	    char c = *++cp;
	    if (c == '-')
		*fp++ = c, c = *++cp;
	    if (isdigit(c)) {
		do {
		    *fp++ = c;
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-3]);
	    }
	    if (c == '.') {
		do {
		    *fp++ = c;
		} while (isdigit(c = *++cp) && fp < &fspec[MAXSPEC-2]);
	    }
	    if (!islower(c)) {
		if (c == '%')		// %% -> %
		    putc(c, fd);
		else
		    fprintf(fd, "%.*s%c", fp-fspec, fspec, c);
		continue;
	    }
	    fp[0] = fformat[c-'a'];	// printf format string
	    fp[1] = '\0';
	    switch (c) {
	    case 'a':
		fprintf(fd, fspec, asctime(cvtTime(sb.st_atime))+4);
		break;
	    case 'c':
		fprintf(fd, fspec, asctime(cvtTime(sb.st_ctime))+4);
		break;
	    case 'd':
		fprintf(fd, fspec, (u_int) sb.st_dev);
		break;
	    case 'f':
		fprintf(fd, fspec, filename);
		break;
	    case 'g':
		fprintf(fd, fspec, (u_int) sb.st_gid);
		break;
	    case 'i':
		fprintf(fd, fspec, (u_int) sb.st_ino);		// XXX
		break;
	    case 'l':
		fprintf(fd, fspec, (u_int) sb.st_nlink);
		break;
	    case 'm':
		fprintf(fd, fspec, asctime(cvtTime(sb.st_mtime))+4);
		break;
	    case 'o':
		fprintf(fd, fspec, userName((u_int) sb.st_gid));
		break;
	    case 'p':
	    case 'q':
		{ char prot[10];				// XXX HP C++
		  makeProt(sb, c == 'q', prot);
		  fprintf(fd, fspec, prot);
		}
		break;
	    case 'r':
		fprintf(fd, fspec, (u_int) sb.st_rdev);
		break;
	    case 's':
		fprintf(fd, fspec, (u_int) sb.st_size);		// XXX
		break;
	    case 'u':
		fprintf(fd, fspec, (u_int) sb.st_uid);
		break;
	    }
	} else
	    putc(*cp, fd);
    }
}

void
HylaFAXServer::makeProt(const struct stat& sb, bool withGrp, char prot[10])
{
    char* pp = prot;
    *pp++ = S_ISREG(sb.st_mode)  ? '-' :
	    S_ISDIR(sb.st_mode)  ? 'd' :
	    S_ISFIFO(sb.st_mode) ? 'p' :
#ifdef S_ISSOCK
	    S_ISSOCK(sb.st_mode) ? 's' :
#endif
				   '?' ;
    *pp++ = (sb.st_mode&S_IRUSR) ? 'r' : '-';
    *pp++ = (sb.st_mode&S_IWUSR) ? 'w' : '-';
    *pp++ = (sb.st_mode&S_IXUSR) ? 'x' : '-';
    if (withGrp) {
	*pp++ = (sb.st_mode&S_IRGRP) ? 'r' : '-';
	*pp++ = (sb.st_mode&S_IWGRP) ? 'w' : '-';
	*pp++ = (sb.st_mode&S_IXGRP) ? 'x' : '-';
    }
    *pp++ = (sb.st_mode&S_IROTH) ? 'r' : '-';
    *pp++ = (sb.st_mode&S_IWOTH) ? 'w' : '-';
    *pp++ = (sb.st_mode&S_IXOTH) ? 'x' : '-';
    *pp++ = '\0';
}

void
HylaFAXServer::statFileCmd(const char* pathname)
{
    struct stat sb;
    SpoolDir* dir = fileAccess(pathname, X_OK, sb);
    if (dir) {
	(void) FileCache::update(pathname, sb);	// insure up to date info
	lreply(211, "Status of %s:", pathname);
	const char* cp = strrchr(pathname, '/');
	(this->*dir->listFile)(stdout, *dir, cp ? cp+1 : pathname, sb);
	fputs("\r\n", stdout);
	reply(211, "End of Status");
    }
}

/*
 * NLST a directory.
 */
void
HylaFAXServer::nlstCmd(const char* pathname)
{
    SpoolDir* sd = dirAccess(pathname);
    if (sd) {
	DIR* dir = opendir(pathname);
	if (dir != NULL) {
	    int code;
	    FILE* dout = openDataConn("w", code);
	    if (dout != NULL) {
		reply(code, "%s for \"%s\".", dataConnMsg(code), pathname);
		if (setjmp(urgcatch) == 0) {
		    state |= S_TRANSFER;
		    (this->*sd->nlstDirectory)(dout, *sd, dir);
		    fflush(dout);
		    reply(226, "Transfer complete.");
		}
		state &= ~S_TRANSFER;
		closeDataConn(dout);
	    }
	    closedir(dir);
	} else if (errno != 0)
	    perror_reply(550, pathname, errno);
	else
	    reply(550, "%s: Cannot open directory.", pathname);
    }
}

void
HylaFAXServer::nlstDirectory(FILE* fd, const SpoolDir& sd, DIR* dir)
{
    /*
     * Use an absolute pathname when doing file
     * lookups to improve cache locality.
     */
    fxStr path(sd.pathname);
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	if (dp->d_name[0] == '.' &&
	  (dp->d_name[1] == '\0' || strcmp(dp->d_name, "..") == 0))
	    continue;
	struct stat sb;
	if (!FileCache::update(path | dp->d_name, sb))
	    continue;
	if ((*sd.isVisibleFile)(dp->d_name, sb)) {
	    (this->*sd.nlstFile)(fd, sd, dp->d_name, sb);
	    fputs("\r\n", fd);
	}
    }
}

void
HylaFAXServer::nlstUnixFile(FILE* fd, const SpoolDir&,
    const char* filename, const struct stat&)
{
    fprintf(fd, "%s", filename);
}

static bool
isTIFF(const TIFFHeader& h)
{
    if (h.tiff_magic != TIFF_BIGENDIAN && h.tiff_magic != TIFF_LITTLEENDIAN)
	return (false);
    union {
	int32	i;
	char	c[4];
    } u;
    u.i = 1;
    uint16 version = h.tiff_version;
    // byte swap version stamp if opposite byte order
    if ((u.c[0] == 0) ^ (h.tiff_magic == TIFF_BIGENDIAN))
	TIFFSwabShort(&version);
    return (version == TIFF_VERSION);
}

bool
HylaFAXServer::docType(const char* docname, FaxSendOp& op)
{
    op = FaxRequest::send_unknown;
    int fd = Sys::open(docname, O_RDONLY);
    if (fd >= 0) {
	struct stat sb;
	if (FileCache::lookup(docname, sb) && S_ISREG(sb.st_mode)) {
	    union {
		char buf[512];
		TIFFHeader h;
	    } b;
	    ssize_t cc = Sys::read(fd, (char*) &b, sizeof (b));
	    if (cc > 2 && b.buf[0] == '%' && b.buf[1] == '!')
		op = FaxRequest::send_postscript;
	    else if (cc > 2 && b.buf[0] == '%' && b.buf[1] == 'P') {
	    	logDebug("What we have here is a PDF file");
	    	op = FaxRequest::send_pdf;
	    }
	    else if (cc > (ssize_t)sizeof (b.h) && isTIFF(b.h))
		op = FaxRequest::send_tiff;
	    else
		op = FaxRequest::send_data;
	}
	Sys::close(fd);
    }
    if (op == FaxRequest::send_unknown)
    	    	logError("Don't know what file");

    return (op != FaxRequest::send_unknown);   
}
