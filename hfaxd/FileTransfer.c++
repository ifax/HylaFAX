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
 * File transfer commands.
 */
#include "HylaFAXServer.h"
#include "Sys.h"
#include "config.h"
#include "zlib.h"
#include "tiffio.h"

#include <ctype.h>
#include <limits.h>

#ifndef CHAR_BIT
#ifdef NBBY
#define	CHAR_BIT	NBBY
#else
#define	CHAR_BIT	8
#endif
#endif /* CHAR_BIT */

#define	HAVE_PSLEVEL2	false
#define	HAVE_PCL5	false

static struct {
    const char*	name;		// protocol token name
    bool	supported;	// true if format is supported
    const char*	suffix;		// file suffix
    const char* help;		// help string for HELP FORM command
} formats[] = {
{ "TIFF", true,		 "tif", "Tagged Image File Format, Class F only" },
{ "PS",	  true,		 "ps",  "Adobe PostScript Level I" },
{ "PS2",  HAVE_PSLEVEL2, "ps",  "Adobe PostScript Level II" },
{ "PCL",  HAVE_PCL5,	 "pcl", "HP Printer Control Language (PCL), Version 5"},
};
static 	const char* typenames[] =  { "ASCII", "EBCDIC", "Image", "Local" };
static 	const char* strunames[] =  { "File", "Record", "Page", "TIFF" };
static 	const char* modenames[] =  { "Stream", "Block", "Compressed", "ZIP" };

#define	N(a)	(sizeof (a) / sizeof (a[0]))

/*
 * Record a file transfer in the log file.
 */
void
HylaFAXServer::logTransfer(const char* direction,
    const SpoolDir& sd, const char* pathname, time_t start)
{
    time_t now = Sys::now();
    time_t xferfaxtime = now - start;
    if (xferfaxtime == 0)
	xferfaxtime++;
    const char* filename = strrchr(pathname, '/');
    fxStr msg(fxStr::format("%.24s %lu %s %lu %s/%s %s %s\n"
	, ctime(&now)
	, (u_long) xferfaxtime
	, (const char*) remotehost
	, (u_long) byte_count
	, sd.pathname, filename ? filename+1 : pathname
	, direction
	, (const char*) the_user
    ));
    (void) Sys::write(xferfaxlog, msg, msg.length());
}

bool
HylaFAXServer::restartSend(FILE* fd, off_t marker)
{
    if (type == TYPE_A) {			// restart based on line count
	int c;
	while ((c = getc(fd)) != EOF)
	    if (c == '\n' && --marker == 0)
		return (true);
	return (false);
    } else					// restart based on file offset
	return (lseek(fileno(fd), marker, SEEK_SET) == marker);
}

/*
 * RETRieve a file.
 */
void
HylaFAXServer::retrieveCmd(const char* name)
{
    struct stat sb;
    SpoolDir* sd = fileAccess(name, R_OK, sb);
    if (sd) {
	FILE* fd = fopen(name, "r");
	if (fd != NULL) {
	    if (restart_point && !restartSend(fd, restart_point)) {
		perror_reply(550, name, errno);
	    } else {
		time_t start_time = Sys::now();
		int code;
		FILE* dout = openDataConn("w", code);
		if (dout != NULL) {
		    file_size = sb.st_size;
		    reply(code, "%s for %s (%lu bytes).",
			dataConnMsg(code), name, (u_long) file_size);
		    if (sendData(fd, dout))
			reply(226, "Transfer complete.");
		    if (TRACE(OUTXFERS) && xferfaxlog != -1)
			logTransfer("o", *sd, name, start_time);
		    closeDataConn(dout);
		}
	    }
	    fclose(fd);
	} else if (errno != 0)
	    perror_reply(550, name, errno);
	else
	    reply(550, "%s: Cannot open file.", name);
    }
}

/*
 * TIFF Directory Template used in returning
 * a single IFD/image from a TIFF file.
 */
typedef struct {
    TIFFDirEntry	SubFileType;
    TIFFDirEntry	ImageWidth;
    TIFFDirEntry	ImageLength;
    TIFFDirEntry	BitsPerSample;
    TIFFDirEntry	Compression;
    TIFFDirEntry	Photometric;
    TIFFDirEntry	FillOrder;
    TIFFDirEntry	StripOffsets;
    TIFFDirEntry	Orientation;
    TIFFDirEntry	SamplesPerPixel;
    TIFFDirEntry	RowsPerStrip;
    TIFFDirEntry	StripByteCounts;
    TIFFDirEntry	XResolution;
    TIFFDirEntry	YResolution;
    TIFFDirEntry	Options;		// T4 or T6
    TIFFDirEntry	ResolutionUnit;
    TIFFDirEntry	PageNumber;
    TIFFDirEntry	BadFaxLines;
    TIFFDirEntry	CleanFaxData;
    TIFFDirEntry	ConsecutiveBadFaxLines;
    uint32		link;			// offset to next directory
    uint32		xres[2];		// X resolution indirect value
    uint32		yres[2];		// Y resolution indirect value
} DirTemplate;

/*
 * RETrieve one Page from a file.  For now the
 * file must be a TIFF image; we might try to
 * handle PostScript at a later time (but don't
 * hold your breath as there's not much reason).
 */
void
HylaFAXServer::retrievePageCmd(const char* name)
{
    TIFF* tif = cachedTIFF;
    if (tif != NULL && streq(name, TIFFFileName(tif))) {
	/*
	 * Reuse the cached open file.  If no directory
	 * has been specified with a REST command then
	 * return the next consecutive directory in the file.
	 */
	if (restart_point == 0)			// advance to next directory
	    restart_point = TIFFCurrentDirectory(tif)+1;
    } else {
	if (tif)				// close cached handle
	    TIFFClose(tif), cachedTIFF = NULL;
	tif = openTIFF(name);
    }
    if (tif != NULL) {
	if (restart_point && !TIFFSetDirectory(tif, (tdir_t) restart_point)) {
	    reply(550, "%s: Unable to access directory %lu.",
		name, (u_long) restart_point);
	} else {
	    time_t start_time = Sys::now();
	    int code;
	    FILE* dout = openDataConn("w", code);
	    if (dout != NULL) {
		/*
		 * Calculate "file size" by totalling up the
		 * amount of image data and then adding in
		 * the expected data for the TIFF headers.
		 */
		uint32* sb;
		TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &sb);
		file_size = sizeof (DirTemplate) +
		    sizeof (TIFFHeader) + sizeof (uint16);
		for (tstrip_t s = 0, ns = TIFFNumberOfStrips(tif); s < ns; s++)
		    file_size += sb[s];
		reply(code, "%s for %s (%lu bytes).",
		    dataConnMsg(code), name, (u_long) file_size);
		if (sendTIFFData(tif, dout))
		    reply(226, "Transfer complete.");
		if (TRACE(OUTXFERS) && xferfaxlog != -1) {
		    struct stat sb;
		    SpoolDir* sd = fileAccess(name, R_OK, sb);
		    logTransfer("o", *sd, name, start_time);
		}
		closeDataConn(dout);
	    }
	}
	cachedTIFF = tif;
    }
}

/*
 * Open a file that is expected to hold a TIFF image.
 */
TIFF*
HylaFAXServer::openTIFF(const char* name)
{
    struct stat sb;
    SpoolDir* sd = fileAccess(name, R_OK, sb);
    if (sd) {
	int fd = Sys::open(name, O_RDONLY);
	if (fd >= 0) {
	    union {
		char buf[512];
		TIFFHeader h;
	    } b;
	    int cc = Sys::read(fd, (char*) &b, sizeof (b));
	    if (cc > sizeof (b.h) && b.h.tiff_version == TIFF_VERSION &&
	      (b.h.tiff_magic == TIFF_BIGENDIAN ||
	       b.h.tiff_magic == TIFF_LITTLEENDIAN)) {
		(void) lseek(fd, 0L, SEEK_SET);		// rewind
		TIFF* tif = TIFFFdOpen(fd, name, "r");
		if (tif != NULL)
		    return (tif);
		else
		    reply(550, "%s: Incomplete or invalid TIFF file.", name);
	    } else
		reply(550, "%s: Not a TIFF file.", name);
	    Sys::close(fd);
	} else if (errno != 0)
	    perror_reply(550, name, errno);
	else
	    reply(550, "%s: Cannot open file.", name);
    }
    return (NULL);
}

/*
 * Tranfer the current directory's contents of "tif" to "fdout".
 */
bool
HylaFAXServer::sendTIFFData(TIFF* tif, FILE* fdout)
{
    state |= S_TRANSFER;
    if (setjmp(urgcatch) != 0) {
	state &= ~S_TRANSFER;
	return (false);
    }
#define	PACK(a,b)	(((a)<<8)|(b))
    switch (PACK(type,mode)) {
    case PACK(TYPE_I,MODE_S):
    case PACK(TYPE_L,MODE_S):
	if (sendTIFFHeader(tif, fileno(fdout)) &&
	    sendITIFFData(tif, fileno(fdout))) {
	    state &= ~S_TRANSFER;
	    return (true);
	}
	break;
    default:
	reply(550, "TYPE %s, MODE %s not implemented."
	    , typenames[type]
	    , modenames[mode]
	);
	break;
    }
#undef PACK
    state &= ~S_TRANSFER;
    return (false);
}

static void
getLong(TIFF* tif, TIFFDirEntry& de)
{
    TIFFGetField(tif, de.tdir_tag, &de.tdir_offset);
}
static void
getShort(TIFF* tif, TIFFDirEntry& de)
{
    uint16 v;
    TIFFGetField(tif, de.tdir_tag, &v);
    de.tdir_offset = (uint32) v;
}

/*
 * Send a TIFF header and IFD for the current directory
 * in the open TIFF file.  The image data is expected to
 * immediately follow this information (i.e. the value of
 * the StripByteOffsets tag is setup to point to the offset
 * immediately after this data) and it is assumed that
 * all image data is concatenated into a single strip.
 */
bool
HylaFAXServer::sendTIFFHeader(TIFF* tif, int fdout)
{
    static DirTemplate templ = {
#define	TIFFdiroff(v) \
    (uint32) (sizeof (TIFFHeader) + sizeof (uint16) + \
      (unsigned) &(((DirTemplate*) 0)->v))
	{ TIFFTAG_SUBFILETYPE,		TIFF_LONG,	1 },
	{ TIFFTAG_IMAGEWIDTH,		TIFF_LONG,	1 },
	{ TIFFTAG_IMAGELENGTH,		TIFF_LONG, 	1 },
	{ TIFFTAG_BITSPERSAMPLE,	TIFF_SHORT,	1,  1 },
	{ TIFFTAG_COMPRESSION,		TIFF_SHORT,	1 },
	{ TIFFTAG_PHOTOMETRIC,		TIFF_SHORT,	1 },
	{ TIFFTAG_FILLORDER,		TIFF_SHORT,	1 },
	{ TIFFTAG_STRIPOFFSETS,		TIFF_LONG,	1, TIFFdiroff(yres[2]) },
	{ TIFFTAG_ORIENTATION,		TIFF_SHORT,	1 },
	{ TIFFTAG_SAMPLESPERPIXEL,	TIFF_SHORT,	1,  1 },
	{ TIFFTAG_ROWSPERSTRIP,		TIFF_LONG,	1, (uint32) -1 },
	{ TIFFTAG_STRIPBYTECOUNTS,	TIFF_LONG,	1 },
	{ TIFFTAG_XRESOLUTION,		TIFF_RATIONAL,	1, TIFFdiroff(xres[0]) },
	{ TIFFTAG_YRESOLUTION,		TIFF_RATIONAL,	1, TIFFdiroff(yres[0]) },
	{ TIFFTAG_GROUP3OPTIONS,	TIFF_LONG,	1 },
	{ TIFFTAG_RESOLUTIONUNIT,	TIFF_SHORT,	1 },
	{ TIFFTAG_PAGENUMBER,		TIFF_SHORT,	2 },
	{ TIFFTAG_BADFAXLINES,		TIFF_LONG,	1 },
	{ TIFFTAG_CLEANFAXDATA,		TIFF_SHORT,	1 },
	{ TIFFTAG_CONSECUTIVEBADFAXLINES,TIFF_LONG,	1 },
	0,					// next directory
	{ 0, 1 }, { 0, 1 },			// x+y resolutions
    };
#define	NTAGS	((TIFFdiroff(link)-TIFFdiroff(SubFileType)) / sizeof (TIFFDirEntry))
    /*
     * Construct the TIFF header for this IFD using
     * the preconstructed template above.  We extract
     * the necessary information from the open TIFF file.
     * In case it's not obvious, this code assumes a lot
     * of things about the contents of the TIFF file.
     */
    struct {
	TIFFHeader h;
	uint16	dircount;
	u_char	dirstuff[sizeof (templ)];
    } buf;
    union { int32 i; char c[4]; } u; u.i = 1;
    buf.h.tiff_magic = (u.c[0] == 0 ? TIFF_BIGENDIAN : TIFF_LITTLEENDIAN);
    buf.h.tiff_version = TIFF_VERSION;
    buf.h.tiff_diroff = sizeof (TIFFHeader);
    buf.dircount = (uint16) NTAGS;
    getLong(tif, templ.SubFileType);
    getLong(tif, templ.ImageWidth);
    getLong(tif, templ.ImageLength);
    getShort(tif, templ.Compression);
    getShort(tif, templ.Photometric);
    getShort(tif, templ.FillOrder);
    getShort(tif, templ.Orientation);
    templ.StripByteCounts.tdir_offset = (uint32) file_size - sizeof (buf);
    float res;
    TIFFGetField(tif, TIFFTAG_XRESOLUTION, &res);
	templ.xres[0] = (uint32) res;
    TIFFGetField(tif, TIFFTAG_YRESOLUTION, &res);
	templ.yres[0] = (uint32) res;
    if (templ.Compression.tdir_offset == COMPRESSION_CCITTFAX3)
	templ.Options.tdir_tag = TIFFTAG_GROUP3OPTIONS;
    else
	templ.Options.tdir_tag = TIFFTAG_GROUP4OPTIONS;
    getLong(tif, templ.Options);
    getShort(tif, templ.ResolutionUnit);
    TIFFGetField(tif, TIFFTAG_PAGENUMBER,  &templ.PageNumber.tdir_offset);
    getLong(tif, templ.BadFaxLines);
    getShort(tif, templ.CleanFaxData);
    getLong(tif, templ.ConsecutiveBadFaxLines);
    if (buf.h.tiff_magic == TIFF_BIGENDIAN) {
	TIFFDirEntry* dp = &templ.SubFileType;
	for (u_int i = 0; i < NTAGS; i++) {
	    if (dp->tdir_type == TIFF_SHORT)
		dp->tdir_offset <<= 16;
	    dp++;
	}
    }
    memcpy(buf.dirstuff, &templ, sizeof (templ));
    if (write(fdout, (const char*) &buf, sizeof (buf)) != sizeof (buf)) {
	perror_reply(426, "Data connection", errno);
	return (false);
    } else {
	byte_count += sizeof (buf);
	return (true);
    }
#undef NTAGS
#undef offsetof
}

/*
 * Send the raw image data for the current directory
 * in the open TIFF file.  If multiple strips are
 * present in the file they are concatenated w/o
 * consideration for any padding that might be present
 * or might be needed.
 */
bool
HylaFAXServer::sendITIFFData(TIFF* tif, int fdout)
{
    uint32* sb;
    (void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &sb);
    tdata_t buf = _TIFFmalloc(sb[0]);
    uint32 bsize = sb[0];
    for (tstrip_t s = 0, ns = TIFFNumberOfStrips(tif); s < ns; s++) {
	tsize_t cc = sb[s];
	if (cc > bsize) {
	    buf = _TIFFrealloc(buf, cc);
	    bsize = cc;
	}
	if (buf == NULL) {
	    reply(551, "Error allocating intermediate buffer");
	    return (false);
	}
	if (TIFFReadRawStrip(tif, s, buf, cc) != cc) {
	    reply(551, "Error reading input file at strip %u", s);
	    goto bad;
	}
	if (write(fdout, buf, (u_int) cc) != cc) {
	    perror_reply(426, "Data connection", errno);
	    goto bad;
	}
	byte_count += cc;
    }
    _TIFFfree(buf);
    return (true);
bad:
    _TIFFfree(buf);
    return (false);
}

const char*
HylaFAXServer::dataConnMsg(int code)
{
    return (code == 125 ?
	 "Using existing data connection" : "Opening new data connection");
}

void
HylaFAXServer::closeDataConn(FILE* fd)
{
    fclose(fd);
    data = -1;
    pdata = -1;
}

/*
 * STORe a file.
 */
void
HylaFAXServer::storeCmd(const char* name, const char* mode)
{
    struct stat sb;
    SpoolDir* sd = fileAccess(name, W_OK, sb);
    if (sd) {
	// check filename for magic characters
	for (const char* cp = name; *cp; cp++)
	    if (isspace(*cp) || !isgraph(*cp)) {
		reply(553, "Bad filename; includes invalid character.");
		return;
	    }
	mode_t omask = umask(027);
	FILE* fout = fopen(name, restart_point ? "r+w" : mode);
	if (fout != NULL) {
	    setFileOwner(name);
	    if (restart_point &&
	      lseek(fileno(fout), restart_point, SEEK_SET) != restart_point)
		perror_reply(550, name, errno);
	    else {
		time_t start_time = Sys::now();
		int code;
		FILE* din = openDataConn("r", code);
		if (din != NULL) {
		    reply(code, "%s for %s.", dataConnMsg(code), name);
		    file_size = -1;
		    if (recvData(din, fout))
			reply(226, "Transfer complete.");
		    if (TRACE(INXFERS) && xferfaxlog != -1)
			logTransfer("i", *sd, name, start_time);
		    closeDataConn(din);
		}
	    }
	    fclose(fout);
	} else
	    perror_reply(553, name, errno);
	(void) umask(omask);
    }
}

/*
 * STOU (STOre Unique) file.
 * STOT (STOre unique Temporary) file.
 *
 * STOT differs from STOU in that files created with STOT
 * are automatically unlinked when the process terminates
 * while files created with STOU are not.  STOT is intended
 * for clients creating documents that are to be sent and
 * then expunged.  STOU is for documents that are intended
 * be shared across multiple sessions.
 */
void
HylaFAXServer::storeUniqueCmd(bool isTemp)
{
    fxStr emsg;
    u_int seqnum = getDocumentNumbers(1, emsg);
    if (seqnum != (u_int) -1) {
	fxStr filename = fxStr::format("/%s/doc%u.%s"
	    , isTemp ? FAX_TMPDIR : FAX_DOCDIR
	    , seqnum
	    , formats[form].suffix
	);
	FILE* fout = fopen(filename, "w");
	if (fout != NULL) {
	    setFileOwner(filename);
	    FileCache::chmod(filename, 0640);		// sync cache
	    if (isTemp)
		tempFiles.append(filename);
	    time_t start_time = Sys::now();
	    int code;
	    FILE* din = openDataConn("r", code);
	    if (din != NULL) {
		reply(code, "FILE: %s (%s).", (const char*) filename,
		    dataConnMsg(code));
		file_size = -1;
		if (recvData(din, fout))
		    reply(226, "Transfer complete (FILE: %s).",
			(const char*) filename);
		if (TRACE(INXFERS) && xferfaxlog != -1)
		    logTransfer("i"
			, *dirLookup(isTemp ? "/" FAX_TMPDIR : "/" FAX_DOCDIR)
			, filename
			, start_time
		    );
		closeDataConn(din);
	    }
	    fclose(fout);
	} else
	    perror_reply(553, filename, errno);
    } else
	reply(553, emsg);
}

/*
 * Tranfer the contents of "fdin" to "fdout".
 */
bool
HylaFAXServer::sendData(FILE* fdin, FILE* fdout)
{
    state |= S_TRANSFER;
    if (setjmp(urgcatch) != 0) {
	state &= ~S_TRANSFER;
	return (false);
    }
#define	PACK(a,b)	(((a)<<8)|(b))
    switch (PACK(type,mode)) {
    case PACK(TYPE_I,MODE_S):
    case PACK(TYPE_L,MODE_S):
	if (sendIData(fileno(fdin), fileno(fdout))) {
	    state &= ~S_TRANSFER;
	    return (true);
	}
	break;
    case PACK(TYPE_I,MODE_Z):
    case PACK(TYPE_L,MODE_Z):
	if (sendZData(fileno(fdin), fileno(fdout))) {
	    state &= ~S_TRANSFER;
	    return (true);
	}
	break;
    case PACK(TYPE_A,MODE_S):
	for (;;) {
	    int c = getc(fdin);
	    if (c == EOF) {
		fflush(fdout);
		if (ferror(fdout)) {
		    perror_reply(426, "Data Connection", errno);
		    break;
		}
		if (ferror(fdin)) {
		    perror_reply(551, "Error on input file", errno);
		    break;
		}
		state &= ~S_TRANSFER;
		return (true);
	    }
	    byte_count++;
	    if (c == '\n') {		// \n -> \r\n
		if (ferror(fdout)) {	// check at the end of each line
		    perror_reply(426, "Data connection", errno);
		    break;
		}
		putc('\r', fdout);
	    }
	    putc(c, fdout);
	}
	break;
    default:
	reply(550, "TYPE %s, MODE %s not implemented."
	    , typenames[type]
	    , modenames[mode]
	);
	break;
    }
#undef PACK
    state &= ~S_TRANSFER;
    return (false);
}

bool
HylaFAXServer::sendIData(int fdin, int fdout)
{
    char buf[16*1024];
    for (;;) {
	int cc = read(fdin, buf, sizeof (buf));
	if (cc == 0)
	    return (true);
	if (cc < 0) {
	    perror_reply(551, "Error reading input file", errno);
	    break;
	}
	if (write(fdout, buf, cc) != cc) {
	    perror_reply(426, "Data connection", errno);
	    break;
	}
	byte_count += cc;
    }
    return (false);
}

bool
HylaFAXServer::sendZData(int fdin, int fdout)
{
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;
    zstream.data_type = Z_BINARY;
    if (deflateInit(&zstream, Z_DEFAULT_COMPRESSION) == Z_OK) {
	char obuf[16*1024];
	zstream.next_out = (Bytef*) obuf;
	zstream.avail_out = sizeof (obuf);

	int cc;
	for (;;) {
	    char buf[16*1024];
	    cc = read(fdin, buf, sizeof (buf));
	    if (cc == 0)
		break;
	    if (cc < 0) {
		perror_reply(551, "Error reading input file", errno);
		goto bad;
	    }
	    zstream.next_in = (Bytef*) buf;
	    zstream.avail_in = cc;
	    do {
		if (deflate(&zstream, Z_NO_FLUSH) != Z_OK) {
		    reply(452, "Compressor error: %s", zstream.msg);
		    goto bad;
		}
		if (zstream.avail_out == 0) {
		    if (write(fdout, obuf, sizeof (obuf)) != sizeof (obuf)) {
			perror_reply(426, "Data connection", errno);
			goto bad;
		    }
		    byte_count += sizeof (obuf);
		    zstream.next_out = (Bytef*) obuf;
		    zstream.avail_out = sizeof (obuf);
		}
	    } while (zstream.avail_in > 0);
	}
	int state;
	do {
	    switch (state = deflate(&zstream, Z_FINISH)) {
	    case Z_STREAM_END:
	    case Z_OK:
		if (zstream.avail_out != sizeof (obuf)) {
		    cc = sizeof (obuf) - zstream.avail_out;
		    if (write(fdout, obuf, cc) != cc) {
			perror_reply(426, "Data connection", errno);
			goto bad;
		    }
		    byte_count += cc;
		    zstream.next_out = (Bytef*) obuf;
		    zstream.avail_out = sizeof (obuf);
		}
		break;
	    default:
		reply(426, "Compressor error: %s", zstream.msg);
		goto bad;
	    }
	} while (state != Z_STREAM_END);
	deflateEnd(&zstream);
	return (true);
bad:
	deflateEnd(&zstream);
    } else
	reply(452, "Can not initialize compression library: %s", zstream.msg);
    return (false);
}

/*
 * Transfer data from peer to file.
 */
bool
HylaFAXServer::recvData(FILE* fdin, FILE* fdout)
{
    state |= S_TRANSFER;
    if (setjmp(urgcatch) != 0) {
	state &= ~S_TRANSFER;
	return (false);
    }
#define	PACK(a,b)	(((a)<<8)|(b))
    switch (PACK(type,mode)) {
    case PACK(TYPE_I,MODE_S):
    case PACK(TYPE_L,MODE_S):
	if (recvIData(fileno(fdin), fileno(fdout))) {
	    state &= ~S_TRANSFER;
	    return (true);
	}
	break;
    case PACK(TYPE_I,MODE_Z):
    case PACK(TYPE_L,MODE_Z):
	if (recvZData(fileno(fdin), fileno(fdout))) {
	    state &= ~S_TRANSFER;
	    return (true);
	}
	break;
    case PACK(TYPE_A,MODE_S):
	for (;;) {
	    int c = getc(fdin);
	    if (c == EOF) {
		fflush(fdout);
		if (ferror(fdin)) {
		    perror_reply(426, "Data Connection", errno);
		    break;
		}
		if (ferror(fdout)) {
		    perror_reply(452, "Error writing output file", errno);
		    break;
		}
		state &= ~S_TRANSFER;
		return (true);
	    }
	    byte_count++;
	    if (c == '\r') {		// \r\n -> \n
		if (ferror(fdout)) {	// check at the end of each line
		    perror_reply(452, "Error writing output file", errno);
		    break;
		}
		c = getc(fdin);
		if (c != '\n') {
		    putc('\r', fdout);
		    if (c == EOF)
			continue;
		}
		byte_count++;
	    }
	    putc(c, fdout);
	}
	break;
    default:
	reply(550, "TYPE %s, MODE %s not implemented."
	    , typenames[type]
	    , modenames[mode]
	);
	break;
    }
#undef PACK
    state &= ~S_TRANSFER;
    return (false);
}

bool
HylaFAXServer::recvIData(int fdin, int fdout)
{
    char buf[16*1024];			// XXX better if page-aligned
    for (;;) {
	int cc = read(fdin, buf, sizeof (buf));
	if (cc == 0)
	    return (true);
	if (cc < 0) {
	    perror_reply(426, "Data Connection", errno);
	    break;
	}
	if (write(fdout, buf, cc) != cc) {
	    perror_reply(452, "Error writing output file", errno);
	    break;
	}
	byte_count += cc;
    }
    return (false);
}

bool
HylaFAXServer::recvZData(int fdin, int fdout)
{
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;
    zstream.data_type = Z_BINARY;
    if (inflateInit(&zstream) == Z_OK) {
	char obuf[16*1024];
	zstream.next_out = (Bytef*) obuf;
	zstream.avail_out = sizeof (obuf);
	for (;;) {
	    char buf[16*1024];
	    int cc = read(fdin, buf, sizeof (buf));
	    if (cc == 0) {
		size_t occ = sizeof (obuf) - zstream.avail_out;
		if (occ > 0 && write(fdout, obuf, occ) != occ) {
		    perror_reply(452, "Error writing output file", errno);
		    break;
		}
		(void) inflateEnd(&zstream);
		return (true);
	    }
	    if (cc < 0) {
		perror_reply(426, "Data Connection", errno);
		break;
	    }
	    byte_count += cc;
	    zstream.next_in = (Bytef*) buf;
	    zstream.avail_in = cc;
	    do {
		int state = inflate(&zstream, Z_PARTIAL_FLUSH);
		if (state == Z_STREAM_END)
		    break;
		if (state != Z_OK) {
		    reply(452, "Decoding error: %s", zstream.msg);
		    goto bad;
		}
		size_t occ = sizeof (obuf) - zstream.avail_out;
		if (write(fdout, obuf, occ) != occ) {
		    perror_reply(452, "Error writing output file", errno);
		    goto bad;
		}
		zstream.next_out = (Bytef*) obuf;
		zstream.avail_out = sizeof (obuf);
	    } while (zstream.avail_in > 0);
	}
bad:
	(void) inflateEnd(&zstream);
    } else
	reply(452, "Can not initialize decoder: %s", zstream.msg);
    return (false);
}

void
HylaFAXServer::formHelpCmd(void)
{
    lreply(211, "Supported file formats:");
    for (u_int i = 0, n = N(formats); i < n; i++)
	lreply(211, "%8s%s  %s"
	    , formats[i].name
	    , formats[i].supported ? " " : "*"
	    , formats[i].help
	);
    reply(211, "Formats marked with a * are not supported.");
}

void
HylaFAXServer::formCmd(const char* name)
{
    fxStr f(name);
    f.raisecase();
    for (u_int i = 0, n = N(formats); i < n; i++)
	if (f == formats[i].name) {
	    if (formats[i].supported) {
		form = i;
		reply(200, "Format set to %s.", (const char*) f);
	    } else
		reply(504, "Format %s not supported.", (const char*) f);
	    return;
	}
    reply(504, "Unknown format %s.", name);
}
#undef N

void
HylaFAXServer::typeCmd(const char* name)
{
    if (strcasecmp(name, "I") == 0)
	type = TYPE_I;
    else if (strcasecmp(name, "A") == 0)
	type = TYPE_A;
    else if (strcasecmp(name, "L") == 0)
	type = TYPE_L;
    else {
	reply(504, "Type %s not supported.", name);
	return;
    }
    reply(200, "Type set to %s.", typenames[type]);
}

void
HylaFAXServer::modeCmd(const char* name)
{
    if (strcasecmp(name, "S") == 0)
	mode = MODE_S;
    else if (strcasecmp(name, "Z") == 0)
	mode = MODE_Z;
    else {
	reply(504, "Mode %s not supported.", name);
	return;
    }
    reply(200, "Mode set to %s.", modenames[mode]);
}

void
HylaFAXServer::struCmd(const char* name)
{
    if (strcasecmp(name, "F") == 0)
	stru = STRU_F;
    else if (strcasecmp(name, "T") == 0)
	stru = STRU_T;
    else {
	reply(504, "Structure %s not supported.", name);
	return;
    }
    reply(200, "Structure set to %s.", strunames[stru]);
}

void
HylaFAXServer::printTransferStatus(FILE* fd)
{
    if (restart_point)
	fprintf(fd, "    Data transfer restart pending at %lu\r\n", 
	    (u_long) restart_point);
    fprintf(fd, "    TYPE: %s", typenames[type]);
    if (type == TYPE_L)
        fprintf(fd, " %d", CHAR_BIT);
    fprintf(fd, "; STRU: %s; MODE: %s; FORM: %s\r\n"
	, strunames[stru]
	, modenames[mode]
	, formats[form].name
    );
}
