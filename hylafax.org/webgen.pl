#!/usr/bin/perl -w

use strict;

#open(CONFIG,"|../configure --quiet");
#close CONFIG;a
`../configure
	--with-DIR_BIN=/usr/bin
	--with-DIR_SBIN=/usr/sbin
	--with-DIR_LIBEXEC=/usr/sbin
	--with-DIR_LIBDATA=/usr/share/fax
	--with-DIR_LOCKS=/var/lock
	--with-TIFFINC=/usr/include
	--with-TIFFBIN=/usr/bin
	--with-DIR_MAN=/usr/man
	--with-PATH_GSRIP=/usr/bin/gs
	--with-DEFVRES=196
	--with-DBLIBINC=/usr/include
	--with-DIR_AFM=/usr/share/fax/afm
	--with-LIBTIFF=-ltiff
	--with-DIR_HTML=/tmp
	--with-DIR_CGI=/usr/doc
	--with-DIR_SPOOL=/var/spool/fax
	--with-LIBDB=-ldb
	--with-ZLIB=no
	--with-ZLIBINC=.
	--with-AFM=no
	--with-DSO=LINUX
	--with-DSOSUF=so
	--with-PATH_VGETTY=/sbin/vgetty
	--with-HTML=yes`
