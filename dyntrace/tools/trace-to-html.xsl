<!--
	XSLT stylesheet for producing a simple HTML document from a
	dynprof results file.

	e.g.: xsltproc -o my-prog.html dynprof-to-html.xsl my-prog.dynprof

	$kbyanc: dyntrace/tools/trace-to-html.xsl,v 1.1 2004/12/01 03:32:18 kbyanc Exp $
 -->

<xsl:stylesheet version="1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
	xmlns="http://www.w3.org/TR/xhtml1/strict">

<xsl:output
	method="html"
	indent="yes"
	encoding="iso-8859-1"
/>

<xsl:template match="dynprof">
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

    <h1>Opcodes:</h1>
    <table border="1">
    <tr>
	<td>Mneumonic</td>
	<td>Prefixes</td>
	<td>N</td>
	<td>Description</td>
	<td>Encoding</td>
    </tr>
    <xsl:apply-templates select="region/op"/>
    </table>
</html>
</xsl:template>


<xsl:template match="dynprof/region/op">
    <xsl:variable name="mneumonic" select="@mneumonic"/>
    <xsl:variable name="detail" select="@detail"/>
    <xsl:variable name="bitmask" select="@bitmask"/>
    <xsl:for-each select="count">
	<tr>
	    <td><xsl:value-of select="$mneumonic"/></td>
	    <td><xsl:value-of select="@prefixes"/></td>
	    <td><xsl:value-of select="@n"/></td>
	    <td><xsl:value-of select="$detail"/></td>
	    <td><xsl:value-of select="$bitmask"/></td>
	</tr>
    </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
