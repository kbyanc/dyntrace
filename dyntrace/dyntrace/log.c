/*
 * Copyright (c) 2004 Kelly Yancey
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $kbyanc: dyntrace/dyntrace/log.c,v 1.2 2004/12/19 10:59:02 kbyanc Exp $
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dynprof.h"


#define	WARN_BUFFER_SIZE	128


static void	 expand_error(const char *src, char *dest, size_t destlen,
			      int errnum);
static void	 warnv(const char *fmt, va_list ap);


/*!
 * expand_error() - Perform syslog(3)-like expansion of \%m to error message.
 *
 *	Copies all text from \a src to \a dest, replacing any instances of
 *	\%m in the source text with the error message returned by strerror(3)
 *	for \a errnum.  Guarantees that no more than \a destlen bytes are
 *	written to the \a dest buffer.  Destination string is always
 *	nul-terminated on return, even if it was truncated.
 *
 *	@param	src		Source text to copy.
 *
 *	@param	dest		Destination buffer to write expanded text to.
 *
 *	@param	destlen		Size of destination buffer.
 *
 *	@param	errnum		Error number to use to lookup error message
 *				to replace \%m with.
 */
void
expand_error(const char *src, char *dest, size_t destlen, int errnum)
{
	const char *errstr;
	const char *m;
	size_t len;

	assert(destlen > 0);
	destlen--;		/* Ensure room to nul-terminate string. */

	while (*src != '\0' && destlen > 0) {

		m = strstr(src, "%m");
		if (m == NULL) {
			strncpy(dest, src, destlen);
			return;
		}

		/*
		 * Append text preceeding the '%m' marker.
		 */
		len = m - src;
		if (len > destlen)
			len = destlen;
		memcpy(dest, src, len);
		destlen -= len;
		dest += len;

		/*
		 * Lookup the error message to replace the '%m' with.
		 */
		errstr = strerror(errnum);
		if (errstr == NULL)
			errstr = "unknown error";

		/*
		 * Append error message text.
		 */
		len = strlen(errstr);
		if (len > destlen)
			len = destlen;
		memcpy(dest, errstr, len);
		destlen -= len;
		dest += len;

		src = m + 2;
	}

	*dest = '\0';
}


/*!
 * warnx() - Write warning to stderr with %m expanded to error message.
 *
 *	@param	fmt		printf(3)-style format specifier indicating
 *				how to format the output.
 *
 *	@param	ap		stdarg(3) variable-length arguments.
 */
void
warnv(const char *fmt, va_list ap)
{
	static char fmtbuf[WARN_BUFFER_SIZE];
	int saved_errno;
	const char *nl;
	const char *m;

	saved_errno = errno;

	assert(fmt != NULL);

	m = strstr(fmt, "%m");
	if (m != NULL) {
		expand_error(fmt, fmtbuf, sizeof(fmtbuf), saved_errno);
		fmt = fmtbuf;
	}

	vfprintf(stderr, fmt, ap);

	/*
	 * Append a trailing newline if one is not supplied.
	 */
	nl = strrchr(fmt, '\n');
	if (nl == NULL || nl[1] != '\0')
		fputc('\n', stderr);
}


/*!
 * warn() - Display warning.
 *
 *	Writes a formatted error message to the standard error output and
 *	returns.
 */
void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	warnv(fmt, ap);
	va_end(ap);
}


/*!
 * fatal() - Report a fatal error and exit.
 *
 *	Writes a formatted error message to the standard error output before
 *	exiting with the given exit code.
 */
void
fatal(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	warnv(fmt, ap);
	va_end(ap);

	/* When debugging, it is more useful to get a core dump. */
	assert(0);

	exit(eval);
}
