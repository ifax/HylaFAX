/*	$Id$ */
/*
 * Copyright (c) 1999 Robert Colquhoun
 *
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
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include "config.h"
#include "port.h"

#if HAS_CRYPT_H
#include <crypt.h>
#endif

#ifndef FAX_DEFAULT_UID
#define FAX_DEFAULT_UID 60002
#endif

extern int optind;
extern char* optarg;

const char passwd_salts[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

const char* usage = "faxadduser [-a admin-password] [-f hosts-file] \
[-h host-name] [-p password] [-u uid] username";

int
main(int argc, char** argv)
{
    char buff[256];
    char salt_buff[2];
    FILE* hf = NULL;
    int c;
    int salt;
    char* hostfile = FAX_SPOOLDIR "/" FAX_PERMFILE;
    char* password = NULL;
    char* adminword = NULL;
    char* hostname = NULL;
    int uid = FAX_DEFAULT_UID;
    
    while ((c = getopt(argc, argv, "a:f:h:p:u:")) != -1) {
        switch (c) {
        case 'a':
            adminword = optarg;
            break;
        case 'f':
            hostfile = optarg;
            break;
        case 'h':
            hostname = optarg;
            break;
        case 'p':
            password = optarg;
            break;
        case 'u':
            uid = atoi(optarg);
            break;
        case '?':
        default:
            printf("Usage: %s\n", usage);
            break;
        }
    }
    hf = fopen(hostfile, "a+");
    if (hf == NULL) {
        snprintf(buff, sizeof(buff), "Error - cannot open hosts file: %s", hostfile);
        perror(buff);
        return 0;
    }
    srand(time(NULL));
    while (optind < argc) {
        fprintf(hf, "^%s@", argv[optind++]);
	if (hostname != NULL) fprintf(hf, "%s$", hostname);
        if (uid != FAX_DEFAULT_UID) {
            fprintf(hf, ":%i", uid);
        } else if (password != NULL || adminword != NULL) {
            fprintf(hf, ":");
        }
        if (password != NULL) {
            salt = (int)(4096.0 * rand() / (RAND_MAX + 1.0));
            salt_buff[0] = passwd_salts[salt / 64];
            salt_buff[1] = passwd_salts[salt % 64];
            fprintf(hf, ":%s", crypt(password, salt_buff));
        } else if (adminword != NULL) {
            fprintf(hf, ":");
        }
        if (adminword != NULL) {
            salt = (int)(4096.0 * rand() / (RAND_MAX + 1.0));
            salt_buff[0] = passwd_salts[salt / 64];
            salt_buff[1] = passwd_salts[salt % 64];
            fprintf(hf, ":%s", crypt(adminword, salt_buff));
        }
        fprintf(hf, "\n");
    }
    fclose(hf);
    return 0;
}
