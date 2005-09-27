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
#ifndef _FaxTrace_
#define	_FaxTrace_
/*
 * Fax Server Tracing Definitions.
 */
const int FAXTRACE_SERVER	= 0x00001;	// server operation
const int FAXTRACE_PROTOCOL	= 0x00002;	// fax protocol
const int FAXTRACE_MODEMOPS	= 0x00004;	// modem operations
const int FAXTRACE_MODEMCOM	= 0x00008;	// modem communication
const int FAXTRACE_TIMEOUTS	= 0x00010;	// all timeouts
const int FAXTRACE_MODEMCAP	= 0x00020;	// modem capabilities
const int FAXTRACE_HDLC		= 0x00040;	// HDLC protocol frames
const int FAXTRACE_MODEMIO	= 0x00080;	// binary modem i/o
const int FAXTRACE_STATETRANS	= 0x00100;	// server state transitions
const int FAXTRACE_QUEUEMGMT	= 0x00200;	// job queue management
const int FAXTRACE_COPYQUALITY	= 0x00400;	// copy quality checking
const int FAXTRACE_JOBMGMT	= 0x00800;	// low-level job management
const int FAXTRACE_IXO		= 0x01000;	// IXO protocol messages
const int FAXTRACE_CONFIG	= 0x02000;	// configuration file parsing
const int FAXTRACE_FIFO		= 0x04000;	// FIFO messages
const int FAXTRACE_MODEMSTATE	= 0x08000;	// modem state changes
const int FAXTRACE_DIALRULES	= 0x10000;	// dialstring processing
const int FAXTRACE_DOCREFS	= 0x20000;	// document reference handling
const int FAXTRACE_TIFF		= 0x40000;	// TIFF library msgs
const int FAXTRACE_ECM		= 0x80000;	// ECM HDLC image data frames
const int FAXTRACE_ANY		= 0xffffffff;

const int FAXTRACE_MASK		= 0xfffff;
#endif /* _FaxTrace_ */
