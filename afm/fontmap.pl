#!perl
# 
# $Id$
#
# This script was provided by:
#     From: Evan Leibovitch <evan@telly.org>
#     Date: Fri, 3 Oct 1997 08:10:57 -0400 (EDT)
#
# with the following comment about newer versions of Ghostscript
# and the use of fonts for textfmt(1):
#
# It's a newer version of ghostscript, which maps font names to filenames.
# The key is in the file /usr/lib/ghostscript/fonts/Fontmap, which describes
# the relationships. And the problem with simply downloading afm files is
# that they may not exactly match the fonts you have.
# 
# Here's a small perl script that I write which automated the creation of
# the links necessary to work with textfmt -- I've uploaded it to the
# HylaFAX ftp site in case there's a desire to put it in the next
# distribution:
# 
# - Evan

chdir "/usr/lib/ghostscript/fonts";

open(FONTLIST,"Fontmap") || die "Can not open Fontmap: $!\n";
while (<FONTLIST>)
	{
	chop;
	@f=split(/\s+/);
	next unless substr(@f[0],0,1) eq "/";
	@l=split(/\./,@f[1]);
	chop(@l[1]);
	$fname = substr(@l[0],1,40) . ".afm";
	$font = substr(@f[0],1,40) . ".afm";
	if (( @l[1] eq "pfb") || (substr(@l[0],0,1) eq "/"))
		{
		if ( -r $fname)
			{
			system ("ln","-s",$fname,$font);
			print "Linking " . $fname . " to " . $font . "\n";
			}
		else
			{
			print "Skipping " . $fname . ": no such file\n";
			}
		}
	}
