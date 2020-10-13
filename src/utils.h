#ifndef __UTILS_H__
#define __UTILS_H__

#include <tchar.h>
#include "sqlite3.h"

namespace utils {
	TCHAR* trim(TCHAR *in);
	TCHAR* maskQuotes(TCHAR* in);

	TCHAR* replace(const TCHAR* in, const TCHAR* oldStr, const TCHAR* newStr, int start = 0);
	TCHAR* replaceAll(const TCHAR* in, const TCHAR* oldStr, const TCHAR* newStr, int start = 0);
	TCHAR* replaceVars (const TCHAR* in, const TCHAR* varname, const TCHAR* value);

	char* replace(const char* in, const char* oldStr, const char* newStr, int start = 0);
	char* replaceAll(const char* in, const char* oldStr, const char* newStr, int start = 0);
	char* replaceVars (const char* in, const char* varname, const char* value);
	int removeUnusedVars (char* inout);

	TCHAR* utf8to16(const char* in);
	char* utf16to8(const TCHAR* in);

	void setClipboardText(const TCHAR* text);
	int openFile(TCHAR* path, const TCHAR* filter);
	bool isFileExists(const TCHAR* path);
	char* getFileName(const char* path);

	void drawArrow(HDC hDC, POINT a, POINT b);
	void drawBezierArrow(HDC hDC, POINT points[]);
}
#endif
