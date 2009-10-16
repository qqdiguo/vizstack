#
# Makefile for use in Shree's VM
# This has Ubuntu 8.10 running, a locally installed JRE
#
# Asciidoc is configured to use docbook as the default backend in Ubuntu
# So forcing FOP is a little bit of a hack.
#
ALL: fop
	@echo "Defaulting to FOP. You may choose fop/docbook/html"

fop:
	env JAVA_HOME=/home/shree/jre1.6.0_16/ PATH=/home/shree/jre1.6.0_16/bin:${PATH} a2x  --icons -d book -f pdf --fop "" manual.txt

docbook:
	a2x  --icons -d book -f pdf manual.txt

html:
	a2x  --icons -d book -f xhtml manual.txt

