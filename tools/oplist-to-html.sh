#!/bin/sh
#
# $kbyanc: dyntrace/tools/oplist-to-html.sh,v 1.3 2005/03/02 04:21:17 kbyanc Exp $

if [ $# -ne 1 ]; then
	echo "Usage: $0 <oplist-list.xml>"
	exit 1
fi

xsltproc --novalid --nonet - $1 << EOF

<!--
	XSLT stylesheet for producing a simple HTML document from a
	opcode list XML file.
 -->

<xsl:stylesheet version="1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns="http://www.w3.org/TR/xhtml1/strict">

<xsl:output
	method="html"
	indent="yes"
	encoding="iso-8859-1"
/>

<xsl:template match="oplist">
<html>
    <h1>Instruction Prefixes:</h1>
    <table border="1">
    <tr> 
	<td>Description</td>
	<td>Encoding</td>
    </tr>
    <xsl:apply-templates select="prefix"/>
    </table>

    <br />

    <h1>Opcodes:</h1>
    <table border="1">
    <tr>
	<td>Mnemonic</td>
	<td>Description</td>
	<td>Detail</td>
	<td>Encoding</td>
    </tr>
    <xsl:apply-templates select="op"/>
    </table>
</html>
</xsl:template>

<xsl:template match="oplist/prefix">
    <tr>
	<td><xsl:value-of select="@detail"/></td>
	<td><xsl:value-of select="@bitmask"/></td>
    </tr>
</xsl:template>

<xsl:template match="oplist/op">
    <tr>
	<td>
	    <xsl:value-of select="@mnemonic"/>
	    <xsl:if test="@conditional">
		(<xsl:value-of select="@conditional"/>)
	    </xsl:if>
	</td>
	<td><xsl:apply-templates select="description"/></td>
	<td><xsl:value-of select="@detail"/></td>
	<td><xsl:value-of select="@bitmask"/></td>
    </tr>
</xsl:template>

</xsl:stylesheet>

EOF
