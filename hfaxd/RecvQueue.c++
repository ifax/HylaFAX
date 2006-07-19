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
 * Support related to received facsimile.
 */
#include "HylaFAXServer.h"
#include "Sys.h"
#include "Socket.h"
#include "tiffio.h"

#include <ctype.h>
#include <sys/file.h>

RecvInfo::RecvInfo()
{
    beingReceived = false;
    recvTime = 0;
}
RecvInfo::RecvInfo(const char* qf)
{
    qfile = qf;
    beingReceived = false;
    recvTime = 0;
}
RecvInfo::~RecvInfo() {}

fxIMPLEMENT_StrKeyPtrValueDictionary(RecvInfoDict, RecvInfo*)


/*
 * This tests whether the tif file is a "fax" image.
 * Traditional fax images are MH, MR, MMR, but some can
 * be JPEG, JBIG, and possibly others.  So we use
 * TIFFTAG_FAXRECVPARAMS as a "fax" image identifier,
 * and if it's not there, then we resort to traditional
 * tactics.
 */
static bool
isFAXImage(TIFF* tif)
{
#ifdef TIFFTAG_FAXRECVPARAMS
    uint32 v;
    if (TIFFGetField(tif, TIFFTAG_FAXRECVPARAMS, &v) && v != 0)
	return (true);
#endif
    uint16 w;
    if (TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &w) && w != 1)
	return (false);
    if (TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &w) && w != 1)
	return (false);
    if (!TIFFGetField(tif, TIFFTAG_COMPRESSION, &w) ||
      (w != COMPRESSION_CCITTFAX3 && w != COMPRESSION_CCITTFAX4))
	return (false);
    if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &w) ||
      (w != PHOTOMETRIC_MINISWHITE && w != PHOTOMETRIC_MINISBLACK))
	return (false);
    return (true);
}

/*
 * Construct receive information from a file's contents.
 */
bool
HylaFAXServer::getRecvDocStatus(RecvInfo& ri)
{
    int fd = Sys::open(ri.qfile, O_RDWR);	// RDWR for flock emulation
    if (fd < 0)
	return (false);
    /*
     * Files that are being received are locked
     * for exclusive use by faxgetty.  
     */
    ri.beingReceived = (flock(fd, LOCK_SH|LOCK_NB) < 0 && errno == EWOULDBLOCK);
    TIFF* tif = TIFFFdOpen(fd, ri.qfile, "r");
    if (!tif) {
	Sys::close(fd);
	/*
	 * File may not have an IFD written yet,
	 * if it's locked just assume so...
	 */
	return (ri.beingReceived);
    }
    /*
     * We know that faxgetty will write received
     * data in only a limited set for formats.
     */
    if (!isFAXImage(tif)) {
	TIFFClose(tif);
	return (false);
    }
    /*
     * Should be a received facsimile, build up status.
     * Note that certain information was not recorded
     * in older versions of the software; thus the careful
     * checks for certain tags and their values.
     */
    uint32 v;
#ifdef TIFFTAG_FAXRECVPARAMS
    if (TIFFGetField(tif, TIFFTAG_FAXRECVPARAMS, &v))
	ri.params.decode((u_int) v);			// page transfer params
    else {
#endif
    float vres = 3.85;					// XXX default
    if (TIFFGetField(tif, TIFFTAG_YRESOLUTION, &vres)) {
	uint16 resunit = RESUNIT_INCH;			// TIFF spec default
	TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_INCH)
	    vres /= 25.4;
	if (resunit == RESUNIT_NONE)
	    vres /= 720.0;				// postscript units ?
    }
    float hres = 8.03;					// XXX default
    if (TIFFGetField(tif, TIFFTAG_XRESOLUTION, &hres)) {
	uint16 resunit = RESUNIT_INCH;			// TIFF spec default
	TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
	if (resunit == RESUNIT_INCH)
	    hres /= 25.4;
	if (resunit == RESUNIT_NONE)
	    hres /= 720.0;				// postscript units ?
    }
    ri.params.setRes((u_int) hres, (u_int) vres);	// resolution
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &v);
    ri.params.setPageWidthInPixels((u_int) v);		// page width
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &v);
    ri.params.setPageLengthInMM((u_int)(v / vres));	// page length
#ifdef TIFFTAG_FAXRECVPARAMS
    }
#endif
    char* cp;
#ifdef TIFFTAG_FAXDCS
    if (TIFFGetField(tif, TIFFTAG_FAXDCS, &cp) && strncmp(cp, "00 00 00", 8) != 0) {
	// cannot trust br from faxdcs as V.34-Fax does not provide it there
	u_int brhold = ri.params.br;
	fxStr faxdcs(cp);
	sanitize(faxdcs);
	ri.params.asciiDecode((const char*) faxdcs);	// params per Table 2/T.30
	ri.params.setFromDCS(ri.params);
	ri.params.br = brhold;
    }
#endif
    ri.sender = "";
    CallID empty_callid;
    ri.callid = empty_callid;
    if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &cp)) {
	while (cp[0] != '\0' && cp[0] != '\n') {	// sender 
	    ri.sender.append(cp[0]);
	    cp++;
	}
	sanitize(ri.sender);
	u_int i = 0;
	while (cp[0] == '\n') {
	    cp++;
	    ri.callid.resize(i+1);
	    while (cp[0] != '\0' && cp[0] != '\n') {
		ri.callid[i].append(cp[0]);
		cp++;
	    }
	    sanitize(ri.callid[i]);
	    i++;
	}
    } else
	ri.sender = "<unknown>";
#ifdef TIFFTAG_FAXSUBADDRESS
    if (TIFFGetField(tif, TIFFTAG_FAXSUBADDRESS, &cp)) {
	ri.subaddr = cp;
	sanitize(ri.subaddr);
    } else
#endif
	ri.subaddr = "";
    fxStr date;
    if (TIFFGetField(tif, TIFFTAG_DATETIME, &cp)) {	// time received
	date = cp;
	sanitize(date);
    }
    ri.time = 0;
    ri.npages = 0;					// page count
    do {
	ri.npages++;
#ifdef TIFFTAG_FAXRECVTIME
	if (TIFFGetField(tif, TIFFTAG_FAXRECVTIME, &v))
	    ri.time += (u_int) v;
#endif
    } while (TIFFReadDirectory(tif));
    TIFFClose(tif);
    return (true);
}

bool
HylaFAXServer::isVisibleRecvQFile(const char* filename, const struct stat&)
{
    return (strncmp(filename, "fax", 3) == 0);
}

RecvInfo*
HylaFAXServer::getRecvInfo(const fxStr& qfile, const struct stat& sb)
{
    RecvInfo* rip = recvq[qfile];
    if (!rip) {
	rip = new RecvInfo(qfile);
	if (!getRecvDocStatus(*rip)) {
	    delete rip;
	    return (NULL);
	}
	// NB: this will be wrong if the file is copied
	rip->recvTime = sb.st_mtime;		// time recv completed
	recvq[qfile] = rip;
    } else if (rip->beingReceived && rip->recvTime < sb.st_mtime) {
	if (!getRecvDocStatus(*rip)) {
	    recvq.remove(qfile);
	    delete rip;
	    return (NULL);
	}
	// NB: this will be wrong if the file is copied
	rip->recvTime = sb.st_mtime;		// time recv completed
    }
    return (rip);
}

void
HylaFAXServer::listRecvQ(FILE* fd, const SpoolDir& sd, DIR* dir)
{
    /*
     * Use an absolute pathname when doing file
     * lookups to improve cache locality.
     */
    fxStr path(sd.pathname);
    struct dirent* dp;
    while ((dp = readdir(dir))) {
	struct stat sb;
	if (!isVisibleRecvQFile(dp->d_name, sb))
	    continue;
	fxStr qfile(path | dp->d_name);
	RecvInfo* rip;
	if (FileCache::update(qfile, sb) && (rip = getRecvInfo(qfile, sb))) {
	    Rprintf(fd, recvFormat, *rip, sb);
	    fputs("\r\n", fd);
	}
    }
}

void
HylaFAXServer::listRecvQFile(FILE* fd, const SpoolDir& dir,
    const char* filename, const struct stat& sb)
{
    RecvInfo* rip =
	getRecvInfo(fxStr::format("%s%s", dir.pathname, filename), sb);
    if (rip)
	Rprintf(fd, recvFormat, *rip, sb);
    else
	listUnixFile(fd, dir, filename, sb);
}

static const char rformat[] = {
    'A',		// A
    'B',		// B
    'C',		// C
    'D',		// D
    'E',		// E
    'F',		// F
    'G',		// G
    'H',		// H
    'I',		// I
    'J',		// J
    'K',		// K
    'L',		// L
    'M',		// M
    'N',		// N
    'O',		// O
    'P',		// P
    'Q',		// Q
    'R',		// R
    'S',		// S
    'T',		// T
    'U',		// U
    'V',		// V
    'W',		// W
#ifdef OLDPROTO_SUPPORT
    'u',		// X (beingReceived as 1 or 0)
    's',		// Y (recvTime in strftime %Y:%m:%d %H:%M:%S format)
    'u',		// Z (recvTime as decimal time_t)
#else
    'X',		// X
    'Y',		// Y
    'Z',		// Z
#endif
    '[',		// [
    '\\',		// \ (must have something after the backslash)
    ']',		// ]
    '^',		// ^
    '_',		// _
    '`',		// `
    's',		// a (subaddr)
    'u',		// b (bitrate)
    'c',		// c
    's',		// d (data format)
    's',		// e (error a.k.a. reason)
    's',		// f (filename)
    'g',		// g
    's',		// h (time spent receiving)
    's',		// i (CIDName)
    's',		// j (CIDNumber)
    'k',		// k
    'u',		// l (pagelength)
    's',		// m (protection mode)
    'u',		// n (file size)
    's',		// o (owner)
    'u',		// p (npages)
    'q',		// q (UNIX-style protection mode)
    'u',		// r (resolution)
    's',		// s (sender TSI)
    's',		// t (time received)
    'u',		// u
    'v',		// v
    'u',		// w (pagewidth)
    'x',		// x
    'y',		// y
    's'			// z (``*'' if being received)
};

#define	rounddown(x, y)	(((x)/(y))*(y))

/*
 * Return a compact notation for the specified
 * time.  This notation is guaranteed to fit in
 * a 7-character field.  We select one of 5
 * representations based on how close the time
 * is to ``now''.
 */
const char*
HylaFAXServer::compactRecvTime(time_t t)
{
    time_t now = Sys::now();
    if (t < now) {				// in the past
	static char buf[15];
	const struct tm* tm = cvtTime(t);
	if (t > rounddown(now, 24*60*60))	// today, use 19:37
	    strftime(buf, sizeof (buf), "%H:%M", tm);
	else if (t > now-7*24*60*60)		// within a week, use Sun 6pm
	    strftime(buf, sizeof (buf), "%a%I%p", tm);
	else					// over a week, use 25Dec95
	    strftime(buf, sizeof (buf), "%d%b%y", tm);
	return (buf);
    } else
	return ("");
}

/*
 * Print a formatted string with fields filled in from
 * the specified received facsimile state.  This
 * functionality is used to permit clients to get recv
 * queue state listings in preferred formats.
 */
void
HylaFAXServer::Rprintf(FILE* fd, const char* fmt,
    const RecvInfo& ri, const struct stat& sb)
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
	    if (!isalpha(c)) {
		if (c == '%')		// %% -> %
		    putc(c, fd);
		else
		    fprintf(fd, "%.*s%c", fp-fspec, fspec, c);
		continue;
	    }
	    fp[0] = rformat[c-'A'];	// printf format string
	    fp[1] = '\0';
	    switch (c) {
	    case 'a':
		fprintf(fd, fspec, (const char*) ri.subaddr);
		break;
	    case 'b':
		fprintf(fd, fspec, ri.params.bitRate());
		break;
	    case 'd':
		fprintf(fd, fspec, ri.params.dataFormatName());
		break;
	    case 'e':
		fprintf(fd, fspec, (const char*) ri.reason);
		break;
	    case 'f':
		fp = (char *) strrchr(ri.qfile, '/');
		fprintf(fd, fspec, fp ? fp+1 : (const char*) ri.qfile);
		break;
	    case 'h':
		fprintf(fd, fspec, fmtTime(ri.time));
		break;
	    case 'i':
		fprintf(fd, fspec, ri.callid.size() > CallID::NAME ? (const char*) ri.callid.id(CallID::NAME) : "");
		break;
	    case 'j':
		fprintf(fd, fspec, ri.callid.size() > CallID::NUMBER ? (const char*) ri.callid.id(CallID::NUMBER) : "");
		break;
	    case 'l':
		fprintf(fd, fspec, ri.params.pageLength());
		break;
	    case 'm':
	    case 'q':
		{ char prot[8];					// XXX HP C++
		  makeProt(sb, c == 'q', prot);
		  fprintf(fd, fspec, prot);
		}
		break;
	    case 'n':
		fprintf(fd, fspec, (u_int) sb.st_size);		// XXX
		break;
	    case 'o':
		fprintf(fd, fspec, userName((u_int) sb.st_gid));
		break;
	    case 'p':
		fprintf(fd, fspec, ri.npages);
		break;
	    case 'r':
		fprintf(fd, fspec, ri.params.verticalRes());
		break;
	    case 's':
		fprintf(fd, fspec, (const char*) ri.sender);
		break;
	    case 't':
		fprintf(fd, fspec, compactRecvTime(ri.recvTime));
		break;
	    case 'w':
		fprintf(fd, fspec, ri.params.pageWidth());
		break;
	    case 'z':
		fprintf(fd, fspec, ri.beingReceived ? "*" : " ");
		break;
#if OLDPROTO_SUPPORT
	    case 'X':
		fprintf(fd, fspec, ri.beingReceived);
		break;
	    case 'Y':
		{ char buf[30];					// XXX HP C++
		  strftime(buf, sizeof (buf), "%Y:%m:%d %H:%M:%S",
			IS(USEGMT) ? gmtime(&ri.recvTime) : localtime(&ri.recvTime));
		  fprintf(fd, fspec, buf);
		}
		break;
	    case 'Z':
		fprintf(fd, fspec, (u_int) ri.recvTime);
		break;
#endif /* OLDPROTO_SUPPORT */
	    }
	} else
	    putc(*cp, fd);
    }
}
