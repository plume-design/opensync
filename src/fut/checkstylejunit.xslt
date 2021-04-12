<?xml version="1.0" encoding="UTF-8"?>
    <xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
    <xsl:output encoding="UTF-8" method="xml"></xsl:output>

    <xsl:template match="/">
        <testsuite>
            <!-- Number of files equals number of tests -->
            <xsl:attribute name="tests">
                <xsl:value-of select="count(.//file)" />
            </xsl:attribute>
            <!-- Convert error elements to filure elements -->
            <xsl:attribute name="failures">
                <xsl:value-of select="count(.//error)" />
            </xsl:attribute>
            <!-- Remove checkstyle element -->
            <xsl:for-each select="//checkstyle">
                <xsl:apply-templates />
            </xsl:for-each>
        </testsuite>
    </xsl:template>

    <!-- Transform file elements to testcase elements -->
    <xsl:template match="file">
        <testcase>
            <!-- Transform name attribute to both name and classname attributes -->
            <xsl:attribute name="name">
                <xsl:value-of select="@name" />
            </xsl:attribute>
            <xsl:attribute name="classname">
                <xsl:value-of select="@name" />
            </xsl:attribute>
            <xsl:apply-templates select="node()" />
        </testcase>
    </xsl:template>

    <!-- Transform error elements to failure elements -->
    <xsl:template match="error">
        <failure>
            <!-- Transform source attribute to type attribute -->
            <xsl:attribute name="type">
                <xsl:value-of select="@source" />
            </xsl:attribute>
            <!-- Combine line, column, message attributes into "failure description" text -->
            <xsl:text>Line </xsl:text>
                <xsl:value-of select="@line" />
            <xsl:text>, Col </xsl:text>
                <xsl:value-of select="@column" />
            <xsl:text>: </xsl:text>
                <xsl:value-of select="@message" />
            <xsl:text> https://www.shellcheck.net/wiki/</xsl:text>
                <xsl:value-of select="substring(@source, '12')" />
        </failure>
    </xsl:template>
</xsl:stylesheet>
