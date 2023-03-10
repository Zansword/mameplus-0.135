//============================================================
//
//  main.c - Win32 main program
//
//============================================================
//
//  Copyright Aaron Giles
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or
//  without modification, are permitted provided that the
//  following conditions are met:
//
//    * Redistributions of source code must retain the above
//      copyright notice, this list of conditions and the
//      following disclaimer.
//    * Redistributions in binary form must reproduce the
//      above copyright notice, this list of conditions and
//      the following disclaimer in the documentation and/or
//      other materials provided with the distribution.
//    * Neither the name 'MAME' nor the names of its
//      contributors may be used to endorse or promote
//      products derived from this software without specific
//      prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY AARON GILES ''AS IS'' AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
//  EVENT SHALL AARON GILES BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//  DAMAGE (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
//  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//============================================================

// standard windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

// MAMEOS headers
#include "strconv.h"

extern int utf8_main(int argc, char *argv[]);



//============================================================
//  main
//============================================================

// undo the command-line #define that maps main to utf8_main in all other cases
#ifndef MAMELIB
#if !defined(WINUI) || defined(MESS)
#undef main
#undef wmain
#endif
#endif

#ifdef __GNUC__
int main(int argc, char **a_argv)
#else // !__GNUC__
int _tmain(int argc, TCHAR **argv)
#endif // __GNUC__
{
	int i, rc;
	char **utf8_argv;

#ifdef __GNUC__
	TCHAR **argv;
#ifdef UNICODE
	// MinGW doesn't support wmain() directly, so we have to jump through some hoops
	extern void __wgetmainargs(int *argc, wchar_t ***wargv, wchar_t ***wenviron, int expand_wildcards, int *startupinfo);
	WCHAR **wenviron;
	int startupinfo;
	__wgetmainargs(&argc, &argv, &wenviron, 0, &startupinfo);
#else // !UNICODE
	argv = a_argv;
#endif // UNICODE
#endif // __GNUC__

#ifdef MALLOC_DEBUG
{
	extern int winalloc_in_main_code;
	winalloc_in_main_code = TRUE;
#endif

	/* convert arguments to UTF-8 */
	utf8_argv = (char **) malloc(argc * sizeof(*argv));
	if (utf8_argv == NULL)
		return 999;
	for (i = 0; i < argc; i++)
	{
		utf8_argv[i] = utf8_from_tstring(argv[i]);
		if (utf8_argv[i] == NULL)
			return 999;
	}

	/* run utf8_main */
	rc = utf8_main(argc, utf8_argv);

	/* free arguments */
	for (i = 0; i < argc; i++)
		free(utf8_argv[i]);
	free(utf8_argv);

#ifdef MALLOC_DEBUG
	{
		void check_unfreed_mem(void);
		check_unfreed_mem();
	}
	winalloc_in_main_code = FALSE;
}
#endif

	return rc;
}
