#! /bin/sh
# custom faxsend that will combine any .cover with the .ps file of the job.
#
#  At this point in the process, the ps files have already been prepared by faxq into the 
#  required tiff files for transmission.  So, all we need to do here is to combined the 
#  cover.ps file with the document.ps file into a single ps file (the document ps file).
#  We have to combine them before we acutally call the real faxsend.
#  The cover.ps will be removed later by the real faxsend.
#  Whatever remains in the document.ps file will get returned by the notify script.
#  
#  There is no need to modify the qfile or to delete any other ps files ourselves here.
#

#  LIMITATIONS:
#  we are going to have to do some more work if there is anyting but .ps files being submitted.
#  or if there are more than one .cover or 1 .ps file submitted per job.
#  if there is more than one qfile at the end of the arg list, the others will not get 
#  processed correctly by this script

NUMARGS=$#
QFILE=${!NUMARGS}

# Is there a cover listed in the qfile ?
QFILECOVERLINE="`cat "$QFILE" | grep "^!postscript.*.cover"`"

if [ $? -eq 1 ]; then
    # no cover for this fax
    # do nothing. here and just call c2faxsend with original args
    :
else
    # YES there is a cover file
    # What's the name of the fax doc listed in the qfile?
    QFILEDOCLINE="`cat "$QFILE" | grep "^!postscript.*.ps"`"

    # the q file entries look like the following so we need to do some cuts
    #   !postscript:0::docq/doc11552.cover
    #   !postscript:0::docq/doc13525.ps.11552
    PSDOCFILE="`echo "$QFILEDOCLINE" | cut -d":" -f4`"
    PSCOVERFILE="`echo "$QFILECOVERLINE" | cut -d":" -f4`"

    # make a uniq tmp file name.  use kill time.  $$ won't do it if this has multi desinations
    KILLTIME="`cat $QFILE | grep "killtime:" | cut -d":" -f2`"
    TMPFILE="tmp/${KILLTIME}.coverplusdoc"
    #  now combine the cover and the doc file into one file and then use the combined file
    cp "$PSCOVERFILE" "$TMPFILE"
    cat "$PSDOCFILE" >> "$TMPFILE"
    cat "$TMPFILE" > "$PSDOCFILE"  # do a cat vs mv incase files are ever linked
    rm "$TMPFILE"
fi

# Call the original faxsend command with whatever args this sript was called with.
/usr/sbin/faxsend $*

# Exit, and don't forget the status
exit $?
