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
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "config.h"
#include "port.h"

int
main(int argc, char** argv)
{
    FILE* hf = NULL;
    int c;
    int fd;
    int i;
    int len1, len2;
    int skip;
    char* hostfile = FAX_SPOOLDIR "/" FAX_PERMFILE;
    char buff[256];
    char newhostfile[256];
    const char* usage = "faxdeluser [ -f hosts-file] username";
    struct passwd* pw;
    
    while ((c = getopt(argc, argv, "f:?:")) != -1) {
        switch (c) {
        case 'f':
            hostfile = optarg;
            break;
        case '?':
        default:
            printf("Usage: %s\n", usage);
            break;
        }
    }
    if ((hf = fopen(hostfile, "r+")) == NULL) {
        sprintf(buff, "Error - cannot open file: %s", hostfile);
        perror(buff);
        return 0;
    }
    sprintf(newhostfile, "%s.%i", hostfile, (int)getpid());
    fd = open(newhostfile, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        sprintf(buff, "Error cannot open file %s", newhostfile);
        perror(buff);
        return 0;
    }
    while (fgets(buff, sizeof(buff), hf)) {
        skip = 0;
        for (i = optind; i < argc; i++) {
            len1 = strcspn(buff, ":\n");
            len2 = strlen(argv[i]);
            if (len1 == len2 && !strncmp(buff, argv[i], len1)) {
                skip = 1;
                break;
            } 
        }
        if (!skip) {
            if (write(fd, buff, strlen(buff)) == -1) {
                sprintf(buff, "Error writing to file %s", newhostfile);
                perror(buff);
                return 0;
            }
        }
    }
    fclose(hf);
    close(fd);
    if (rename(newhostfile, hostfile)) {
        perror("Error writing hosts file");
    }
    pw = getpwnam(FAX_USER);
    if (pw == NULL || chown(hostfile, pw->pw_uid, pw->pw_uid)) {
        perror("Error writing hosts file");
    }
    return 0;
}
