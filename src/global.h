#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#define _WIN32_IE	                0x0500
#define LVS_EX_AUTOSIZECOLUMNS  0x10000000
#define DLG_OK                           1
#define DLG_CANCEL                      -1


#define MAX_TEXT_LENGTH              32000
#define MAX_NAME_LENGTH                512
#define MAX_LINK_COUNT                1024

#define MODE_NONE                        0
#define MODE_MOVE_NODE                   1
#define MODE_DRAW_LINK                   2

#define NODE_SQL                         1
#define NODE_VAR                         2
#define NODE_IF                          3
#define NODE_SUB                         4
#define NODE_SIZE                       64

#define CX_LINK_HANDLE_RADIUS            9
#define CX_LINK_INDENT                  30

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shlwapi.h>
#include <time.h>
#include <sys/stat.h>
#include <locale.h>
#include "sqlite3.h"

extern TCHAR APP_BUFFER[MAX_TEXT_LENGTH];
extern TCHAR APP_PATH[MAX_PATH];
extern sqlite3* db;

bool showDbError();
int setListViewData(HWND hListWnd, sqlite3_stmt *stmt);

#endif
