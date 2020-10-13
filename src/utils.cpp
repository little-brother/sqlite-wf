#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "utils.h"

namespace utils {
	TCHAR* trim(TCHAR *in) {
		auto isBlank = [](TCHAR ch) -> bool {
			return (ch == TEXT(' ')) || (ch == TEXT('\r')) || (ch == TEXT('\n'));
		};

		int start = 0;
		int end = !in ? 0 : _tcslen(in);

		while(start < end && isBlank(in[start]))
			start++;

		while(end > start && isBlank(in[end - 1]))
			end--;

		TCHAR* out = (TCHAR*)calloc (end - start + 1, sizeof (TCHAR));
		for (int i = 0; i < end - start; i++)
			out[i] = in[start + i];
		out[end - start] = TEXT('\0');

		return out;
	}

	int countChar(TCHAR* str, TCHAR chr) {
		int count = 0;
		for(; *str; count += (*str++ == chr));
			return count;
	}

	TCHAR* maskQuotes(TCHAR* in) {
		int qCount = countChar(in, TEXT('"'));
		int len = _tcslen(in);
		TCHAR* res = (TCHAR*)calloc (len + qCount + 1, sizeof (TCHAR));

		int j = 0;
		for (int i = 0; i < len; i++) {
			if (in[i] == TEXT('"')) {
				res[i + j] = TEXT('\\');
				j++;
			}
			res[i + j] = in[i];
		}

		res[len + qCount] = '\0';
		return res;
	}

	TCHAR* replace (const TCHAR* in, const TCHAR* oldStr, const TCHAR* newStr, int start, bool isAll, bool isExact) {
		int len = _tcslen(in);
		int nLen = _tcslen(newStr);
		int oLen = _tcslen(oldStr);

		if (start > len || len == 0)
			return (TCHAR*)calloc (1, sizeof (TCHAR));

		TCHAR* res = (TCHAR*)calloc (nLen <= oLen ? len + 1 : len * (nLen - oLen + 1), sizeof (TCHAR));
		TCHAR* p = (TCHAR*)in + start;
		TCHAR* p2 = p;

		_tcsncat(res, in, start);

		while((p = _tcsstr(p, oldStr))) {
			_tcsncat(res, p2, p - p2);
			TCHAR next = !isExact ? 0 : (p + oLen)[0];
			if (!isExact || (isExact && !_istalpha(next) && !_istdigit(next) && next != TEXT('_') && (next != TEXT('.'))))
				_tcsncat(res, newStr, nLen);
			else
				_tcsncat(res, oldStr, oLen);
			p = p + oLen;
			p2 = p;

			if (!isAll)
				break;
		}

		_tcsncat(res, p2, len - (p2 - in));
		return res;
	}

	TCHAR* replace (const TCHAR* in, const TCHAR* oldStr, const TCHAR* newStr, int start) {
		return replace(in, oldStr, newStr, start, false, false);
	}

	TCHAR* replaceAll (const TCHAR* in, const TCHAR* oldStr, const TCHAR* newStr, int start) {
		return replace(in, oldStr, newStr, start, true, false);
	}

	TCHAR* replaceVars (const TCHAR* in, const TCHAR* varname, const TCHAR* value) {
		return replace(in, varname, value, 0, true, true);
	}

	char* replace (const char* in, const char* oldStr, const char* newStr, int start, bool isAll, bool isExact) {
		int len = strlen(in);
		int nLen = strlen(newStr);
		int oLen = strlen(oldStr);

		if (start > len || len == 0)
			return (char*)calloc (1, sizeof (char));

		char* res = (char*)calloc (nLen <= oLen ? len + 1 : len * (nLen - oLen + 1), sizeof (char));
		char* p = (char*)in + start;
		char* p2 = p;

		strncat(res, in, start);

		while((p = strstr(p, oldStr))) {
			strncat(res, p2, p - p2);
			char next = !isExact ? 0 : (p + oLen)[0];
			if (!isExact || (isExact && !isalpha(next) && !isdigit(next) && next != '_' && (next != '.')))
				strncat(res, newStr, nLen);
			else
				strncat(res, oldStr, oLen);
			p = p + oLen;
			p2 = p;

			if (!isAll)
				break;
		}

		strncat(res, p2, len - (p2 - in));
		return res;
	}

	char* replace (const char* in, const char* oldStr, const char* newStr, int start) {
		return replace(in, oldStr, newStr, start, false, false);
	}

	char* replaceAll (const char* in, const char* oldStr, const char* newStr, int start) {
		return replace(in, oldStr, newStr, start, true, false);
	}

	char* replaceVars (const char* in, const char* varname, const char* value) {
		return replace(in, varname, value, 0, true, true);
	}

	int removeUnusedVars(char* inout) {
		int len = strlen(inout);
		int j = 0;

		bool isIgnore = false;
		for (int i = 0; i < len; i++) {
			char c = inout[i];
			char n = inout[i + 1];
			isIgnore = (c == '$' && (n == '$' || isupper(n))) || (isIgnore && (isalpha(c) || isdigit(c) || c == '_' || c == '.'));

			if (!isIgnore) {
				inout[j] = c;
				j++;
			}
		}

		for (; j < len; j++)
			inout[j] = 0;

		return 0;
	}

	TCHAR* utf8to16(const char* in) {
		TCHAR *out;
		if (!in || strlen(in) == 0) {
			out = (TCHAR*)calloc (1, sizeof (TCHAR));
		} else  {
			DWORD size = MultiByteToWideChar(CP_UTF8, 0, in, -1, NULL, 0);
			out = (TCHAR*)calloc (size, sizeof (TCHAR));
			MultiByteToWideChar(CP_UTF8, 0, in, -1, out, size);
		}
		return out;
	}

	char* utf16to8(const TCHAR* in) {
		char* out;
		if (!in || _tcslen(in) == 0) {
			out = (char*)calloc (1, sizeof(char));
		} else  {
			int len = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, 0, 0);
			out = (char*)calloc (len, sizeof(char));
			WideCharToMultiByte(CP_UTF8, 0, in, -1, out, len, 0, 0);
		}
		return out;
	}

	void setClipboardText(const TCHAR* text) {
		int len = (_tcslen(text) + 1) * sizeof(TCHAR);
		HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, len);
		memcpy(GlobalLock(hMem), text, len);
		GlobalUnlock(hMem);
		OpenClipboard(0);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hMem);
		CloseClipboard();
	}

	int openFile(TCHAR* path, const TCHAR* filter) {
		OPENFILENAME ofn = {0};

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = HWND_DESKTOP;
		ofn.lpstrFile = path;
		ofn.lpstrFile[0] = '\0';
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
		return GetOpenFileName(&ofn);
	}

	bool isFileExists(const TCHAR* path) {
		WIN32_FIND_DATA FindFileData;
		HANDLE hFile = FindFirstFile(path, &FindFileData) ;
		int isFound = hFile != INVALID_HANDLE_VALUE;
		if (isFound)
			FindClose(hFile);

		return isFound;
	}

	char* getFileName(const char* path) {
		TCHAR* path16 = utils::utf8to16(path);
		TCHAR name16[255], ext16[32], name_ext16[300];
		_tsplitpath(path16, NULL, NULL, name16, ext16);
		_stprintf(name_ext16, TEXT("%s%s"), name16, ext16);
		char* name8 = utils::utf16to8(name_ext16);
		delete [] path16;
		return name8;
	}

	void drawArrow(HDC hDC, POINT a, POINT b) {
		double Par = 10.0;
		double slopy = atan2(b.y - a.y, b.x - a.x);
		double cosy = cos(slopy);
		double siny = sin(slopy);

		b = {b.x/2 + a.x / 2, b.y / 2 + a.y/2};

		MoveToEx(hDC, b.x + (-Par * cosy - (Par / 2.0 * siny)), b.y + (- Par * siny + (Par / 2.0 * cosy )), NULL);
		LineTo(hDC, b.x, b.y);
		LineTo(hDC, b.x + (- Par * cosy + (Par / 2.0 * siny)), b.y - (Par / 2.0 * cosy + Par * siny));
	}

	POINT getBezierPoint(POINT ps[], float t) {
		float t1 = 1.0 - t;
		return {
			(int)(t1 * t1 * t1 * ps[0].x + 3.0 * t * t1 * t1 * ps[1].x + 3.0 * t * t * t1 * ps[2].x + t * t * t * ps[3].x),
			(int)(t1 * t1 * t1 * ps[0].y + 3.0 * t * t1 * t1 * ps[1].y + 3.0 * t * t * t1 * ps[2].y + t * t * t * ps[3].y)
		};
	}

	void drawBezierArrow(HDC hDC, POINT ps[]) {
		drawArrow(hDC, getBezierPoint(ps, 0.4), getBezierPoint(ps, 0.6));
	}
}
