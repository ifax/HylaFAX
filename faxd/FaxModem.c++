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
#include <ctype.h>
#include <stdlib.h>

#include "ClassModem.h"
#include "FaxServer.h"
#include "FaxTrace.h"
#include "FaxFont.h"
#include "t.30.h"
#include "Sys.h"

FaxModem::FaxModem(FaxServer& s, const ModemConfig& c)
    : ClassModem(s,c)
    , server(s)
{
    tagLineFont = NULL;
    minsp = BR_2400;
    curreq = NULL;
    group3opts = 0;
    // fill order settings may be overwritten in derived class
    recvFillOrder = (conf.recvFillOrder != 0)? conf.recvFillOrder : FILLORDER_LSB2MSB;
    sendFillOrder = (conf.sendFillOrder != 0)? conf.sendFillOrder : FILLORDER_LSB2MSB;
    rtcRev        = TIFFGetBitRevTable(sendFillOrder == FILLORDER_LSB2MSB);
}

FaxModem::~FaxModem()
{
    delete tagLineFont;
}

/*
 * Default methods for modem driver interface.
 */

u_int FaxModem::getTagLineSlop() const		{ return tagLineSlop; }

/*
 * Do setup work prior to placing the call.
 */
bool
FaxModem::sendSetup(FaxRequest& req, const Class2Params&, fxStr&)
{
    minsp = fxmax((u_int) req.minsp, conf.minSpeed);
    pageNumber = 1;
    pageNumberOfJob = req.npages + 1;
    if (req.desiredtl == 0)
	setupTagLine(req, conf.tagLineFmt);
    else
	setupTagLine(req, req.tagline);
    curreq = &req;
    return (true);
}
/*
 * Do work at the beginning of a send operation;
 * after a call has been established.
 */
void
FaxModem::sendBegin()
{
    if (conf.sendBeginCmd != "")
	atCmd(conf.sendBeginCmd);
    optFrames = 0;
}
void FaxModem::sendSetupPhaseB(const fxStr&, const fxStr&){}
void FaxModem::sendEnd()	{}

bool
FaxModem::recvBegin(fxStr&)
{
    optFrames = 0;
    return (true);
}

bool
FaxModem::pollBegin(const fxStr&, const fxStr&, const fxStr&, fxStr&)
{
    optFrames = 0;
    return (true);
}

static void
stripBlanks(fxStr& d, const fxStr& s)
{
    d = s;
    d.remove(0, d.skip(0,' '));		// strip leading white space
    u_int pos = d.skipR(d.length(),' ');
    d.remove(pos, d.length() - pos);	// and trailing white space
}

void
FaxModem::recvTSI(const fxStr& s)
{
    stripBlanks(tsi, s);
    protoTrace("REMOTE TSI \"%s\"", (const char*) tsi);
    optFrames |= 0x1;
}
void
FaxModem::recvCSI(const fxStr& s)
{
    stripBlanks(tsi, s);
    protoTrace("REMOTE CSI \"%s\"", (const char*) tsi);
    optFrames |= 0x1;
}
bool
FaxModem::getRecvTSI(fxStr& s)
{
    if (optFrames & 0x1) {
	s = tsi;
	return (true);
    } else
	return (false);
}
bool FaxModem::getSendCSI(fxStr& s)	{ return getRecvTSI(s); }

void
FaxModem::recvPWD(const fxStr& s)
{
    stripBlanks(pwd, s);
    protoTrace("REMOTE PWD \"%s\"", (const char*) pwd);
    optFrames |= 0x2;
}
bool
FaxModem::getRecvPWD(fxStr& s)
{
    if (optFrames & 0x2) {
	s = pwd;
	return (true);
    } else
	return (false);
}
void
FaxModem::recvSUB(const fxStr& s)
{
    stripBlanks(sub, s);
    protoTrace("REMOTE SUB \"%s\"", (const char*) sub);
    optFrames |= 0x4;
}
bool
FaxModem::getRecvSUB(fxStr& s)
{
    if (optFrames & 0x4) {
	s = sub;
	return (true);
    } else
	return (false);
}

void
FaxModem::recvNSF( const NSF& aNsf )
{
    nsf = aNsf;
    optFrames |= 0x8;
    protoTrace("REMOTE NSF \"%s\"", (const char*) nsf.getHexNsf() );
    protoTrace("NSF remote fax equipment: %s %s", 
               (const char*)nsf.getVendor(),
               (const char*)nsf.getModel());
    if( nsf.stationIdFound() )
        protoTrace("NSF %sremote station ID: \"%s\"",
                   nsf.vendorFound()? "": "possible ",
                   nsf.getStationId());
}

bool
FaxModem::getSendNSF(NSF& aNsf)
{
    if (optFrames & 0x8) {
        aNsf = nsf;
        return (true);
    } else {
        return (false);
    }
}

void
FaxModem::recvDCS(const Class2Params& params)
{
    protoTrace("REMOTE wants %s", params.bitRateName());
    protoTrace("REMOTE wants %s", params.pageWidthName());
    protoTrace("REMOTE wants %s", params.pageLengthName());
    protoTrace("REMOTE wants %s", params.verticalResName());
    protoTrace("REMOTE wants %s", params.dataFormatName());
    if (params.ec != EC_DISABLE)
	protoTrace("REMOTE wants %s", params.ecmName());
}
const Class2Params& FaxModem::getRecvParams() const	{ return params; }

/*
 * Decode the post-page-handling string to get page
 * chopping information for the current page.  The
 * page chop information is optional and always
 * precedes the page-handling information, so at least
 * 2+5+1 characters must be present.  The format of the
 * information is:
 *
 *   xxZcccxM
 *
 * xx is the hex-encoded session parameters (see below),
 * M is a post-page message, and cccc is an optional 4-digit
 * hex encoding of the number of bytes to chop from the
 * encoded page data.  Note also that we only return the
 * chop count if the negotiated session parameters permit
 * us to send a variable-length page.
 */ 
u_int
FaxModem::decodePageChop(const fxStr& pph, const Class2Params& params)
{
    if (params.ln == LN_INF && pph.length() >= 2+5+1 && pph[2] == 'Z') {
	char buf[5];
	buf[0] = pph[2+1];
	buf[1] = pph[2+2];
	buf[2] = pph[2+3];
	buf[3] = pph[2+4];
	buf[4] = '\0';
	return ((u_int) strtoul(buf, NULL, 16));
    } else
	return (0);
}

/*
 * Decode the post-page-handling string to get the next
 * post-page message.  The string is assumed to have 3
 * characters per page of the form:
 *
 *   xxM
 *
 * where xx is a 2-digit hex encoding of the session
 * parameters required to send the page data and the M
 * the post-page message to use between this page and
 * the next page.  See FaxServer::sendPrepareFax for the
 * construction of this string.
 */ 
bool
FaxModem::decodePPM(const fxStr& pph, u_int& ppm, fxStr& emsg)
{
    const char* what;
    if (pph.length() >= 3 && (pph[2] != 'Z' || pph.length() >= 2+5+1)) {
	switch (pph[pph[2] == 'Z' ? 2+5 : 2+0]) {
	case 'P': ppm = PPM_EOP; return (true);
	case 'M': ppm = PPM_EOM; return (true);
	case 'S': ppm = PPM_MPS; return (true);
	}
	what = "unknown";
    } else
	what = "bad";
    emsg = fxStr::format( "Internal botch; %s post-page handling string \"%s\"",
	what, (const char*) pph);
    return (false);
}

/*
 * Modem capability (and related) query interfaces.
 */

u_int
FaxModem::getCapabilities() const
{
    return modemParams.encodeCaps();
}

static u_int
bestBit(u_int bits, u_int top, u_int bot)
{
    while (top > bot && (bits & BIT(top)) == 0)
	top--;
    return (top);
}

/*
 * Return Class 2 code for best modem signalling rate.
 */
u_int
FaxModem::getBestSignallingRate() const
{
    return bestBit(modemParams.br, BR_14400, BR_2400);
}

/*
 * Compare the requested signalling rate against
 * those the modem can do and return the appropriate
 * Class 2 bit rate code.
 */
int
FaxModem::selectSignallingRate(int br) const
{
    for (; br >= 0 && (modemParams.br & BIT(br)) == 0; br--)
	;
    return (br);
}

/*
 * Compare the requested min scanline time
 * to what the modem can do and return the
 * lowest time the modem can do.
 */
int
FaxModem::selectScanlineTime(int st) const
{
    for (; st < ST_40MS && (modemParams.st & BIT(st)) == 0; st++)
	;
    return (st);
}

/*
 * Return the best min scanline time the modem
 * is capable of supporting.
 */
u_int
FaxModem::getBestScanlineTime() const
{
    u_int st;
    for (st = ST_0MS; st < ST_40MS; st++)
	if (modemParams.st & BIT(st))
	    break;
    return st;
}

/*
 * Return the best vres the modem supports.
 */
u_int
FaxModem::getBestVRes() const
{
    return bestBit(modemParams.vr, VR_FINE, VR_NORMAL);
}

/*
 * Return the best page width the modem supports.
 */
u_int
FaxModem::getBestPageWidth() const
{
    // XXX NB: we don't use anything > WD_2432
    return bestBit(modemParams.wd, WD_2432, WD_1728);
}

/*
 * Return the best page length the modem supports.
 */
u_int
FaxModem::getBestPageLength() const
{
    return bestBit(modemParams.ln, LN_INF, LN_A4);
}

/*
 * Return the best data format the modem supports.
 */
u_int
FaxModem::getBestDataFormat() const
{
    return bestBit(modemParams.df, DF_2DMMR, DF_1DMR);
}

/*
 * Return the best ECM functionality the modem supports.
 */
u_int
FaxModem::getBestECM() const
{
    return bestBit(modemParams.ec, EC_DISABLE, EC_ECLFULL);
}

/*
 * Return whether or not the modem supports 2DMR.
 */
bool
FaxModem::supports2D() const
{
    return (modemParams.df & BIT(DF_2DMR)) != 0;
}

/*
 * Return whether or not received EOLs are byte aligned.
 */
bool
FaxModem::supportsEOLPadding() const
{
    return false;
}

/*
 * Return whether or not the modem is capable of polling.
 */
bool
FaxModem::supportsPolling() const
{
    return false;
}

/*
 * Return whether or not the modem supports
 * the optional Error Correction Mode (ECM).
 */
bool
FaxModem::supportsECM() const
{
    return (modemParams.ec &~ BIT(EC_DISABLE)) != 0;
}

/*
 * Return whether or not the modem supports the
 * specified vertical resolution.  Note that we're
 * rather tolerant because of potential precision
 * problems and general sloppiness on the part of
 * applications writing TIFF files.
 */
bool
FaxModem::supportsVRes(float res) const
{
    if (3.0 <= res && res < 4.75)
	return (modemParams.vr & BIT(VR_NORMAL)) != 0;
    else if (5.9 <= res && res < 9.8)
	return (modemParams.vr & BIT(VR_FINE)) != 0;
    else
	return false;
}

/*
 * Return whether or not the modem supports the
 * specified page width.
 */
bool
FaxModem::supportsPageWidth(u_int w) const
{
    switch (w) {
    case 1728:	return (modemParams.wd & BIT(WD_1728)) != 0;
    case 2048:	return (modemParams.wd & BIT(WD_2048)) != 0;
    case 2432:	return (modemParams.wd & BIT(WD_2432)) != 0;
    case 1216:	return (modemParams.wd & BIT(WD_1216)) != 0;
    case 864:	return (modemParams.wd & BIT(WD_864)) != 0;
    }
    return false;
}

/*
 * Return whether or not the modem supports the
 * specified page length.  As above for vertical
 * resolution we're lenient in what we accept.
 */
bool
FaxModem::supportsPageLength(u_int l) const
{
    // XXX probably need to be more forgiving with values
    if (270 < l && l <= 330)
	return (modemParams.ln & (BIT(LN_A4)|BIT(LN_INF))) != 0;
    else if (330 < l && l <= 390)
	return (modemParams.ln & (BIT(LN_B4)|BIT(LN_INF))) != 0;
    else
	return (modemParams.ln & BIT(LN_INF)) != 0;
}

/*
 * Return modems best capabilities for setting up
 * the initial T.30 DIS when receiving data.
 */
u_int
FaxModem::modemDIS() const
{
    return DIS_T4RCVR
	  | Class2Params::vrDISTab[getBestVRes()]
	  | Class2Params::brDISTab[getBestSignallingRate()]
	  | Class2Params::wdDISTab[getBestPageWidth()]
	  | Class2Params::lnDISTab[getBestPageWidth()]
	  | Class2Params::dfDISTab[getBestDataFormat()]
	  | Class2Params::stDISTab[getBestScanlineTime()]
	  ;
}

/*
 * Return the 32-bit extended capabilities for the
 * modem for setting up the initial T.30 DIS when
 * receiving data.
 */
u_int
FaxModem::modemXINFO() const
{
    return
	  ((modemParams.df & BIT(DF_2DMRUNCOMP)) ? DIS_2DUNCOMP : 0)
	| ((modemParams.df & BIT(DF_2DMMR)) ? DIS_G4COMP : 0)
	;
}

/*
 * Tracing support.
 */

/*
 * Trace a modem's capabilities.
 */
void
FaxModem::traceModemParams()
{
    traceBits(modemParams.vr, Class2Params::verticalResNames);
    traceBits(modemParams.br, Class2Params::bitRateNames);
    traceBits(modemParams.wd, Class2Params::pageWidthNames);
    traceBits(modemParams.ln, Class2Params::pageLengthNames);
    traceBits(modemParams.df, Class2Params::dataFormatNames);
    if (supportsECM())
	traceBits(modemParams.ec, Class2Params::ecmNames);
    if (modemParams.bf & BIT(BF_ENABLE))
	modemSupports("binary file transfer");
    traceBits(modemParams.st, Class2Params::scanlineTimeNames);
}

void
FaxModem::tracePPM(const char* dir, u_int ppm)
{
    static const char* ppmNames[16] = {
	"unknown PPM 0x00",
	"EOM (more documents)",				// FCF_EOM
	"MPS (more pages, same document)",		// FCF_MPS
	"unknown PPM 0x03",
	"EOP (no more pages or documents)",		// FCF_EOP
	"unknown PPM 0x05",
	"unknown PPM 0x06",
	"unknown PPM 0x07",
	"unknown PPM 0x08",
	"PRI-EOM (more documents after interrupt)",	// FCF_PRI_EOM
	"PRI-MPS (more pages after interrupt)",		// FCF_PRI_MPS
	"unknown PPM 0x0B",
	"PRI-EOP (no more pages after interrupt)",	// FCF_PRI_EOP
	"unknown PPM 0x0D",
	"unknown PPM 0x0E",
    };
    protoTrace("%s %s", dir, ppmNames[ppm&0xf]);
}

void
FaxModem::tracePPR(const char* dir, u_int ppr)
{
    static const char* pprNames[16] = {
	"unknown PPR 0x00",
	"MCF (message confirmation)",			// FCF_MCF/PPR_MCF
	"RTN (retrain negative)",			// FCF_RTN/PPR_RTN
	"RTP (retrain positive)",			// FCF_RTP/PPR_RTP
	"PIN (procedural interrupt negative)",		// FCF_PIN/PPR_PIN
	"PIP (procedural interrupt positive)",		// FCF_PIP/PPR_PIP
	"unknown PPR 0x06",
	"unknown PPR 0x07",
	"CRP (command repeat)",				// FCF_CRP
	"unknown PPR 0x09",
	"unknown PPR 0x0A",
	"unknown PPR 0x0B",
	"unknown PPR 0x0C",
	"unknown PPR 0x0D",
	"unknown PPR 0x0E",
	"DCN (disconnect)",				// FCF_DCN
    };
    protoTrace("%s %s", dir, pprNames[ppr&0xf]);
}

/*
 * Modem i/o support.
 */

/*
 * Miscellaneous server interfaces hooks.
 */
bool FaxModem::isFaxModem() const		{ return true; }

bool FaxModem::getHDLCTracing()
    { return (server.getSessionTracing() & FAXTRACE_HDLC) != 0; }

FaxSendStatus
FaxModem::sendSetupParams(TIFF* tif, Class2Params& params,
    FaxMachineInfo& info, fxStr& emsg)
{
    return server.sendSetupParams(tif, params, info, emsg);
}


/*
 * Record the file offset to the start of the data
 * in the file.  We write zero bytes to force the
 * strip offset to be setup in case this is the first
 * time the strip is being written.
 */
void
FaxModem::recvStartPage(TIFF* tif)
{
    u_char null[1];
    (void) TIFFWriteRawStrip(tif, 0, null, 0);
    u_long* lp;
    (void) TIFFGetField(tif, TIFFTAG_STRIPOFFSETS, &lp);
    savedWriteOff = lp[0];
}

/*
 * Reset the TIFF state for the current page so that
 * subsequent data overwrites anything previously
 * written.  This is done by reseting the file offset
 * and setting the strip's bytecount and offset to
 * values they had at the start of the page.  This
 * scheme assumes that only page data is written to
 * the file between the time recvSetupPage is called
 * and recvResetPage is called.
 */
void
FaxModem::recvResetPage(TIFF* tif)
{
    u_long* lp;
    TIFFSetWriteOffset(tif, 0);		// force library to reset state
    TIFFGetField(tif, TIFFTAG_STRIPOFFSETS, &lp);	lp[0] = savedWriteOff;
    TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &lp);	lp[0] = 0;
}

void
FaxModem::countPage()
{
    pageNumber++;
    pageNumberOfJob++;
}

void
FaxModem::notifyPageSent(TIFF* tif)
{
    if (curreq)
	server.notifyPageSent(*curreq, TIFFFileName(tif));
}

/*
 * Phase C data correction
 */

#include "G3Decoder.h"
#include "G3Encoder.h"
#include "StackBuffer.h"
#include "Class2Params.h"

class MemoryDecoder : public G3Decoder {
private:
    u_char*     bp;
    u_int       width;
    u_int       byteWidth;
    u_long      cc;
    
    u_int fillorder;
    bool is2D;
    
    u_char*     endOfData;      // used by cutExtraRTC

    tiff_runlen_t*
                runs;
    u_char*     rowBuf;

    int         decodeNextByte();
public:
    MemoryDecoder(u_char* data, u_int wid, u_long n,
                  u_int fillorder, bool twoDim);
    ~MemoryDecoder();
    u_char* current() { return bp; }
    void fixFirstEOL();
    u_char* cutExtraRTC();
};

MemoryDecoder::MemoryDecoder(u_char* data, u_int wid, u_long n,
                             u_int order, bool twoDim)
{
    bp         = data;
    width      = wid;
    byteWidth  = howmany(width, 8);
    cc         = n;
    
    fillorder  = order;
    is2D       = twoDim;

    runs      = new tiff_runlen_t[2*width];      // run arrays for cur+ref rows
    rowBuf    = new u_char[byteWidth];
    setupDecoder(fillorder, is2D);
    setRuns(runs, runs+width, width);
}
MemoryDecoder::~MemoryDecoder()
{
    delete rowBuf;
    delete runs;
}

int
MemoryDecoder::decodeNextByte()
{
    if (cc == 0)
        raiseRTC();                     // XXX don't need to recognize EOF
    cc--;
    return (*bp++);
}

#ifdef roundup
#undef roundup
#endif
#define roundup(a,b)    ((((a)+((b)-1))/(b))*(b))

/*
 * TIFF Class F specs say:
 *
 * "As illustrated in FIGURE 1/T.4 in Recommendation T.4 (the Red
 * Book, page 20), facsimile documents begin with an EOL (which in
 * Class F is byte-aligned)..."
 *
 * This is wrong! "Byte-aligned" first EOL means extra zero bits
 * which are not allowed by T.4. Reencode first row to fix this
 * "byte-alignment".
 */
void MemoryDecoder::fixFirstEOL()
{
    fxStackBuffer result;
    G3Encoder enc(result);
    enc.setupEncoder(fillorder, is2D);
    
    memset(rowBuf, 0, byteWidth*sizeof(u_char)); // clear row to white
    if(!RTCraised()) {
        u_char* start = current();
        (void)decodeRow(rowBuf, width);
        /*
         * syncronize to the next EOL and calculate pointer to it
         * (see detailed explanation of look_ahead in TagLine.c++)
         */
        (void)isNextRow1D();
        u_int look_ahead = roundup(getPendingBits(),8) / 8;
        u_int decoded = current() - look_ahead - start;

        enc.encode(rowBuf, width, 1);
        u_int encoded = result.getLength();
            
        while( encoded < decoded ){
            result.put((char) 0);
            encoded++;
        }
        if( encoded == decoded ){
            memcpy(start, (const char*)result, encoded);
        }
    }
}


/*
 * TIFF Class F specs say:
 *
 * "Aside from EOL's, TIFF Class F files contain only image data. This
 * means that the Return To Control sequence (RTC) is specifically
 * prohibited..."
 *
 * Nethertheless Ghostscript and possibly other TIFF Class F writers
 * append RTC or single EOL to the last encoded line. Remove them.
 */
u_char* MemoryDecoder::cutExtraRTC()
{
    u_char* start = current();
    
    /*
     * We expect RTC near the end of data and thus
     * do not check all image to save processing time.
     * It's safe because we will resync on the first 
     * encountered EOL.
     *
     * NB: We expect G3Decoder::data==0 and
     * G3Decoder::bit==0 (no data in the accumulator).
     * As we cannot explicitly clear the accumulator
     * (bit and data are private), cutExtraRTC()
     * should be called immediately after
     * MemoryDecoder() constructing.
     */
    const u_long CheckArea = 20;
    if( cc > CheckArea ){
        bp += (cc-CheckArea);
        cc = CheckArea;
    }
        
    endOfData = NULL;
    if(!RTCraised()) {
        /*
         * syncronize to the next EOL and calculate pointer to it
         * (see detailed explanation of look_ahead in TagLine.c++)
         */
        (void)isNextRow1D();
        u_int look_ahead = roundup(getPendingBits(),8) / 8;
        endOfData = current() - look_ahead;
        for (;;) {
            if( decodeRow(NULL, width) ){
                /*
                 * endOfData is now after last good row. Thus we correctly handle
                 * RTC, single EOL in the end, or no RTC/EOL at all
                 */
                endOfData = current();
            }
            if( seenRTC() )
                break;
        }
    }
    return endOfData;
}

void
FaxModem::correctPhaseCData(u_char* buf, u_long* pBufSize,
                            u_int fillorder, const Class2Params& params)
{
    MemoryDecoder dec1(buf, params.pageWidth(), *pBufSize, fillorder, params.is2D());
    dec1.fixFirstEOL();
    /*
     * We have to construct new decoder. See comments to cutExtraRTC().
     */
    MemoryDecoder dec2(buf, params.pageWidth(), *pBufSize, fillorder, params.is2D());
    u_char* endOfData = dec2.cutExtraRTC();
    if( endOfData )
        *pBufSize = endOfData - buf;
}
