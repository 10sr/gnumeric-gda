#
# Some common rules for building gnumeric docs.
# These will be changed as the documentation format changes
# but it is a start.
#
# Requires that the calling makefile define 'lang'

docname = gnumeric
if WITH_GNOME
  omffile = gnumeric-$(lang).omf
endif
gnumeric_docdir  = $(top_srcdir)/doc
entities += functions.xml

functions_xml_parts = func.defs func-header.xml func-footer.xml

functions.xml: $(gnumeric_docdir)/make-func-list.pl $(functions_xml_parts)
	(cat $(srcdir)/func-header.xml ;				\
	 $(PERL) $(gnumeric_docdir)/make-func-list.pl func.defs ;	\
	 cat $(srcdir)/func-footer.xml					\
	) >functions.tmp ;						\
	if xmllint --format --encode "UTF-8" functions.tmp >functions.out ; then	\
	    mv functions.out $@; rm functions.tmp;					\
	fi

MOSTLYCLEANFILES = functions.out functions.tmp

func.defs: $(top_builddir)/src/gnumeric$(EXEEXT)
	LC_ALL="$(locale)" ; export LC_ALL ; $(top_builddir)/src/gnumeric --dump-func-defs="$@"

include $(top_srcdir)/xmldocs.make

# Include generated files to simplify installation.
# (Entities, including functions.xml, are shipped via xmldocs.make.)
EXTRA_DIST += $(functions_xml_parts)

.PHONY : html
html :
	-mkdir -p html
	xsltproc -o html/index.html			\
	    --param db.chunk.chunk_top 0 		\
	    --param db.chunk.max_depth 3 		\
	    --stringparam db.chunk.basename gnumeric 	\
	    --stringparam db2html.admon.graphics_path stylesheet/ \
	    $(datadir)/xml/gnome/xslt/docbook/html/db2html.xsl	\
	    $(srcdir)/gnumeric.xml
