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
#ifndef _FAXMODEM_
#define	_FAXMODEM_
/*
 * Class 1, 2, and 2.0 Fax Modem Driver Interface.
 */
#include "ClassModem.h"
#include "Class2Params.h"
#include "tiffio.h"
#include "G3Decoder.h"
#include "FaxSendStatus.h"
#include "NSF.h"
#include "FaxParams.h"

class FaxMachineInfo;
class fxStackBuffer;
class FaxFont;
class FaxServer;

// NB: these would be enums in the FaxModem class
//     if there were a portable way to refer to them!
typedef unsigned int RTNHandling;       // RTN signal handling method 

/*
 * This is an abstract class that defines the interface to
 * the set of modem drivers.  Real drivers are derived from
 * this and fill in the pure virtual methods to, for example,
 * send specific commands to the modem.  The Class2Params
 * structure defines the session parameters used/supported
 * by this interface.  Class2Params is derived from the
 * set of parameters supported by the Class 2 interface spec.
 */
class FaxModem : public ClassModem, public G3Decoder {
private:
    FaxServer&	server;		// server for getting to device
// transmit tag line support
    u_int	pageNumber;	// current transmit page number
    u_int	pageNumberOfJob;// current transmit page number of Job
    FaxFont*	tagLineFont;	// font for imaging tag line
    u_int	tagLineSlop;	// extra space reserved for tag line re-encoding
    fxStr	tagLine;	// tag line formatted with transmit time
    u_int	tagLineFields;	// number of fields in the tag line

    void	setupTagLine(const FaxRequest&, const fxStr& tagLineFmt);
// phase c data receive & copy quality checking
    u_int	cblc;		// current count of consecutive bad lines
    bool	lastRowBad;	// last decoded row was bad

    u_long	recvEOLCount;	// EOL count for received page
    u_long	recvBadLineCount;
    u_long	recvConsecutiveBadLineCount;
    tstrip_t	recvStrip;	// current strip number during receive
    u_char*	recvRow;	// current receive row raster
    u_long	savedWriteOff;	// file offset to start of page data
    int		decoderFd[2];	// file descriptors for the decoder pipe
    int		counterFd[2];	// file descriptors for the counter pipe
    pid_t	decoderPid;	// process id for the decoding process

    void	flushEncodedData(TIFF*, tstrip_t, const u_char*, u_int);
    void	flushRawData(TIFF*, tstrip_t, const u_char*, u_int);
    void	invalidCode(const char* type, int x);
    void	badPixelCount(const char* type, int got, int expected);
    void	badDecodingState(const char* type, int x);
// send/receive session state
    u_int	optFrames;	// mask of optional frames received 
    fxStr	tsi;		// received TSI/CSI
    fxStr	sub;		// received subaddressing string
    fxStr	pwd;		// received password string
    NSF         nsf;		// received nonstandard facilities
    // NB: remaining session state is below (params) or maintained by subclass
protected:
// NB: these are defined protected for convenience (XXX)
    Class2Params modemParams;	// NOTE: these are masks of Class 2 codes
    u_int	minsp;		// minimum required signalling rate
    FaxRequest*	curreq;		// current job request being processed
    int		bytePending;	// pending byte on recv
    fxStackBuffer* recvBuf;	// raw recv data for when copy quality disabled
    uint32	group3opts;	// for writing received TIFF
    Class2Params params;	// current session params
    u_int       recvFillOrder;  // bit order of recvd data (ModemConfig & autodetected)
    u_int       sendFillOrder;  // bit order of sent data (ModemConfig & autodetected)
    const u_char* rtcRev;       // bit reversal table for RTC

    FaxModem(FaxServer&, const ModemConfig&);

// miscellaneous
    void	countPage();
    int		getPageNumber();
    void	recvTrace(const char* fmt, ...);
    void	copyQualityTrace(const char* fmt, ...);
    void	traceModemParams();
    void	tracePPR(const char* dir, u_int ppr);
    void	tracePPM(const char* dir, u_int ppm);
// server-related stuff
    bool	getHDLCTracing();
    FaxSendStatus sendSetupParams(TIFF*, Class2Params&,
		    FaxMachineInfo&, fxStr&);
    void	recvTSI(const fxStr&);
    void	recvPWD(const fxStr&);
    void	recvSUB(const fxStr&);
    void	recvNSF(const NSF&);
    void	recvCSI(const fxStr&);
    void	recvDCS(const Class2Params&);
    void	recvSetupTIFF(TIFF* tif, long group3opts, int fillOrder, const fxStr& id);
    void	recvStartPage(TIFF* tif);
    void	recvResetPage(TIFF* tif);
    u_int	decodePageChop(const fxStr& pph, const Class2Params&);
    bool	decodePPM(const fxStr& pph, u_int& ppm, fxStr& emsg);
    void	notifyPageSent(TIFF*);
// phase c data receive & copy quality checking
    bool	recvPageDLEData(TIFF* tif, bool checkQuality,
		    const Class2Params& params, fxStr& emsg);
    void	setupStartPage(TIFF* tif, const Class2Params& params);
    void	recvEndPage(TIFF* tif, const Class2Params& params);
    void	writeECMData(TIFF*, u_char*, u_int, const Class2Params&, u_short);
    void	initializeDecoder(const Class2Params&);
    virtual void abortPageRecv() = 0;
    virtual int nextByte();

    bool	checkQuality();
    bool	isQualityOK(const Class2Params&);
    u_long	getRecvEOLCount() const;
    u_long	getRecvBadLineCount() const;
    u_long	getRecvConsecutiveBadLineCount() const;
// tag line support
    bool	setupTagLineSlop(const Class2Params&);
    u_int	getTagLineSlop() const;
    u_char*	imageTagLine(u_char* buf, u_int fillorder, const Class2Params&, u_long& totdata);
/*
 * Correct if neccessary Phase C (T.4/T.6) data (remove extra RTC/EOFB etc.)
 */
    int		correctPhaseCData(u_char* buf, u_long* pBufSize,
                                  u_int fillorder, const Class2Params& params);
/*
 * Convert Phase C data...
 */
    u_char*	convertPhaseCData(u_char* buf, u_long& totdata, u_int fillorder,
                            const Class2Params& params, const Class2Params& newparams);
public:
    enum {			// FaxModem::RTNHandling
        RTN_RETRANSMIT = 0,         // retransmit page after RTN until MCF/MPS
        RTN_GIVEUP     = 1,         // immediately abort
        RTN_IGNORE     = 2          // ignore error and send next page
    };

    virtual ~FaxModem();

    bool isFaxModem() const;

// configuration controls
    virtual void setLID(const fxStr& number) = 0;
    u_int getCapabilities() const;
// methods for querying modem capabilities
    virtual bool supports2D() const;
    virtual bool supportsMMR() const;
    virtual bool supportsEOLPadding() const;
    virtual bool supportsVRes(float res) const;
    virtual bool supportsPageWidth(u_int w, u_int r) const;
    virtual bool supportsPageLength(u_int l) const;
    virtual bool supportsPolling() const;
    virtual bool supportsECM(u_int ec = 0) const;

    virtual int selectSignallingRate(int br) const;
    u_int getBestSignallingRate() const;

    u_int getBestScanlineTime() const;
    virtual int selectScanlineTime(int st) const;

    u_int getVRes() const;
    u_int getBestDataFormat() const;
    u_int getBestPageWidth() const;
    u_int getBestPageLength() const;
    u_int getBestECM() const;
    FaxParams modemDIS() const;

    /*
     * Fax send protocol.  The expected sequence is:
     *
     * if (faxService() && sendSetup(req, params, emsg) && dial(number, emsg) == OK) {
     *	  sendBegin();
     *	  if (getPrologue() == send_ok and parameters acceptable) {
     *	     select send parameters
     *	     sendSetupPhaseB();
     *	     for (each file)
     *		if (!sendPhaseB()) break;
     *	  }
     *	  sendEnd();
     * }
     * hangup();
     *
     * The post page handling parameter to sendPhaseB enables the
     * client to control whether successive files are lumped together
     * as a single T.30 document or split apart.  This is important
     * for doing things like keeping cover pages & documents in a
     * single T.30 document.
     */
    virtual bool faxService(bool enableV34) = 0;
    virtual bool sendSetup(FaxRequest&, const Class2Params& dis, fxStr& emsg);
    virtual void sendBegin();
    virtual FaxSendStatus getPrologue(Class2Params&,
	bool& hasDoc, fxStr& emsg, u_int& batched) = 0;
    virtual void sendSetupPhaseB(const fxStr& pwd, const fxStr& sub);
    virtual FaxSendStatus sendPhaseB(TIFF*, Class2Params&, FaxMachineInfo&,
	fxStr& pph, fxStr& emsg, u_int& batched) = 0;
    virtual void sendEnd();
    virtual void sendAbort() = 0;
    // query interfaces for optional state
    virtual bool getSendCSI(fxStr&);
    virtual bool getSendNSF(NSF&);

    /*
     * Fax receive protocol.  The expected sequence is:
     *
     * if (waitForRings(nrings, ctype)) {	# wait before answering phone
     *    case (answerCall(type, emsg)) {
     *    CALLTYPE_FAX:
     *	    if (recvBegin()) {
     *	      do {
     *		TIFF* tif = TIFFOpen(..., "w");
     *		int ppm = PPM_EOP;
     *		do {
     *		    if (!recvPage(tif, ppm, emsg))
     *		        error during receive;
     *	        } while (ppm == PPM_MPS);
     *		deal with received file
     *	      } while (ppm != PPM_EOP);
     *	      recvEnd();
     *	    }
     *	    hangup();
     *	  CALLTYPE_DATA:
     *	    dataService();
     *	    do data kinds of things...
     *	  CALLTYPE_VOICE:
     *	    voiceService();
     *	    do voice kinds of things...
     *    }
     * }
     */
    virtual bool setupReceive() = 0;
    virtual bool recvBegin(fxStr& emsg);
    virtual bool recvEOMBegin(fxStr& emsg);
    virtual bool recvPage(TIFF*, u_int& ppm, fxStr& em, const fxStr& id) = 0;
    virtual bool recvEnd(fxStr& emsg) = 0;
    virtual void recvAbort() = 0;
    virtual void recvSucceeded();
    virtual void pokeConfig() = 0;
    // query interfaces for optional state
    virtual bool getRecvPWD(fxStr&);
    virtual bool getRecvTSI(fxStr&);
    virtual bool getRecvSUB(fxStr&);
    virtual const Class2Params& getRecvParams() const;

    /*
     * Polling protocol (for polling a remote site).  This is done
     * in conjunction with a send operation: first, before dialing,
     * call requestToPoll(), then after sending any files, do:
     *
     * if (pollBegin(...)) {
     *    do {
     *	    TIFF* tif = TIFFOpen(..., "w");
     *	    if (recvPhaseB(tif, ..., ppm, ...) deal with received file
     *	  } while (ppm != PPM_EOP);
     *	  recvEnd();
     * }
     *
     * (i.e. it's just like a receive operation.)
     */
    virtual bool requestToPoll(fxStr& emsg) = 0;
    virtual bool pollBegin(
	const fxStr& cig, const fxStr& sep, const fxStr& pwd,
	fxStr& emsg);
};
#endif /* _FAXMODEM_ */
