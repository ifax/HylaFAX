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
 * Support related to server status.
 */
#include "HylaFAXServer.h"
#include "Sys.h"
#include "config.h"

#include <ctype.h>
#include <sys/file.h>

class ModemConfig : public FaxConfig {
public:
    enum {			// ClassModem::SpeakerVolume
	OFF	= 0,		// nothing
	QUIET	= 1,		// somewhere near a dull chirp
	LOW	= 2,		// normally acceptable
	MEDIUM	= 3,		// heard above a stereo
	HIGH	= 4		// ear splitting
    };
    u_int	speakerVolume;		// volume control
    u_int	tracingLevel;		// tracing level w/o session
    u_int	logTracingLevel;	// tracing level during session
    u_int	maxRecvPages;		// max pages to accept on receive
    fxStr	localIdentifier;	// to use in place of FAXNumber
    fxStr	FAXNumber;		// phone number
    fxStr	modemName;		// canonical modem name
    bool	isGettyRunning;		// true if faxgetty responds via FIFO
    fxStr	status;			// from status file

    ModemConfig(const char* name);
    ~ModemConfig() {};

    bool setConfigItem(const char* tag, const char* value);
    void configError(const char* fmt, ...);
    void configTrace(const char* fmt, ...);

    void checkGetty(const char* fifoFile);
};
ModemConfig::ModemConfig(const char* name) : modemName(name)
{
    HylaFAXServer::canonModem(modemName);
    maxRecvPages = (u_int) -1;
    tracingLevel = 0;
    logTracingLevel = 0;
    speakerVolume = QUIET;
}
void ModemConfig::configError(const char*, ...) {}
void ModemConfig::configTrace(const char*, ...) {}

void
ModemConfig::checkGetty(const char* fifoFile)
{
    int fifo;
    Sys::close(fifo = Sys::open(fifoFile, O_WRONLY|O_NDELAY));
    isGettyRunning = (fifo != -1);
}

bool
ModemConfig::setConfigItem(const char* tag, const char* value)
{
    if (streq(tag, "faxnumber"))
	FAXNumber = value;
    else if (streq(tag, "localidentifier"))
	localIdentifier = value;
    else if (streq(tag, "speakervolume")) {	// XXX duplicates code elsewhere
	if (strcasecmp(value, "off") == 0)
	    speakerVolume = OFF;
	else if (strcasecmp(value, "quiet") == 0)
	    speakerVolume = QUIET;
	else if (strcasecmp(value, "low") == 0)
	    speakerVolume = LOW;
	else if (strcasecmp(value, "medium") == 0)
	    speakerVolume = MEDIUM;
	else
	    speakerVolume = HIGH;
    } else if (streq(tag, "tracinglevel"))
	tracingLevel = getNumber(value);
    else if (streq(tag, "logtracinglevel"))
	logTracingLevel = getNumber(value);
    else if (streq(tag, "maxrecvpages"))
	maxRecvPages = getNumber(value);
    return (true);				// avoid complaints
}

void
HylaFAXServer::listStatus(FILE* fd, const SpoolDir& sd, DIR* dir)
{
    KeyStringArray listing;
    /*
     * Check scheduler status.
     */
    int fifo;
    Sys::close(fifo = Sys::open("/" FAX_FIFO, O_WRONLY|O_NDELAY));
    fprintf(fd, "HylaFAX scheduler on %s: %s\r\n"
	, (const char*) hostname
	, fifo != -1 ? "Running" : "Not running"
    );
    /*
     * Cross-check entries in the status directory
     * against the other files required for an operating
     * modem.  There must be a configuration file and,
     * if there is a faxgetty process running, there must
     * be a FIFO special file in the root filesystem.
     */
    fxStr path(sd.pathname);
    struct stat sb;
    fxStr fifoPrefix("/" FAX_FIFO ".");
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	fxStr statusFile(path | dp->d_name);
	if (!FileCache::update(statusFile, sb) || !S_ISREG(sb.st_mode))
	    continue;
	// verify there is a modem config file
	fxStr configFile = fxStr::format("/" FAX_CONFIG ".%s", dp->d_name);
	if (!FileCache::lookup(configFile, sb) || !S_ISREG(sb.st_mode))
	    continue;
	fxStr fifoFile(fifoPrefix | dp->d_name);
	if (!FileCache::lookup(fifoFile, sb) || !S_ISFIFO(sb.st_mode))
	    continue;
	ModemConfig config(dp->d_name);
	config.readConfig(configFile);			// read config file
	config.checkGetty(fifoFile);			// check for faxgetty
	getServerStatus(statusFile, config.status);	// XXX
	if (modemSortFormat.length() == 0) {
	    Mprintf(fd, modemFormat, config);
	    fputs("\r\n", fd);
	} else {
	    fxStackBuffer buf;
	    Mprintf(buf, modemFormat, config);
	    fxStr content(buf, buf.getLength());
	    buf.reset();
	    Mprintf(buf, modemSortFormat, config);
	    fxStr key(buf, buf.getLength());
	    listing.append(KeyString(key, content));
	}
    }
    if (listing.length() > 1)
	listing.qsort();

    for (int i = 0; i < listing.length(); i++)
    {
	fwrite(listing[i], listing[i].length(), 1, fd);
	fputs("\r\n", fd);
    }
}

void
HylaFAXServer::listStatusFile(FILE* fd, const SpoolDir& dir,
    const char* filename, const struct stat& sb)
{
    listUnixFile(fd, dir, filename, sb);
}

void
HylaFAXServer::getServerStatus(const char* fileName, fxStr& status)
{
    int fd = Sys::open(fileName, O_RDONLY);
    if (fd > 0) {
        struct stat sb;
        (void) Sys::fstat(fd, sb);
        status.resize((u_int) sb.st_size);
        char* buff = new char[sb.st_size];
        int n = Sys::read(fd, buff, (size_t) sb.st_size);
        status = buff;
        Sys::close(fd);
        if (n > 0 && status[n-1] == '\n') n--;
        if (n == 0) {
            status = "No status (empty file)";
        } else {
            status.resize(n);
        }
        delete [] buff;
    } else {
        status = "No status (cannot open file)";
    }
}

static const char mformat[] = {
    'a',		// a
    'b',		// b
    'c',		// c
    'd',		// d
    'e',		// e
    'f',		// f
    'g',		// g
    's',		// h (hostname)
    'i',		// i
    'j',		// j
    'k',		// k
    's',		// l (local identifier)
    's',		// m (canonical modem name)
    's',		// n (phone number)
    'o',		// o
    'p',		// p
    'q',		// q
    's',		// r (max recv pages)
    's',		// s (status information)
    's',		// t (server/session tracing level)
    'x',		// u (server tracing level)
    'c',		// v (speaker volume)
    'w',		// w
    'x',		// x
    'y',		// y
    'c'			// z (``*'' if faxgetty is running)
};

/*
 * Print a formatted string with fields filled in from
 * the specified modem state.  This functionality is
 * used to permit clients to get modem status listings
 * in preferred formats.
 */
void
HylaFAXServer::Mprintf(fxStackBuffer& buf, const char* fmt, const ModemConfig& config)
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
		    buf.put(c);
		else
		    buf.fput("%.*s%c", fp-fspec, fspec, c);
		continue;
	    }
	    fp[0] = mformat[c-'a'];	// printf format string
	    fp[1] = '\0';
	    switch (c) {
	    case 'h':
		buf.fput(fspec, (const char*) hostname);
		break;
	    case 'l':
		buf.fput(fspec, (const char*) config.localIdentifier);
		break;
	    case 'm':
		buf.fput(fspec, (const char*) config.modemName);
		break;
	    case 'n':
		buf.fput(fspec, (const char*) config.FAXNumber);
		break;
	    case 'r':
        /* 
        * this is not used for some reason ie -1 represents infinite
		* if (config.maxRecvPages == (u_int) -1) {
		*    tmp = "INF";
		* } else {
		*    tmp = fxStr::format("%u", config.maxRecvPages);
        * }
        */
		buf.fput(fspec, config.maxRecvPages);
		break;
	    case 's':
		buf.fput(fspec, (const char*) config.status);
		break;
            case 't': {
		            fxStr tmp = fxStr::format("%05x:%05x",
                        config.tracingLevel&0xfffff,
                        config.logTracingLevel&0xfffff);
                    buf.fput(fspec, (const char*)tmp);
                }
		        break;
	    case 'v':
		buf.fput(fspec, " QLMH"[config.speakerVolume]);
		break;
	    case 'z':
		buf.fput(fspec, config.isGettyRunning ? '*' : ' ');
		break;
	    }
	} else
	    buf.put(*cp);
    }
}

void
HylaFAXServer::Mprintf(FILE* fd, const char* fmt, const ModemConfig& config)
{
	fxStackBuffer buf;
	Mprintf(buf, fmt, config);
	fwrite((const char*)buf, buf.getLength(), 1, fd);
}


void
HylaFAXServer::nlstStatus(FILE* fd, const SpoolDir& sd, DIR* dir)
{
    /*
     * Cross-check entries in the status directory
     * against the other files required for an operating
     * modem.  There must be a configuration file and,
     * if there is a faxgetty process running, there must
     * be a FIFO special file in the root filesystem.
     */
    fxStr path(sd.pathname);
    struct stat sb;
    fxStr fifoPrefix("/" FAX_FIFO ".");
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	fxStr statusFile(path | dp->d_name);
	if (!FileCache::update(statusFile, sb) || !S_ISREG(sb.st_mode))
	    continue;
	// verify there is a modem config file
	fxStr configFile = fxStr::format("/" FAX_CONFIG ".%s", dp->d_name);
	if (!FileCache::lookup(configFile, sb) || !S_ISREG(sb.st_mode))
	    continue;
	fxStr fifoFile(fifoPrefix | dp->d_name);
	if (!FileCache::lookup(fifoFile, sb) || !S_ISFIFO(sb.st_mode))
	    continue;
	ModemConfig config(dp->d_name);
	config.readConfig(configFile);			// read config file
	config.checkGetty(fifoFile);			// check for faxgetty
	getServerStatus(statusFile, config.status);	// XXX
	Mprintf(fd, "%m\r\n", config);
    }
}
