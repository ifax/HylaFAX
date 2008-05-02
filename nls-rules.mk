# Common rules for Native Language Support (NLS)
# #
# # If some subdirectory of the source tree wants to provide NLS, it
# # needs to define the following make variable assignments:
# #
# # SOURCES               -- list of source files that contain message strings
# # GETTEXT_TRIGGERS      -- (optional) list of functions that contain
# #                          translatable strings
# #
# # That's all.  This will allow a "message.po" to be build there, and
# # the infrastructue in po/Makefile will use this to update everything

nls-SHOUT:
	@echo "NLS Settings:"
	@echo "PO: '${PO_FILES}'"
	@echo "MO: '${MO_FILES}'"
	@echo "FILES: ${SOURCES}"

OLDmessages.po: ${SOURCES}
	${XGETTEXT} -D ${DEPTH}/${SUBDIR} -D ${SRCDIR} -n ${addprefix -k, _ N_ ${GETTEXT_TRIGGERS}} ${SOURCES}

messages.po: ${SOURCES}
	${XGETTEXT} -D ${DEPTH} -D ${DEPTH}/${TOPSRCDIR} -n ${addprefix -k, _ N_ ${GETTEXT_TRIGGERS}} ${patsubst %, ${SUBDIR}/%, ${SOURCES}}
