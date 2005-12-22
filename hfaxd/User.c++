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
#include "HylaFAXServer.h"
#include "Sys.h"
#include "RE.h"
#include "config.h"

#include <ctype.h>
#include <pwd.h>
#if HAS_CRYPT_H
#include <crypt.h>
#endif
#include <math.h>

#ifndef CHAR_BIT
#ifdef NBBY
#define	CHAR_BIT	NBBY
#else
#define	CHAR_BIT	8
#endif
#endif /* CHAR_BIT */

/*
 * User Access Control Support.
 */
gid_t	HylaFAXServer::faxuid = 0;		// reserved fax uid
#define	FAXUID_RESV	HylaFAXServer::faxuid	// reserved fax uid

#ifdef HAVE_PAM
extern int
pamconv(int num_msg, STRUCT_PAM_MESSAGE **msg, struct pam_response **resp, void *appdata);
#endif

bool
HylaFAXServer::checkUser(const char* name)
{
    bool check = false;
    FILE* db = fopen(fixPathname(userAccessFile), "r");
    if (db != NULL) {
	check = checkuser(db, name) || checkuser(name);
	fclose(db);
    } else
	logError("Unable to open the user access file %s: %s",
	    (const char*) userAccessFile, strerror(errno));
    /*
     * This causes the user to be prompted for a password
     * and then denied access.  We do this to guard against
     * folks that probe the server looking for valid accounts.
     */
    return (true);
}

static bool
nextRecord(FILE* db, char line[], u_int size)
{
    while (fgets(line, size-1, db)) {
	char* cp = strchr(line, '#');
	if (cp) {			// trim trailing white space */
	    for (cp = strchr(line, '\0'); cp > line; cp--)
		if (!isspace(cp[-1]))
		    break;
	    *cp = '\0';
	}
	if ((cp = strchr(line, '\n')))
	    *cp = '\0';
	if (line[0] != '\0')
	    return (true);
    }
    return (false);
}

bool
HylaFAXServer::checkuser(const char* name)
{
	bool retval=false;

#ifdef HAVE_PAM
	if (pam_chrooted) {
	    logNotice("PAM authentication for %s can't be used for a re-issuance of USER command because of chroot jail\n", name);
	    return false;
	}

	int pamret;
	struct pam_conv conv = {pamconv, NULL};		

	pamret = pam_start(FAX_SERVICE, name, &conv, &pamh);

	if (pamret == PAM_SUCCESS)
		pamret = pam_authenticate(pamh, 0);

	if (pamret == PAM_SUCCESS)
		pamret = pam_acct_mgmt(pamh, 0);

	if (pamret == PAM_SUCCESS) {
		passwd = "";
		pamEnd(pamret);
	} else {
	    passwd = "*";
	    adminwd = "*";
	}
	retval = true;

#endif //HAVE_PAM
	return(retval);
}

/*
 * Check the user name and host name/address against
 * the list of users and hosts that are permitted to
 * user the server and setup password handling.
 */
bool
HylaFAXServer::checkuser(FILE* db, const char* name)
{
    struct stat sb;
    if (Sys::fstat(fileno(db), sb) < 0)
	return (false);
    if (sb.st_mode&077) {	// file must not be publicly readable
	logError("Access control file not mode 600; access denied.");
	return (false);
    }
    uid = FAXUID_ANON;		// anonymous user
    adminwd = "*";		// disallow privileged access

    fxStr dotform  = fxStr::format("%s@%s", name, (const char*) remoteaddr);
    fxStr hostform = fxStr::format("%s@%s", name, (const char*) remotehost);

    rewind(db);
    char line[1024];
    while (nextRecord(db, line, sizeof (line))) {
	/*
	 * Records are of the form:
	 *
	 *    [!]regex[:uid[:passwd[:adminwd]]]
	 *
	 * where regex is a regular expression that must
	 * match a string of the form "user@host" or "user@addr"
	 * (where addr is the dot-notation form of the client
	 * host).  If subsequent fields are present then the
	 * first is treated as the numeric ID for the user,
	 * followed by the encrypted password that the client
	 * must supply.  The next field is the password that
	 * must be presented to gain administrative privileges.
	 *
	 * If the regex is a single word (no @ sign), we take it
	 * as a host only short form for (^.*@<input>$)
	 *
	 * If the first character of the <regex> is a ``!''
	 * then the line specifies user(s) to disallow; a match
	 * causes the user to be rejected w/o a password prompt.
	 * This facility is mainly for backwards compatibility.
	 */
	char* cp;
	bool userandhost = false;
	for (cp = line; *cp && *cp != ':'; cp++)
	    if (*cp == '@') userandhost = true;

	const char* base = &line[line[0] == '!'];
	fxStr pattern(base, cp-base);
	if (! userandhost) {
	    pattern.insert("^.*@");
	    pattern.append("$");
	}
	RE pat(pattern);
	if (line[0] == '!') {		// disallow access on match
	    if (pat.Find(dotform) || pat.Find(hostform))
		return (false);
	} else {			// allow access on match
	    if (pat.Find(dotform) || pat.Find(hostform)) {
		passwd = "";		// no password required
		if (*cp == ':') {	// :uid[:passwd[:adminwd]]
		    if (isdigit(*++cp)) {
			uid = atoi(cp);
			for (; *cp && *cp != ':'; cp++)
			    ;
		    }
		    if (*cp == ':') {	// :passwd[:adminwd]
			for (base = ++cp; *cp && *cp != ':'; cp++)
			    ;
			if (*cp == ':') {
			    passwd = fxStr(base, cp-base);
			    adminwd = cp+1;
			} else
			    passwd = base;
		    } else
			passwd = "";	// no password required
		}
		return (true);
	    }
	}
    }
    passwd = "*";
    return (false);
}

fxDECLARE_PtrKeyDictionary(IDCache, u_int, fxStr)
fxIMPLEMENT_PtrKeyObjValueDictionary(IDCache, u_int, fxStr)

/*
 * Read the host access file and fill the ID cache
 * with entries that map fax UID to name.  We pick
 * names by stripping any host part from matching
 * regex's and by mapping ``.*'' user matches to a
 * generic ``anyone'' name.
 *
 * XXX Maybe should convert RE entries to numeric
 *     equivalent of ID to avoid funky names???
 */
void
HylaFAXServer::fillIDCache(void)
{
    idcache = new IDCache;
    FILE* db = fopen(fixPathname(userAccessFile), "r");
    if (db != NULL) {
	char line[1024];
	while (nextRecord(db, line, sizeof (line))) {
	    if (line[0] == '!')			// ignore ! entries
		continue;
	    char* cp;
	    for (cp = line; *cp && *cp != ':'; cp++)
		;
	    fxStr name(line, cp-line);
	    name.resize(name.next(0, '@'));	// strip @host part
	    if (name == ".*")			// map .* -> ``anyone''
		name = "anyone";
	    if (*cp == ':')
		cp++;
	    u_int id;				// fax UID
	    if (isdigit(*cp))
		id = atoi(cp);
	    else
		id = FAXUID_ANON;
	    (*idcache)[id] = name;
	}
	fclose(db);
    }
}

/*
 * Map fax UID to user name.
 */
const char*
HylaFAXServer::userName(u_int id)
{
    if (id == uid)				// user currently logged in
	return (const char*) the_user;
    if (id == FAXUID_ANON)			// anonymous user
	return "fax";
    if (idcache == NULL)			// load cache from file
	fillIDCache();
    const fxStr* hit = idcache->find(id);	// check cache
    if (!hit) {					// create entry w/ numeric value
	(*idcache)[id] = fxStr((int) id, "%u");
	hit = idcache->find(id);		// new entry
    }
    return (*hit);
}

/*
 * Map user name to fax UID.
 */
bool
HylaFAXServer::userID(const char* name, u_int& id)
{
    if (name == the_user)
	id = uid;
    else if (strcmp(name, "fax") == 0)
	id = FAXUID_ANON;
    else {
	if (idcache == NULL)
	    fillIDCache();
	for (IDCacheIter iter(*idcache); iter.notDone(); iter++)
	    if (iter.value() == name) {
		id = iter.key();
		return (true);
	    }
	return (false);
    }
    return (true);
}

static bool
isAllLower(const char* cp)
{
    while (*cp) {
	if (!islower(*cp))
	    return (false);
	cp++;
    }
    return (true);
}

static void
to64(char* cp, long v, int len)
{
    while (--len >= 0) {
	*cp++ = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"[v&0x3f];
	v >>= 6;
    }
}

bool
HylaFAXServer::cvtPasswd(const char* type, const char* pass, fxStr& result)
{
    if (*pass == '\0') {		// null password *IS* permitted
	result = "";
	return (true);
    }
    if (strlen(pass) <= 5) {
	reply(500, "%s password is too short; use 5-8 characters.", type);
	return (false);
    }
    if (isAllLower(pass)) {
	reply(500, "%s password is all lower-case; use something more.", type);
	return (false);
    }
    srandom((int) Sys::now());
    char salt[9];
    /*
     * Contemporary systems use an extended salt that
     * is distinguished by a leading character (``_'').
     * Older systems use a 2-character salt that results
     * in encrypted strings that are easier to crack.
     */
#ifdef _PASSWORD_EFMT1
    salt[0] = _PASSWORD_EFMT1;
    to64(&salt[1], (long)(29 * 25), 4);
    to64(&salt[5], random(), 4);
#else
    to64(&salt[0], random(), 2);
#endif
    result = crypt(pass, salt);
    return (true);
}

#define	NBPL	(sizeof (u_long) * CHAR_BIT)	// bits/u_long
#define	SetBit(b) (allocated[(b)/NBPL] |= ((u_long) 1)<<((b)%NBPL))
#define	ClrBit(b) (allocated[(b)/NBPL] &= ~(((u_long) 1)<<((b)%NBPL)))
#ifndef howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif
#define	N(a)	(sizeof (a) / sizeof (a[0]))

bool
HylaFAXServer::findUser(FILE* db, const char* user, u_int& newuid)
{
    rewind(db);
    char line[1024];
    u_long allocated[howmany(FAXUID_MAX,NBPL)];
    memset(allocated, 0, sizeof (allocated));
    if (faxuid < FAXUID_MAX)
	SetBit(FAXUID_RESV);			// reserved uid
    else
	logError("Internal error, \"fax\" UID (%u) too large.", faxuid);
    SetBit(FAXUID_ANON);			// anonymous uid is reserved
    while (nextRecord(db, line, sizeof (line))) {
	if (line[0] == '!')
	    continue;
	char* cp;
	for (cp = line; *cp && *cp != ':'; cp++)
	    ;
	if (strncmp(user, line, cp-line) == 0)
	    return (true);
	if (*cp == ':' && isdigit(cp[1])) {	// mark uid as in-use
	    u_int uid = (u_int) atoi(cp+1);
	    SetBit(uid);
	}
    }
    // find unallocated uid
    for (u_int l = 0; l < N(allocated); l++)
	if (allocated[l] != (u_long) -1) {
	    u_int b = 0;
	    for (u_long mask = 1; allocated[l] & mask; mask <<= 1) 
		b++;
	    newuid = (u_int) (l*NBPL + b);
	    return (false);
	}
    newuid = (u_int) -1;			// no more space
    return (false);
}

bool
HylaFAXServer::addUser(FILE* db, const char* user, u_int uid, const char* upass, const char* apass)
{
    const char* templ = "/" FAX_TMPDIR "/uaddXXXXXX";
    char* buff = strcpy(new char[strlen(templ) + 1], templ);
    int fd = Sys::mkstemp(buff);
    fxStr tfile = buff;
    delete [] buff;
    if (fd < 0) {
	reply(550, "Error creating temp file %s: %s.",
	    (const char*) tfile, strerror(errno));
	return (false);
    }
    rewind(db);
    char buf[8*1024];
    int cc;
    while ((cc = Sys::read(fileno(db), buf, sizeof (buf))) > 0)
	if (Sys::write(fd, buf, cc) != cc) {
	    perror_reply(550, "Write error", errno);
	    Sys::close(fd);
	    (void) Sys::unlink(tfile);
	    return (false);
	}
    fxStr line;
    if (*apass != '\0')
	line = fxStr::format("^%s@:%u:%s:%s\n", user, uid, upass, apass);
    else if (*upass != '\0')
	line = fxStr::format("^%s@:%u:%s\n", user, uid, upass);
    else
	line = fxStr::format("^%s@:%u\n", user, uid);
    if (Sys::write(fd, line, line.length()) != (ssize_t)line.length()) {
	perror_reply(550, "Write error", errno);
	Sys::close(fd);
	(void) Sys::unlink(tfile);
	return (false);
    }
    Sys::close(fd);
    if (Sys::rename(tfile, fixPathname(userAccessFile)) < 0) {
	perror_reply(550, "Rename of temp file failed", errno);
	(void) Sys::unlink(tfile);
	return (false);
    }
    return (true);
}

/*
 * Add a new user to the access control file.
 */
void
HylaFAXServer::addUserCmd(const char* user, const char* up, const char* ap)
{
    logcmd(T_ADDUSER, "%s XXXX YYYY", user);
    fxStr upass, apass;
    if (!cvtPasswd("User", up, upass) || !cvtPasswd("Admin", ap, apass))
	return;
    FILE* db = fopen(fixPathname(userAccessFile), "r");
    if (db != NULL) {
	u_int newuid;
	if (findUser(db, user, newuid))
	    reply(500, "User %s is already present.", user);
	else if (newuid == (u_int) -1)
	    reply(500, "Unable to add user %s; out of user IDs.", user);
	else if (addUser(db, user, newuid, upass, apass))
	    reply(200, "User %s added with uid %u.", user, newuid);
	fclose(db);
    } else
	reply(500, "Cannot open user access file %s: %s.",
	    (const char*) userAccessFile, strerror(errno));
}

bool
HylaFAXServer::deleteUser(FILE* db, const char* user)
{
    const char* templ = "/" FAX_TMPDIR "/udelXXXXXX";
    char* buff = strcpy(new char[strlen(templ) + 1], templ);
    int fd = Sys::mkstemp(buff);
    fxStr tfile = buff;
    delete [] buff;
    FILE* ftmp;
    if (fd < 0 || (ftmp = fdopen(fd, "w")) == NULL) {
        reply(550, "Error creating temp file %s: %s.",
	        (const char*)tfile, strerror(errno));
        return (false);
    }
    /*
     * Scan the existing file for the specified user
     * and copy other entries to the temporary file.
     * Once the entry for the user is found, stop
     * scanning line-by-line and just block-copy the
     * remaining part of the file.
     */
    bool found = false;
    rewind(db);
    char line[8*1024];
    while (fgets(line, sizeof (line)-1, db)) {
	if (line[0] != '!') {
	    const char* cp;
	    for (cp = line; *cp && *cp != '\n' && *cp != ':'; cp++)
		;
	    if (strncmp(user, line, cp-line) == 0) {
		found = true;
		break;
	    }
	}
	fputs(line, ftmp);
    }
    int cc;
    while ((cc = fread(line, 1, sizeof (line), db)) > 0)
	fwrite(line, cc, 1, ftmp);
    bool ioError = (fclose(ftmp) != 0);
    if (found) {
	if (ioError)
	    perror_reply(550, "I/O error", errno);
	else if (Sys::rename(tfile, fixPathname(userAccessFile)) < 0)
	    perror_reply(550, "Rename of temp file failed", errno);
	else {
	    return (true);
        }
    } else
	reply(500, "User %s not found in access file.", user);
    (void) Sys::unlink(tfile);
    return (false);
}

/*
 * Remove a user from the access control file.
 */
void
HylaFAXServer::delUserCmd(const char* user)
{
    logcmd(T_DELUSER, "%s", user);
    FILE* db = fopen(fixPathname(userAccessFile), "r");
    if (db != NULL) {
	if (deleteUser(db, user))
	    reply(200, "User %s deleted.", user);
	fclose(db);
    } else
	reply(500, "Cannot open user access file %s: %s.",
	    (const char*) userAccessFile, strerror(errno));
}
