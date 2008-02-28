#
# We build the language .po/.mo files with all makes
#
# Unfortunately, only real makes (i.e. GNU make) no khow to rebuild
# the raw .po files properly, but that's not a big problem
hylafax.pot: version.po libhylafax/messages.pot hylafax-client/messages.pot hylafax-server/messages.pot
	cat version.po > $@.tmp
	${MSGCAT} libhylafax/messages.pot hylafax-client/messages.pot hylafax-server/messages.pot >> $@.tmp
	mv $@.tmp $@


# A bit more trikery here
# We want $(wildcard...) because we don't want to try to "rebuild" thes messages.pot
# unless the components exist.  If update-po was run, they should all exist
# But legacy makes (SCO) don't support $(wildcard...).  Luckly, they don't support $(...)
# At all, so they just drop it all.  Real makes support $(wildcard)
# That's why we use the $(...${...})
libhylafax/messages.pot: $(wildcard ${patsubst %, ${DEPTH}/%/messages.po, libhylafax})
	test -d libhylafax || mkdir libhylafax
	${MSGCAT} $^ > $@.tmp
	mv $@.tmp $@

hylafax-client/messages.pot: $(wildcard ${patsubst %, ${DEPTH}/%/messages.po, ${CLIENTS}})
	test -d hylafax-client || mkdir hylafax-client
	${MSGCAT} $^ > $@.tmp
	mv $@.tmp $@


hylafax-server/messages.pot: $(wildcard ${patsubst %, ${DEPTH}/%/messages.po, ${SERVERS}})
	test -d hylafax-server || mkdir hylafax-server
	${MSGCAT} $^ > $@.tmp
	mv $@.tmp $@

# We can't use any normal Make pattern rules while we're stuck supporting
# legacy comiples like SCO, so be carefull!
# Here again (like MANCVT) we can't use $<, because we're not an "inferred" rule
# So techincally, this rule's depencies aren't *quite* complete, but on well
${CATALOG}/${LANGUAGE}.po: ${CATALOG}/messages.pot
	test -d ${CATALOG} || mkdir ${CATALOG}
	touch $@.tmp
	${MSGMERGE} $@.tmp $? -o $@ --compendium=${SRCDIR}/${LANGUAGE}.po
	rm -f $@.tmp

${CATALOG}/${LANGUAGE}.mo: ${CATALOG}/${LANGUAGE}.po
	${MSGFMT} -o $@ $?

lang-mo: ${CATALOG}/${LANGUAGE}.mo

#and since we don't have pattern rules, we do this horrid loop
all-mo:
	@for l in ${LANGUAGES}; do					\
	    for c in libhylafax hylafax-client hylafax-server; do	\
		${MAKE} LANGUAGE=$$l CATALOG=$$c lang-mo || exit $?;	\
	    done;							\
	done
