<!--
	XSLT stylesheet for producing a simple HTML document from a
	dyntrace results file.

	e.g.: xsltproc -o my-prog.html trace-to-html.xsl my-prog.trace

	$kbyanc: dyntrace/tools/trace-to-html.xsl,v 1.3 2004/12/27 10:33:51 kbyanc Exp $
 -->

<xsl:stylesheet version="1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns="http://www.w3.org/TR/xhtml1/strict">

<xsl:output
	method="html"
	indent="yes"
	encoding="iso-8859-1"
/>

<xsl:template match="dyntrace">
<html>
    <h1>Instruction Prefixes:</h1>
    <table border="1">
    <tr> 
	<td>Id</td>
	<td>Description</td>
	<td>Encoding</td>
    </tr>
    <xsl:for-each select="prefix">
	<tr>
	    <td><xsl:value-of select="@id"/></td>
	    <td><xsl:value-of select="@detail"/></td>
	    <td><xsl:value-of select="@bitmask"/></td>
	</tr>
    </xsl:for-each>
    </table>

    <br />

    <xsl:for-each select="program">
	<h1><xsl:value-of select="@name"/></h1>
	<xsl:apply-templates select="region"/>
    </xsl:for-each>

</html>
</xsl:template>


<xsl:template match="dyntrace/program/region">
    <h2>Region: <xsl:value-of select="@type"/></h2>
    <table border="1">
    <tr>
	<td>Mnemonic</td>
	<td>Prefixes</td>
	<td align="right">N</td>
	<td>Description</td>
	<td>Encoding</td>
    </tr>

    <xsl:for-each select="opcount">
	<tr>
	    <td><xsl:value-of select="@mnemonic"/></td>
	    <td><xsl:value-of select="@prefixes"/></td>
	    <td align="right"><xsl:value-of select="@n"/></td>
	    <td><xsl:value-of select="@detail"/></td>
	    <td><xsl:value-of select="@bitmask"/></td>
	</tr>
    </xsl:for-each>

    </table>
</xsl:template>

</xsl:stylesheet>
