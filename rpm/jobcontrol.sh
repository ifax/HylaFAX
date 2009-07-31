#!/bin/sh

##  More sample jobcontrol programs can be found at 
##   http://www.hylafax.org/content/JobControl

##
##  This is a sample jobcontrol program for HylaFAX that can be used
##   to Virtualize a fax service for various "client" companies in a
##   single HylaFAX installation
##
##  This virtualization does a few things:
##    1) Verifies client
##    2) Rejects jobs that the client submitted that we consider
##       Malicious
##    3) Set's Modem based on destination and client settings
##    4) Set's faxsend options to make the job appear from the client's own
##       FAX service
##
##  This assumes that by default, faxsend is configured not allow TSI
##   modification, and is branded with our fax service branding/numbers/etc.
##
##  To get full milage from this type of JobControl, you should have the
##   following settings in $SPOOL/etc/config for FaxQueuer:
##	MaxBatchJobs: 1
##	JobControlCmd: bin/jcontrol.sh
##	JobControlWait: false
##

JOBID=$1

## GetClient <owner> <email>
##  - returns a string - the client identifier

## Result of "REJECT" is a non-authorized submitter, and
##  should be rejected.
## Result of "" means it's not a virtualized client and 
##  should be subject to our default modem settings
GetClient ()
{
    # Pretend to lookup the user
    case "$1" in
#	aidan)
#	    echo "1234"
#	    ;;
#	root)
#	    echo "REJECT"
#	    ;;
	*)
	    # All others are not full virtual clients
	    echo ""
    esac
}

##
## GetJobParam <param>
## - returns the value of the job param
GetJobParam ()
{
	grep "^$1:" sendq/q$JOBID | cut -d : -f 2-
}

##
## SetControlParam <tag> <value>
SetControlParam ()
{
    echo "$1: \"$2\""
}

OWNER=$(GetJobParam owner)
EMAIL=$(GetJobParam mailaddr)


CLIENT=$(GetClient "$OWNER" "$EMAIL")

if [ "$CLIENT" == "REJECT" ]
then
    SetControlParam RejectNotice "Not authorized"
    exit
fi

# Only client 1234 is allowed to specify ModemGroups
#if [ "$CLIENT" != 1234 ]
#then
#    MODEM=$(GetJobParam modem)
#    if [ "$MODEM" != "any" ]
#    then
#	SetControlParam RejectNoctice "Modem setting of $MODEM not allowed - use any"
#	exit
#    fi
#fi

# Set the modemgroup based on the destination
# We're strict on the given number format - it must be a cannonical
# number (including the +)
DEST=$(GetJobParam number)
#case "$DEST" in
#    82??) 				## Our local extensions
#    	SetControlParam Modem "PBX"
#	;;
#    +1215*)
#    	SetControlParam Modem "Local"
#    	;;
#    +1*)
#    	SetControlParam Modem "NA"
#    	;;
#    +44*)
#    	SetControlParam Modem "UK"
#    	;;
#    *)
#    	SetControlParam RejectNotice "Not a number we support - see numbering FAQ"
#    	;;
#esac


##
## And now client-specific settings

#case "$CLIENT" in
#    1234)
#    	# These guys can do whatever they want - they pay big bucks for the
#	#  privilege
#	SetControlParam UseJOBTSI true
#	SetControlParam UseJobTagLine true
#	;;
#    5678)
#    	# These guys aren't nice
#	SetControlParam LocalIdentifier "Company XYX"
#	SetControlParam DesiredBR 14400
#	SetControlParam TagLineFormat "From %%n|%c|Page %%P of %%T"
#	SetControlParam TimeOfDay "000000-065959,190000-235900"
#	;;
#    *)
#    	# And our regular guys
#	SetControlParam DesiredBR 9600
#esac

