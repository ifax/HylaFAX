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
#ifndef _ixo_
#define	_ixo_
/*
 * IXO/TAP protocol parameters used in this implementation.
 * All timeouts are in seconds.  Several are guesses and may
 * need to be lengthened depending on the service provider.
 *
 * These should be configurable on a per-paging service basis.
 */
#define	IXO_IDPROBE		2	// time to resend \r during ID sequence
#define	IXO_IDTIMEOUT		20	// timeout waiting for ID=
#define	IXO_SERVICE		"PG"	// service identification
#define	IXO_DEVICEID		"1"	// entry device category
#define	IXO_LOGINRETRIES	3	// max login attempts
#define	IXO_MAXUNKNOWN		3	// max unknown messages to accept
#define	IXO_LOGINTIMEOUT	15	// timeout on login response
#define	IXO_GATIMEOUT		30	// timeout waiting for go-ahead msg
#define	IXO_XMITRETRIES		3	// 3 attempts to send message block
#define	IXO_XMITTIMEOUT		45	// timeout for message block xmit resp
#define	IXO_ACKTIMEOUT		30	// timeout waiting for transaction ack

#define STX	02
#define EOT	04
#define ACK	06
#define NAK	025
#define ESC	033
#ifdef RS
#undef	RS				// for HPUX at least
#endif
#define RS	036
#endif /* _ixo_ */
