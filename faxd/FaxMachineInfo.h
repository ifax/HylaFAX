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
#ifndef _FaxMachineInfo_
#define	_FaxMachineInfo_
/*
 * Fax Machine Information Database Support.
 */
#include "Str.h"
#include "FaxConfig.h"
#include <stdarg.h>

class fxStackBuffer;
/*
 * Each remote machine the server sends a facsimile to
 * has information that describes capabilities that are
 * important in formatting outgoing documents, and, potentially,
 * controls on what the server should do when presented
 * with documents to send to the destination.  The capabilities
 * are treated as a cache; information is initialized to
 * be a minimal set of capabilities that all machines are
 * required (by T.30) to support and then updated according
 * to the DIS/DTC messages received during send operations.
 */
class FaxMachineInfo : public FaxConfig {
private:
    fxStr	file;			// pathname to info file
    u_int	locked;			// bit vector of locked items
    bool	changed;		// changed since restore
    bool	supportsHighRes;	// capable of 7.7 line/mm vres
    bool	supports2DEncoding;	// handles Group 3 2D
    bool	supportsPostScript;	// handles Adobe NSF protocol
    bool	calledBefore;		// successfully called before
    u_short		maxPageWidth;		// max capable page width
    u_short		maxPageLength;		// max capable page length
    u_short		maxSignallingRate;	// max capable signalling rate
    u_short		minScanlineTime;	// min scanline time capable
    fxStr	csi;			// last received CSI
    int		sendFailures;		// count of failed send attempts
    int		dialFailures;		// count of failed dial attempts
    fxStr	lastSendFailure;	// reason for last failed send attempt
    fxStr	lastDialFailure;	// reason for last failed dial attempt
    u_int	pagerMaxMsgLength;	// max text message length for pages
    fxStr	pagerPassword;		// pager service password string
    fxStr	pagerTTYParity;		// tty setting required by pager service
    fxStr	pagingProtocol;		// Protocol spoken by paging central
    fxStr	pageSource;		// String identifying page source
    fxStr	pagerSetupCmds;		// atcmds to overwrite value in config

    static const fxStr infoDir;

    void writeConfig(fxStackBuffer&);

    bool setConfigItem(const char* tag, const char* value);
    void vconfigError(const char* fmt0, va_list ap);
    void configError(const char* fmt0 ...);
    void configTrace(const char* fmt0 ...);
    void error(const char* fmt0 ...);
public:
    FaxMachineInfo();
    FaxMachineInfo(const FaxMachineInfo& other);
    virtual ~FaxMachineInfo();

    virtual bool updateConfig(const fxStr& filename);
    virtual void writeConfig();
    virtual void resetConfig();

    bool getSupportsHighRes() const;
    bool getSupports2DEncoding() const;
    bool getSupportsPostScript() const;
    bool getCalledBefore() const;
    u_short getMaxPageWidthInPixels() const;
    u_short getMaxPageWidthInMM() const;
    u_short getMaxPageLengthInMM() const;
    u_short getMaxSignallingRate() const;
    u_short getMinScanlineTime() const;
    const fxStr& getCSI() const;

    int getSendFailures() const;
    int getDialFailures() const;
    const fxStr& getLastSendFailure() const;
    const fxStr& getLastDialFailure() const;

    void setSupportsHighRes(bool);
    void setSupports2DEncoding(bool);
    void setSupportsPostScript(bool);
    void setCalledBefore(bool);
    void setMaxPageWidthInPixels(int);
    void setMaxPageLengthInMM(int);
    void setMaxSignallingRate(int);
    void setMinScanlineTime(int);
    void setCSI(const fxStr&);

    void setSendFailures(int);
    void setDialFailures(int);
    void setLastSendFailure(const fxStr&);
    void setLastDialFailure(const fxStr&);

    u_int getPagerMaxMsgLength() const;
    const fxStr& getPagerPassword() const;
    const fxStr& getPagerTTYParity() const;
    const fxStr& getPagingProtocol() const;
    const fxStr& getPageSource() const;
    const fxStr& getPagerSetupCmds() const;
};

inline bool FaxMachineInfo::getSupportsHighRes() const
    { return supportsHighRes; }
inline bool FaxMachineInfo::getSupports2DEncoding() const
    { return supports2DEncoding; }
inline bool FaxMachineInfo::getSupportsPostScript() const
    { return supportsPostScript; }
inline bool FaxMachineInfo::getCalledBefore() const	
    { return calledBefore; }
inline u_short FaxMachineInfo::getMaxPageWidthInPixels() const
    { return maxPageWidth; }
inline u_short FaxMachineInfo::getMaxPageLengthInMM() const
    { return maxPageLength; }
inline u_short FaxMachineInfo::getMaxSignallingRate() const
    { return maxSignallingRate; }
inline u_short FaxMachineInfo::getMinScanlineTime() const
    { return minScanlineTime; }
inline const fxStr& FaxMachineInfo::getCSI() const
    { return csi; }

inline int FaxMachineInfo::getSendFailures() const
    { return sendFailures; }
inline int FaxMachineInfo::getDialFailures() const
    { return dialFailures; }
inline const fxStr& FaxMachineInfo::getLastSendFailure() const
    { return lastSendFailure; }
inline const fxStr& FaxMachineInfo::getLastDialFailure() const
    { return lastDialFailure; }

inline u_int FaxMachineInfo::getPagerMaxMsgLength() const
    { return pagerMaxMsgLength; }
inline const fxStr& FaxMachineInfo::getPagerPassword() const
    { return pagerPassword; }
inline const fxStr& FaxMachineInfo::getPagerTTYParity() const
    { return pagerTTYParity; }
inline const fxStr& FaxMachineInfo::getPagingProtocol() const
    { return pagingProtocol; }
inline const fxStr& FaxMachineInfo::getPageSource() const
    { return pageSource; }
inline const fxStr& FaxMachineInfo::getPagerSetupCmds() const
    { return pagerSetupCmds; }
#endif /* _FaxMachineInfo_ */
