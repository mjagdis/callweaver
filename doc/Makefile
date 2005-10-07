# Copyright 2005, In Words - Technical Documentation Services and Solutions
#
# Author: Sean Wheller   sean.at.inwords.co.za   http://www.inwords.co.za
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTI
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# This program is free software; you can redistribute it and/or modify 
# it under the terms of the GNU General Public License as published by 
# the Free Software Foundation; either version 2 of the License, or 
# (at your  option) any later version.  This program is distributed in 
# the hope that it will be useful, but WITHOUT ANY WARRANTY; without 
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
# PARTICULAR PURPOSE.
#
# See the GNU General Public License for more details.
# http://www.gnu.org/copyleft/gpl.html
#
##############
# 
##############

MAKECMD=make

ifndef "HTMLXSL"
HTMLXSL=libs/html-cust.xsl
endif

ifndef "HTMLCHUNKXSL"
HTMLCHUNKXSL=libs/html-chunk-cust.xsl
endif

ifndef "FOXSL"
FOXSL=libs/fo-cust.xsl
endif

ifndef TAG
SVN_URL=svn://svn.openpbx.org/openpbx/trunk/doc
endif

DATE=`date +%Y%m%d`

books:	prepguide instguide commguide operguide provguide faqguide

# Builds OpenPBX.org Preparation Guide in build/prep/
prepguide: imagemv
	@echo "Starting OpenPBX.org Preparation Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/prep/prep-all.html prep/prep-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet prep/prep-guide.xml $(HTMLCHUNKXSL) base.dir=build/prep/
	
	java com.icl.saxon.StyleSheet -o build/prep/prep-guide.fo prep/prep-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/prep/
	
	java org.apache.fop.apps.Fop -fo build/prep/prep-guide.fo -pdf build/prep/prep-guide.pdf
	
	rm build/prep/prep-guide.fo

# Builds OpenPBX.org Installation Guide in build/inst/
instguide: imagemv
	@echo "Starting OpenPBX.org Installation Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/inst/inst-all.html inst/inst-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet inst/inst-guide.xml $(HTMLCHUNKXSL) base.dir=build/inst/
	
	java com.icl.saxon.StyleSheet -o build/inst/inst-guide.fo inst/inst-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/inst/
	
	java org.apache.fop.apps.Fop -fo build/inst/inst-guide.fo -pdf build/inst/inst-guide.pdf
	
	rm build/inst/inst-guide.fo

# Builds OpenPBX.org Comissioning Guide in build/comm/
commguide: imagemv
	@echo "Starting OpenPBX.org Comissioning Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/comm/comm-all.html comm/comm-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet comm/comm-guide.xml $(HTMLCHUNKXSL) base.dir=build/comm/
	
	java com.icl.saxon.StyleSheet -o build/comm/comm-guide.fo comm/comm-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/comm/
	
	java org.apache.fop.apps.Fop -fo build/comm/comm-guide.fo -pdf build/comm/comm-guide.pdf
	
	rm build/comm/comm-guide.fo

# Builds OpenPBX.org Operation Guide in build/oper/
operguide: imagemv
	@echo "Starting OpenPBX.org Operation Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/oper/oper-all.html oper/oper-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet oper/oper-guide.xml $(HTMLCHUNKXSL) base.dir=build/oper/
	
	java com.icl.saxon.StyleSheet -o build/oper/oper-guide.fo oper/oper-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/oper/
	
	java org.apache.fop.apps.Fop -fo build/oper/oper-guide.fo -pdf build/oper/oper-guide.pdf
	
	rm build/oper/oper-guide.fo
	
# Builds OpenPBX.org Provisioning Guide in build/prov/
provguide: imagemv
	@echo "Starting OpenPBX.org Provisioning Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/prov/prov-all.html prov/prov-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet prov/prov-guide.xml $(HTMLCHUNKXSL) base.dir=build/prov/
	
	java com.icl.saxon.StyleSheet -o build/prov/prov-guide.fo prov/prov-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/prov/
	
	java org.apache.fop.apps.Fop -fo build/prov/prov-guide.fo -pdf build/prov/prov-guide.pdf
	
	rm build/prov/prov-guide.fo
	
# Builds OpenPBX.org FAQ Guide in build/faq/
faqguide: imagemv
	@echo "Starting OpenPBX.org FAQ Guide  . . ."
	java com.icl.saxon.StyleSheet -o build/faq/faq-all.html faq/faq-guide.xml $(HTMLXSL)
	
	java com.icl.saxon.StyleSheet faq/faq-guide.xml $(HTMLCHUNKXSL) base.dir=build/faq/
	
	java com.icl.saxon.StyleSheet -o build/faq/faq-guide.fo faq/faq-guide.xml $(FOXSL)
	
	cp libs/corpstyle.css build/faq/
	
	java org.apache.fop.apps.Fop -fo build/faq/faq-guide.fo -pdf build/faq/faq-guide.pdf
	
	rm build/faq/faq-guide.fo

# Moves images shared by all docs into build/images
imagemv:
	mkdir -p build/images
	cp images/*.* build/images
	
	mkdir -p build/images/admon
	cp images/admon/*.* build/images/admon
	
	mkdir -p build/images/callouts
	cp images/callouts/*.* build/images/callouts
	
	mkdir -p build/images/navig
	cp images/navig/*.* build/images/navig

# Deletes the build/ directory
clean:
	rm -r build



