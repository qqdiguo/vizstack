#
# Makefile for use in Shree's VM
# This has Ubuntu 8.10 running, a locally installed JRE
#
# Asciidoc is configured to use docbook as the default backend in Ubuntu
# So forcing FOP is a little bit of a hack.
#
#
# If you get failures w.r.t finding image files, 
# then ensure that the images needed for the document are available
#
# E.g., on Ubuntu I use :
#   ln -s /usr/share/asciidoc/images/ image
# and then
#   make
 
ALL: fop
	@echo "Built FOP by default. You may choose fop/docbook/html"

fop:
	env JAVA_HOME=/home/shree/jre1.6.0_16/ PATH=/home/shree/jre1.6.0_16/bin:${PATH} a2x  --icons -d book -f pdf --fop "" manual.txt

docbook:
	a2x  --icons -d book -f pdf manual.txt

html:
	a2x  --icons -d book -f xhtml manual.txt

