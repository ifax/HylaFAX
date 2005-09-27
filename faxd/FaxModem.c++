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
    rtcRev        = TIFFGetBitRevTable(sendFillOrder != FILLORDER_LSB2MSB);
    pageNumberOfCall = 1;
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
    minsp = fxmax((u_int) req.minbr, fxmax((u_int) conf.minSpeed, modemParams.getMinSpeed()));
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
FaxModem::recvEOMBegin(fxStr&)
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

/*
 * Issue a command upon successful reception.
 */
void
FaxModem::recvSucceeded()
{
    if (conf.recvSuccessCmd != "")
	atCmd(conf.recvSuccessCmd, AT_OK);
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
    return bestBit(modemParams.br, BR_33600, BR_2400);
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
FaxModem::getVRes() const
{
    /*
     * We don't use bestBit() here because T.32 Table 21
     * states that VR is to be reported as a bitmask
     * of supported resolutions.  So we already have it.
     */
    return (modemParams.vr);
}

/*
 * Return the best page width the modem supports.
 */
u_int
FaxModem::getBestPageWidth() const
{
    // XXX NB: we don't use anything > WD_A3
    return bestBit(modemParams.wd, WD_A3, WD_A4);
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
    return bestBit(modemParams.df, DF_2DMMR, DF_1DMH);
}

/*
 * Return the best ECM functionality the modem supports.
 */
u_int
FaxModem::getBestECM() const
{
    return bestBit(modemParams.ec, EC_ECLFULL, EC_DISABLE);
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
 * Return whether or not the modem supports 2DMMR.
 */
bool
FaxModem::supportsMMR() const
{
    return (modemParams.df & BIT(DF_2DMMR)) != 0;
}

/*
 * Return whether or not the modem supports JBIG.
 */
bool
FaxModem::supportsJBIG() const
{
    return (modemParams.df & BIT(DF_JBIG)) != 0;
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
FaxModem::supportsECM(u_int ec) const
{
    if (ec)
	return (modemParams.ec & BIT(ec)) != 0;
    else	// supports "any ecm"
	return (modemParams.ec &~ BIT(EC_DISABLE)) != 0;
}

/*
 * Return whether or not the modem supports the
 * specified vertical resolution.  Note that we're
 * rather tolerant because of potential precision
 * problems and general sloppiness on the part of
 * applications writing TIFF files.
 *
 * Because R8 and R16 vertical resolutions are the same
 * but differ by horizontal resolution, R16 is "coded"
 * as "20" in order to support it.
 */
bool
FaxModem::supportsVRes(float res) const
{
    if (3.0 <= res && res < 4.75)
	return ((modemParams.vr & VR_NORMAL) || (modemParams.vr & VR_200X100)) != 0;
    else if (5.9 <= res && res < 9.8)
	return ((modemParams.vr & VR_FINE) || (modemParams.vr & VR_200X200)) != 0;
    else if (9.8 <= res && res < 13)
	return (modemParams.vr & VR_300X300) != 0;
    else if (13 <= res && res < 19)
	return ((modemParams.vr & VR_R8) || (modemParams.vr & VR_200X400)) != 0;
    else if (res == 20)
	return (modemParams.vr & VR_R16) != 0;
    else
	return false;
}

/*
 * Return whether or not the modem supports the
 * specified page width.
 */
bool
FaxModem::supportsPageWidth(u_int w, u_int r) const
{
    switch (r) {
	case VR_R16:
	    switch (w) {
		case 3456:   return (modemParams.wd & BIT(WD_A4)) != 0;
		case 4096:   return (modemParams.wd & BIT(WD_B4)) != 0;
		case 4864:   return (modemParams.wd & BIT(WD_A3)) != 0;
	    }
	case VR_300X300:
	    switch (w) {
		case 2592:   return (modemParams.wd & BIT(WD_A4)) != 0;
		case 3072:   return (modemParams.wd & BIT(WD_B4)) != 0;
		case 3648:   return (modemParams.wd & BIT(WD_A3)) != 0;
	    }
	case VR_NORMAL:
	case VR_FINE:
	case VR_R8:
	case VR_200X100:
	case VR_200X200:
	case VR_200X400:
	    switch (w) {
		case 1728:   return (modemParams.wd & BIT(WD_A4)) != 0;
		case 2048:   return (modemParams.wd & BIT(WD_B4)) != 0;
		case 2432:   return (modemParams.wd & BIT(WD_A3)) != 0;
	    }
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
FaxParams
FaxModem::modemDIS() const
{
    Class2Params tmp(modemParams);
    tmp.update(true);
    return tmp;
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
    traceBitMask(modemParams.vr, Class2Params::verticalResNames);
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
    if ((ppm & 0x7F) == FCF_DCS) {
	protoTrace("%s DCS (command signal)", dir);
	return;
    }
    if ((ppm & 0x7F) == FCF_TSI) {
	protoTrace("%s TSI (sender id)", dir);
	return;
    }
    if ((ppm & 0x7F) == FCF_CRP) {
	protoTrace("%s CRP (command repeat)", dir);
	return;
    }
    static const char* ppmNames[16] = {
	"NULL (more blocks, same page)",		// PPS-NULL
	"EOM (more documents)",				// FCF_EOM
	"MPS (more pages, same document)",		// FCF_MPS
	"EOR (end of retransmission)",			// FCF_EOR
	"EOP (no more pages or documents)",		// FCF_EOP
	"unknown PPM 0x05",
	"RR (receive ready)",				// FCF_RR
	"unknown PPM 0x07",
	"CTC (continue to correct)",			// FCF_CTC
	"PRI-EOM (more documents after interrupt)",	// FCF_PRI_EOM
	"PRI-MPS (more pages after interrupt)",		// FCF_PRI_MPS
	"unknown PPM 0x0B",
	"PRI-EOP (no more pages after interrupt)",	// FCF_PRI_EOP
	"PPS (partial page signal)",			// FCF_PPS
	"unknown PPM 0x0E",
	"DCN (disconnect)",				// FCF_DCN
    };
    protoTrace("%s %s", dir, ppmNames[ppm&0xf]);
}

void
FaxModem::tracePPR(const char* dir, u_int ppr)
{
    if ((ppr & 0x7f) == 0x58)				// avoid CRP-ERR collision
	protoTrace("%s %s", dir, "CRP (command repeat)");
    else if ((ppr & 0x7f) == 0x23)			// avoid CTR-RTP collision
	protoTrace("%s %s", dir, "CTR (confirm continue to correct)");
    else if ((ppr & 0x7f) == FCF_CFR)			// avoid CFR-MPS collision
	protoTrace("%s %s", dir, "CFR (confirmation to receive)");
    else if ((ppr & 0x7f) == FCF_NSF)			// avoid NSF-PIN collision
	protoTrace("%s %s", dir, "NSF (non-standard facilities)");
    else {
	static const char* pprNames[16] = {
	    "unknown PPR 0x00",
	    "MCF (message confirmation)",		// FCF_MCF/PPR_MCF
	    "RTN (retrain negative)",			// FCF_RTN/PPR_RTN
	    "RTP (retrain positive)",			// FCF_RTP/PPR_RTP
	    "PIN (procedural interrupt negative)",	// FCF_PIN/PPR_PIN
	    "PIP (procedural interrupt positive)",	// FCF_PIP/PPR_PIP
	    "unknown PPR 0x06",
	    "RNR (receive not ready)",			// FCF_RNR
	    "ERR (confirm end of retransmission)",	// FCF_ERR
	    "unknown PPR 0x09",
	    "unknown PPR 0x0A",
	    "unknown PPR 0x0B",
	    "unknown PPR 0x0C",
	    "PPR (partial page request)",		// FCF_PPR
	    "unknown PPR 0x0E",
	    "DCN (disconnect)",				// FCF_DCN
	};
        protoTrace("%s %s", dir, pprNames[ppr&0xf]);
    }
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
bool FaxModem::getECMTracing()
    { return (server.getSessionTracing() & FAXTRACE_ECM) != 0; }

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
    (void) TIFFGetField(tif, TIFFTAG_STRIPOFFSETS, &savedWriteOff);
    (void) TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &savedStripByteCounts);
    pageStarted = true;
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
    if (!pageStarted) return;
    TIFFSetWriteOffset(tif, 0);		// force library to reset state
    TIFFSetField(tif, TIFFTAG_STRIPOFFSETS, savedWriteOff);
    TIFFSetField(tif, TIFFTAG_STRIPBYTECOUNTS, savedStripByteCounts);
}

void
FaxModem::countPage()
{
    pageNumber++;
    pageNumberOfJob++;
    pageNumberOfCall++;
}

int
FaxModem::getPageNumberOfCall()
{
    return pageNumberOfCall;
}

void
FaxModem::notifyPageSent(TIFF* tif)
{
    if (curreq)
	server.notifyPageSent(*curreq, TIFFFileName(tif));
}

#include "MemoryDecoder.h"

int
FaxModem::correctPhaseCData(u_char* buf, u_long* pBufSize,
                            u_int fillorder, const Class2Params& params, uint32& rows)
{
    u_char* endOfData;
    int lastbyte = 0;
    if (params.df == DF_2DMMR) {
	MemoryDecoder dec1(buf, params.pageWidth(), *pBufSize, fillorder, params.is2D(), true);
	endOfData = dec1.cutExtraEOFB();
	lastbyte = dec1.getLastByte();
	rows = dec1.getRows();
    } else {
	MemoryDecoder dec1(buf, params.pageWidth(), *pBufSize, fillorder, params.is2D(), false);
	dec1.fixFirstEOL();
	/*
	 * We have to construct new decoder. See comments to cutExtraRTC().
	 */
	MemoryDecoder dec2(buf, params.pageWidth(), *pBufSize, fillorder, params.is2D(), false);
	endOfData = dec2.cutExtraRTC();
	rows = dec2.getRows();
    }
    if( endOfData )
        *pBufSize = endOfData - buf;
    return lastbyte;
}

u_char*
FaxModem::convertPhaseCData(u_char* buf, u_long& totdata, u_int fillorder, 
			    const Class2Params& params, const Class2Params& newparams, uint32& rows)
{
    MemoryDecoder dec(buf, params.pageWidth(), totdata, fillorder, params.is2D(), (params.df == DF_2DMMR));
    u_char* data = dec.convertDataFormat(newparams);
    totdata = dec.getCC();
    rows = dec.getRows();
    return (data);
}
