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
#ifndef _FaxServer_
#define	_FaxServer_
/*
 * Fax Modem and Protocol Server.
 */
#include "ModemServer.h"
#include "FaxRecvInfo.h"
#include "Array.h"

class FaxAcctInfo;

fxDECLARE_ObjArray(FaxRecvInfoArray, FaxRecvInfo)

/*
 * This class defines the ``server process'' that manages the fax
 * modem and implements the necessary protocol above the FaxModem
 * driver interface.  When the server is multi-threaded, this class
 * embodies a separate thread.
 */
class FaxServer : public ModemServer {
private:
    FaxModem*	modem;			// modem driver
// group 3 protocol-related state
    Class2Params clientCapabilities;	// received client capabilities
    Class2Params clientParams;		// current session parameters
// for fax reception ...
    fxStr	recvTSI;		// sender's TSI
    fxStr	hostname;		// host on which fax is received
    u_int	recvPages;		// count of received pages
    pid_t	waitNotifyPid;		// pid of last notification process
// send+receive stats
    time_t	connTime;		// time connected to peer
    time_t	fileStart;		// starting time for file transmit
    time_t	pageStart;		// starting time for page transmit
    time_t	pageTransferTime;	// transfer time for last page
    u_int	npages;			// # pages sent/received
    fxStr	batchid;		// Batch ID, for session logs

    friend class FaxModem;

// FAX transmission protocol support
    void	sendFax(FaxRequest& fax, FaxMachineInfo&, const fxStr& number, u_int&);
    bool	sendClientCapabilitiesOK(FaxRequest&, FaxMachineInfo&, fxStr&);
    bool	sendFaxPhaseB(FaxRequest&, FaxItem&, FaxMachineInfo&, u_int, bool);
    void	sendPoll(FaxRequest& fax, bool remoteHasDoc);
    FaxSendStatus sendSetupParams(TIFF*,
		    Class2Params&, const FaxMachineInfo&, fxStr&);
    FaxSendStatus sendSetupParams1(TIFF*,
		    Class2Params&, const FaxMachineInfo&, fxStr&);
    void	sendFailed(FaxRequest& fax,
		    FaxSendStatus, const char* notice, u_int tts = 0);
// FAX reception support
    int		getRecvFile(fxStr& qfile, fxStr& emsg);
    TIFF*	setupForRecv(FaxRecvInfo&, FaxRecvInfoArray&, fxStr& emsg);
    bool	recvDocuments(TIFF*, FaxRecvInfo&, FaxRecvInfoArray&,
		    fxStr& emsg);
    bool	recvFaxPhaseD(TIFF* tif, FaxRecvInfo&, u_int& ppm, fxStr& emsg);
    bool	pollFaxPhaseB(const fxStr& sep, const fxStr& pwd,
		    FaxRecvInfoArray&, fxStr& emsg);
protected:
    FaxServer(const fxStr& deviceName, const fxStr& devID);

    bool	setupModem(bool isSend = true);
    ClassModem*	deduceModem(bool isSend = true);
    void	discardModem(bool dropDTR);
    fxStr	getModemCapabilities() const;

    void	readConfig(const fxStr& filename);
    void	setLocalIdentifier(const fxStr& lid);

    void	sendFax(FaxRequest&, FaxMachineInfo&, FaxAcctInfo&, u_int&);
    bool	recvFax(const CallID& callid, fxStr& emsg);

    time_t	getFileTransferTime() const;
    time_t	getPageTransferTime() const;
    time_t	setPageTransferTime();
    time_t	getConnectTime() const;
    const Class2Params& getClientParams() const;

// notification interfaces overridden in derived class
    virtual void notifyCallPlaced(const FaxRequest&);
    virtual void notifyConnected(const FaxRequest&);
    virtual void notifyPageSent(FaxRequest&, const char*);
    virtual void notifyDocumentSent(FaxRequest&, u_int index);
    virtual void notifyPollRecvd(FaxRequest&, FaxRecvInfo&);
    virtual void notifyPollDone(FaxRequest&, u_int index);
    virtual void notifyRecvBegun(FaxRecvInfo&);
    virtual void notifyPageRecvd(TIFF* tif, FaxRecvInfo&, int ppm);
    virtual void notifyDocumentRecvd(FaxRecvInfo& req);
    virtual void notifyRecvDone(FaxRecvInfo& req);
public:
    virtual ~FaxServer();

    void initialize(int argc, char** argv);

    const fxStr& getLocalIdentifier() const;

    bool modemSupports2D() const;
    bool modemSupportsMMR() const;
    bool modemSupportsEOLPadding() const;
    bool modemSupportsVRes(float res) const;
    bool modemSupportsPageLength(u_int l) const;
    bool modemSupportsPolling() const;
};
#endif /* _FaxServer_ */
