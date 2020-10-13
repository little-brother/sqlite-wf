#ifndef __PREFS_H__
#define __PREFS_H__

#include <stdio.h>
#include <windows.h>
#include <tchar.h>

#include "sqlite3.h"

namespace prefs {
	sqlite3* load(char* path);
	bool save();

	int get(const char* name);
	void set(const char* name, int value);

	char* get(const char* name, const char* def);
	bool set(const char* name, char* value);
}

#endif
