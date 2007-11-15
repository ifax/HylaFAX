/*	$Id$ */
/*
 * Copyright (c) 2006 Aidan Van Dyk
 * Copyright (c) 2006 iFAX Solutions, Inc.
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

#include "HylaFAXServer.h"

#include <unistd.h>

#ifdef HAVE_PAM
extern "C" {
#include <security/pam_appl.h>
}



extern int
pamconv(int num_msg, STRUCT_PAM_MESSAGE **msg, struct pam_response **resp, void *appdata);


int
pamconv(int num_msg, STRUCT_PAM_MESSAGE **msg, struct pam_response **resp, void *appdata)
{
	char *password =(char*) appdata;
	struct pam_response* replies;

	if (num_msg != 1 || msg[0]->msg_style != PAM_PROMPT_ECHO_OFF)
	    return PAM_CONV_ERR;

//logWarning("pamconv: %d [%x] with \"%s\"", num_msg, msg[0]->msg_style, password);

	if (password == NULL)
	    return PAM_CONV_ERR;
#if 0
	    /*
	     * Solaris doesn't have PAM_CONV_AGAIN defined.
	     */
	    #ifdef PAM_CONV_AGAIN
		return PAM_CONV_AGAIN;
	    #else
		return PAM_CONV_ERR;
	    #endif
#endif

	replies=(struct pam_response*)calloc(num_msg, sizeof(struct pam_response));

	replies[0].resp = strdup(password);
	replies[0].resp_retcode = 0;
	*resp = replies;

	return PAM_SUCCESS;
}

int do_pamauth(pam_handle_t * pamh)
{
    int pamret;

    for (int i = 0; i < 10; i++)
    {
	/*
	 * PAM supports event-driven applications by returning PAM_INCOMPLETE
	 * and requiring the application to recall pam_authenticate after the
	 * underlying PAM module is ready.  The application is supposed to
	 * utilize the pam_conv structure to determine when the authentication
	 * module is ready.  However, in our case we're not event-driven, and
	 * so we can wait, and a call to sleep saves us the headache.
	 */
	pamret = pam_authenticate(pamh, 0);
#ifdef PAM_INCOMPLETE
	if (pamret != PAM_INCOMPLETE)
#endif
		break;
	sleep(1);
    };

    if (pamret == PAM_SUCCESS) 
    {
	pamret = pam_acct_mgmt(pamh, 0);
	if (pamret != PAM_SUCCESS)
	    logNotice("pam_acct_mgmt failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
    }
    return pamret;
}

bool do_pamcheck(const char* user, const char* passwd, const char* remoteaddr)
{
    /*
     * The effective uid must be privileged enough to
     * handle whatever the PAM module may require.
     */
    bool retval = false;
    uid_t ouid = geteuid();
    (void) seteuid(0);

    int pamret = PAM_SUCCESS;;
    pam_handle_t *pamh;
    struct pam_conv conv = {pamconv, (void*)passwd};

    pamret = pam_start(FAX_SERVICE, user, &conv, &pamh);
    if (pamret != PAM_SUCCESS)
    {
	logNotice("pam_start failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
	return false;
    }
    pamret = pam_set_item(pamh, PAM_RHOST, remoteaddr);
    if (pamret == PAM_SUCCESS)
    {
    	pamret = do_pamauth(pamh);
	if (pamret == PAM_SUCCESS)
	    retval = true;
	else
	    logNotice("pam_authenticate failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));
    } else
	logNotice("pam_set_item (PAM_RHOST) failed in pamCheck with 0x%X: %s", pamret, pam_strerror(pamh, pamret));


    pamret = pam_end(pamh, pamret);

    if (pamret != PAM_SUCCESS)
	logNotice("pam_end failed with 0x%X: %s", pamret, pam_strerror(pamh, pamret));

    (void) seteuid(ouid);

    /*
     * And we need to reset our logging because pam clobbers us
     */
    HylaFAXServer::setupLogging();

    return retval;
}

#endif // HAVE_PAM

bool
HylaFAXServer::checkuserPAM(const char* name)
{
#ifdef HAVE_PAM
	if (IS(LOGGEDIN)) {
	    logNotice("PAM authentication for %s can't be used for a re-issuance of USER command because of chroot jail\n", name);
	    return false;
	}


	/*
	 * Here should go code to se if the user is valid for PAM
	 */
	return do_pamcheck(name, NULL, remoteaddr);
#endif

	return(false);
}

bool HylaFAXServer::checkpasswdPAM (const char* pass)
{
#ifdef HAVE_PAM
    /*
     * Here should go code to see if the password is ok for the PAM user
     */
    return do_pamcheck(the_user, pass, remoteaddr);
#endif

    return false;
}


