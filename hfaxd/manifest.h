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
#ifndef _manifest_
#define	_manifest_
/*
 * Manifest Defintions.
 */

/*
 * Users are assigned 16-bit IDs that are used
 * to implement the access control mechanisms
 * for documents, jobs, etc.  These numbers are
 * maintained private to the fax software.  On
 * UNIX systems we store the uid in the group ID
 * of files and use the group protection bits for
 * access control.
 */
#if defined(CONFIG_MAXGID) && CONFIG_MAXGID < 60002
#define	FAXUID_MAX	CONFIG_MAXGID	// constrain to system limit
#else
#define	FAXUID_MAX	60002		// fits in unsigned 16-bit value
#endif
#define	FAXUID_ANON	FAXUID_MAX	// UID of anonymous user

/*
 * The client-server protocol is derived from the Internet
 * File Transfer Protocol (FTP).  We extend two FTP notions,
 * data transfer mode and file structure, to satisfy our
 * specific needs.
 */

/*
 * Reply codes.
 */
#define	PRELIM		1	// positive preliminary
#define	COMPLETE	2	// positive completion
#define	CONTINUE	3	// positive intermediate
#define	TRANSIENT	4	// transient negative completion
#define	ERROR		5	// permanent negative completion

/*
 * Type codes
 */
#define	TYPE_A		0	// ASCII
#define	TYPE_E		1	// EBCDIC
#define	TYPE_I		2	// image
#define	TYPE_L		3	// local byte size

/*
 * Structure codes
 */
#define	STRU_F		0	// file (no record structure)
#define	STRU_R		1	// record structure
#define	STRU_P		2	// page structure
#define	STRU_T		3	// TIFF structured files

/*
 * Mode types
 */
#define	MODE_S		0	// stream
#define	MODE_B		1	// block
#define	MODE_C		2	// compressed
#define	MODE_Z		3	// zlib-compressed data mode

/*
 * File format types.
 */
#define	FORM_TIFF	0	// Tagged Image File Format
#define	FORM_PS		1	// PostScript Level I
#define	FORM_PS2	2	// PostScript Level II
#define	FORM_PCL	3	// HP-PCL5

/*
 * Definitions for the TELNET protocol (only those used).
 */
#define	IAC		255	// interpret as command:
#define	DONT		254	// you are not to use option
#define	DO		253	// please, you use option
#define	WONT		252	// I won't use option
#define	WILL		251	// I will use option
#endif /* _manifest_ */
