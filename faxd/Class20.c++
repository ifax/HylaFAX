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
#include "Class20.h"
#include "ModemConfig.h"
#include "StackBuffer.h"

#include <stdlib.h>
#include <ctype.h>

Class20Modem::Class20Modem(FaxServer& s, const ModemConfig& c) : Class2Modem(s,c)
{
    serviceType = SERVICE_CLASS20;
    setupDefault(classCmd,	conf.class2Cmd,		"AT+FCLASS=2.0");
    setupDefault(mfrQueryCmd,	conf.mfrQueryCmd,	"AT+FMI?");
    setupDefault(modelQueryCmd,	conf.modelQueryCmd,	"AT+FMM?");
    setupDefault(revQueryCmd,	conf.revQueryCmd,	"AT+FMR?");
    setupDefault(dccQueryCmd,	conf.class2DCCQueryCmd, "AT+FCC=?");
    setupDefault(abortCmd,	conf.class2AbortCmd,	"AT+FKS");

    setupDefault(borCmd,	conf.class2BORCmd,	"AT+FBO=0");
    setupDefault(tbcCmd,	conf.class2TBCCmd,	"AT+FPP=0");
    setupDefault(crCmd,		conf.class2CRCmd,	"AT+FCR=1");
    setupDefault(phctoCmd,	conf.class2PHCTOCmd,	"AT+FCT=30");
    setupDefault(bugCmd,	conf.class2BUGCmd,	"AT+FBU=1");
    setupDefault(lidCmd,	conf.class2LIDCmd,	"AT+FLI");
    setupDefault(dccCmd,	conf.class2DCCCmd,	"AT+FCC");
    setupDefault(disCmd,	conf.class2DISCmd,	"AT+FIS");
    setupDefault(cigCmd,	conf.class2CIGCmd,	"AT+FPI");
    setupDefault(splCmd,	conf.class2SPLCmd,	"AT+FSP");
    setupDefault(ptsCmd,	conf.class2PTSCmd,	"AT+FPS");
    setupDefault(ptsQueryCmd,	conf.class2PTSQueryCmd,	"AT+FPS?");
    setupDefault(minspCmd,	conf.class2MINSPCmd,	"AT+FMS");

    setupDefault(noFlowCmd,	conf.class2NFLOCmd,	"AT+FLO=0");
    setupDefault(softFlowCmd,	conf.class2SFLOCmd,	"AT+FLO=1");
    setupDefault(hardFlowCmd,	conf.class2HFLOCmd,	"AT+FLO=2");

    // ignore procedure interrupts
    setupDefault(pieCmd,	conf.class2PIECmd,	"AT+FIE=0");
    // enable reporting of everything
    setupDefault(nrCmd,		conf.class2NRCmd,	"AT+FNR=1,1,1,1");
}

Class20Modem::~Class20Modem()
{
}

ATResponse
Class20Modem::atResponse(char* buf, long ms)
{
    if (FaxModem::atResponse(buf, ms) == AT_OTHER &&
      (buf[0] == '+' && buf[1] == 'F')) {
	if (strneq(buf, "+FHS:", 5)) {
	    processHangup(buf+5);
	    lastResponse = AT_FHNG;
	    hadHangup = true;
	} else if (strneq(buf, "+FCO", 4))
	    lastResponse = AT_FCON;
	else if (strneq(buf, "+FPO", 4))
	    lastResponse = AT_FPOLL;
	else if (strneq(buf, "+FVO", 4))
	    lastResponse = AT_FVO;
	else if (strneq(buf, "+FIS:", 5))
	    lastResponse = AT_FDIS;
	else if (strneq(buf, "+FNF:", 5))
	    lastResponse = AT_FNSF;
	else if (strneq(buf, "+FCI:", 5))
	    lastResponse = AT_FCSI;
	else if (strneq(buf, "+FPS:", 5))
	    lastResponse = AT_FPTS;
	else if (strneq(buf, "+FCS:", 5))
	    lastResponse = AT_FDCS;
	else if (strneq(buf, "+FNS:", 5))
	    lastResponse = AT_FNSS;
	else if (strneq(buf, "+FTI:", 5))
	    lastResponse = AT_FTSI;
	else if (strneq(buf, "+FET:", 5))
	    lastResponse = AT_FET;
	else if (strneq(buf, "+FPA:", 5))
	    lastResponse = AT_FPA;
	else if (strneq(buf, "+FSA:", 5))
	    lastResponse = AT_FSA;
	else if (strneq(buf, "+FPW:", 5))
	    lastResponse = AT_FPW;
    }
    return (lastResponse);
}

/*
 * Abort a data transfer in progress.
 */
void
Class20Modem::abortDataTransfer()
{
    protoTrace("SEND abort data transfer");
    char c = CAN;
    putModemData(&c, 1);
}

/*
 * Send a page of data using the ``stream interface''.
 */
bool
Class20Modem::sendPage(TIFF* tif, u_int pageChop)
{
    /*
     * Support MT5634ZBA-V92 real-time fax compression conversion:
     * AT+FFC=? gives us non-zero data if RTFCC is supported.
     * Firstly, we must have set our FCC to MMR support (+FCC=,,,,3), and
     * we may need to have ECM enabled (+FCC=,,,,,1) if we intend to
     * allow RTFCC to send in MMR.  Now we send <DLE><char> where char =
     *    6Bh  -  if we formatted the image in MH
     *    6Ch  -  if we formatted the image in MR
     *    6Eh  -  if we formatted the image in MMR
     */
    if (conf.class2RTFCC) {
	protoTrace("Enable Real-Time Fax Compression Conversion");
	uint16 compression;
	char rtfcc[2];
	rtfcc[0] = DLE;
	TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
	if (compression != COMPRESSION_CCITTFAX4) {
	    uint32 g3opts = 0;
	    TIFFGetField(tif, TIFFTAG_GROUP3OPTIONS, &g3opts);
	    if ((g3opts & GROUP3OPT_2DENCODING) == DF_2DMR) {
		rtfcc[1] = 0x6C;	// MR
		protoTrace("Reading MR-compressed image file");
	    } else {
		rtfcc[1] = 0x6B;	// MH
		protoTrace("Reading MH-compressed image file");
	    }
	} else {
	    rtfcc[1] = 0x6E;		// MMR
	    protoTrace("Reading MMR-compressed image file");
	}
	putModemData(rtfcc, sizeof (rtfcc));
    }

    protoTrace("SEND begin page");
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(FLOW_XONXOFF, FLOW_NONE, ACT_FLUSH);
    bool rc = sendPageData(tif, pageChop);
    if (!rc)
	abortDataTransfer();
    else if( conf.class2SendRTC )
	rc = sendRTC(params);
    if (flowControl == FLOW_XONXOFF)
	setXONXOFF(getInputFlow(), FLOW_XONXOFF, ACT_DRAIN);
    protoTrace("SEND end page");
    return (rc);
}

/*
 * Handle the page-end protocol.  Class 2.0 returns
 * OK/ERROR according to the post-page response.  We
 * should query the modem to get the actual code, but
 * some modems don't support +FPS? and so instead we
 * synthesize codes, ignoring whether or not the
 * modem does retraining on the next page transfer.
 */
bool
Class20Modem::pageDone(u_int ppm, u_int& ppr)
{
    static char ppmCodes[3] = { 0x2C, 0x3B, 0x2E };
    char eop[2];

    if (ppm == PPH_SKIP)
    {
	ppr = PPR_MCF;
        return true;
    }

    eop[0] = DLE;
    eop[1] = ppmCodes[ppm];

    ppr = 0;            // something invalid
    if (putModemData(eop, sizeof (eop))) {
        for (;;) {
	    switch (atResponse(rbuf, conf.pageDoneTimeout)) {
            case AT_FHNG:
		waitFor(AT_OK);
                if (!isNormalHangup()) {
                    return (false);
                }
                ppr = PPR_MCF;
                return (true);
            case AT_OK:
                /*
                 * We do explicit status query e.g. to
                 * distinguish between MCF, RTP, and PIP.
		 * If we don't understand the response,
		 * assume that our query isn't supported.
                 */
                {
		    if (strcasecmp(conf.class2PTSQueryCmd, "none") != 0) {
			fxStr s;
			if(!atQuery(conf.class2PTSQueryCmd, s) ||
			    sscanf(s, "%u", &ppr) != 1) {
			    protoTrace("MODEM protocol botch (\"%s\"), %s",
			    (const char*)s, "can not parse PPR");
			    ppr = PPR_MCF;
			}
		    } else
			ppr = PPR_MCF;	// could be PPR_RTP/PPR_PIP
                }
                return (true);
            case AT_ERROR:
                /*
                 * We do explicit status query e.g. to
                 * distinguish between RTN and PIN 
		 * If we don't understand the response,
		 * assume that our query isn't supported.
                 */
                {
		    if (strcasecmp(conf.class2PTSQueryCmd, "none") != 0) {
			fxStr s;
			if(!atQuery(conf.class2PTSQueryCmd, s) ||
			    sscanf(s, "%u", &ppr) != 1) {
			    protoTrace("MODEM protocol botch (\"%s\"), %s",
			    (const char*)s, "can not parse PPR");
			    ppr = PPR_RTN;
			}
		    } else
			ppr = PPR_RTN;	// could be PPR_PIN
                }
                return (true);
            case AT_EMPTYLINE:
            case AT_TIMEOUT:
            case AT_NOCARRIER:
            case AT_NODIALTONE:
            case AT_NOANSWER:
                goto bad;
            }
        }
    }
bad:
    processHangup("50");        // Unspecified Phase D error
    return (false);
}

/*
 * Class 2.0 must override the default behaviour used
 * Class 1+2 modems in order to do special handling of
 * <DLE><SUB> escape (translate to <DLE><DLE>).
 */
int
Class20Modem::nextByte()
{
    int b;
    if (bytePending & 0x100) {
	b = bytePending & 0xff;
	bytePending = 0;
    } else {
	b = getModemDataChar();
	if (b == EOF)
	    raiseEOF();
    }
    if (b == DLE) {
	switch (b = getModemDataChar()) {
	    case 0x01:			// +FDB=1 support
		{
		    fxStr dbdata;
		    bool notdone = true;
		    do {
			b = getModemDataChar();
			if (b == DLE) {
			    b = getModemDataChar();
			    if (b == 0x04) {
				notdone = false;
				protoTrace("DCE DEBUG: %s", (const char*) dbdata);
			    } else {
				dbdata.append(DLE);
			    }
			}
			if (b != '\0' && b != '\r' && b != '\n')
			    dbdata.append(b);
		    } while (notdone);
		    b = nextByte();
		}
	    break;
	case EOF: raiseEOF();
	case ETX: raiseRTC();		// RTC
	case DLE: break;		// <DLE><DLE> -> <DLE>
	case SUB: b = DLE;		// <DLE><SUB> -> <DLE><DLE>
	    /* fall thru... */
	default:
	    bytePending = b | 0x100;
	    b = DLE;
	    break;
	}
    }
    b = getBitmap()[b];
    if (recvBuf)
	recvBuf->put(b);
    return (b);
}
