/*
	exec(cmd, maxOutputLength = 32000)
	Executes shell command and returns console output as result.
	If maxOutputLength less 1 then returns NULL
	Can be used to import an external data or to execute smth in a trigger
	select exec('powershell -nologo "Get-Content C:/data.txt"') -- as one value
	select * from exec('powershell -nologo "Get-Content C:/data.txt"') -- as a table
*/

#define UNICODE
#define _UNICODE

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
char* utf16to8(const TCHAR* in) {
	char* out;
	if (!in || _tcslen(in) == 0) {
		out = (char*)calloc (1, sizeof(char));
	} else {
		int len = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, 0, 0);
		out = (char*)calloc (len, sizeof(char));
		WideCharToMultiByte(CP_UTF8, 0, in, -1, out, len, 0, 0);
	}
	return out;
}

TCHAR* MBto16(const char* in, UINT codepage) {
	TCHAR *out;
	if (!in || strlen(in) == 0) {
		out = (TCHAR*)calloc (1, sizeof (TCHAR));
	} else {
		DWORD size = MultiByteToWideChar(codepage, 0, in, -1, NULL, 0);
		out = (TCHAR*)calloc (size, sizeof (TCHAR));
		MultiByteToWideChar(codepage, 0, in, -1, out, size);
	}
	return out;
}

TCHAR* utf8to16(const char* in) {
	return MBto16(in, CP_UTF8);
}

TCHAR* OEMto16(const char* in) {
	return MBto16(in, CP_OEMCP);
}

static BOOL execCmd(const char* cmd, char* output, int maxLength) {
	SECURITY_ATTRIBUTES sa = {0};
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;
	
	HANDLE hStdOutRd, hStdOutWr;
	HANDLE hStdErrRd, hStdErrWr;
	
	if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0) || !CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0)) 
		return FALSE;

	SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
	
	STARTUPINFO si = {0};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = hStdOutWr;
	si.hStdError = hStdErrWr;
	
	PROCESS_INFORMATION pi = {0};
	
	TCHAR* cmd16 = utf8to16(cmd);
	BOOL rc = CreateProcess(NULL, cmd16, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi); 
	free(cmd16);

	CloseHandle(hStdOutWr);
	CloseHandle(hStdErrWr);

	BYTE buf[maxLength + 1];
	BYTE data[maxLength + 1];
	memset (data, 0, maxLength + 1);

	for (int i = 0; maxLength > 0 && rc && (i < 2); i++) {
		BOOL bSuccess = FALSE;
		DWORD dwRead;
		HANDLE hRd = i == 0 ? hStdOutRd : hStdErrRd;

		for (;strlen(data) < maxLength;) { 
			memset (buf, 0, maxLength + 1);
			bSuccess = ReadFile(hRd, buf, maxLength, &dwRead, NULL);
			if(!bSuccess) 
				break; 
			
			int rLen = strlen(data);
			int bLen = strlen(buf);
			strncat(data, buf, rLen + bLen < maxLength ? bLen : maxLength - rLen);
		}

		// CP437 -> UTF16 -> UTF8
		TCHAR* data16 = OEMto16(data);
		char* data8 = utf16to8(data16);
		memset(output, 0, maxLength + 1);
		strncpy(output, data8, strlen(data8));
		free(data16);
		free(data8);
	}


	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	CloseHandle(hStdOutRd);	
	CloseHandle(hStdErrRd);

	return rc;
}

typedef struct exec_vtab exec_vtab;
struct exec_vtab {
	sqlite3_vtab base;
};

typedef struct exec_cursor exec_cursor;
struct exec_cursor {
	sqlite3_vtab_cursor base;
	
	sqlite3_int64 iRowid;
	char* cmd;
	int len;
	BOOL isEof; 
	char* data;
	char* pos;
};

static int execConnect(sqlite3 *db, void *pAux, int argc, const char *const*argv, sqlite3_vtab **ppVtab, char **pzErr){
	int rc = sqlite3_declare_vtab(db, "CREATE TABLE x(line, cmd hidden, len hidden)");
	if (rc==SQLITE_OK) {
		exec_vtab *pTab = sqlite3_malloc(sizeof(*pTab));
		*ppVtab = (sqlite3_vtab*)pTab;

		if (pTab == 0) 
			return SQLITE_NOMEM;

		memset(pTab, 0, sizeof(*pTab));
		sqlite3_vtab_config(db, SQLITE_VTAB_DIRECTONLY);
	}

	return rc;
}

static int execDisconnect(sqlite3_vtab *pVtab){
	exec_vtab *pTab = (exec_vtab*)pVtab;
	sqlite3_free(pTab);

	return SQLITE_OK;
}

static int execOpen(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCursor){
	exec_cursor *pCur;
	pCur = sqlite3_malloc(sizeof(*pCur));
	if (pCur == 0)
		return SQLITE_NOMEM;

	memset(pCur, 0, sizeof(*pCur));
	*ppCursor = &pCur->base;
	pCur->data = 0;
	pCur->pos = 0;
	pCur->isEof = TRUE;

	return SQLITE_OK;
}

static int execClose(sqlite3_vtab_cursor *cur){
	exec_cursor *pCur = (exec_cursor*)cur;
	free(pCur->data);
	free(pCur->cmd);
	sqlite3_free(pCur);
	
	return SQLITE_OK;
}

static int execNext(sqlite3_vtab_cursor *cur){
	exec_cursor *pCur = (exec_cursor*)cur;
	pCur->pos = strchr(pCur->pos, '\n');
	if (pCur->pos != NULL && (pCur->pos + 1)[0]) /* Ignore empty last row */
		pCur->pos = pCur->pos + 1;
	else 
		pCur->isEof = TRUE;
	
	pCur->iRowid++;
	return SQLITE_OK;
}

static int execColumn(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int colNo) {
	exec_cursor *pCur = (exec_cursor*)cur;
	exec_vtab *pTab = (exec_vtab*)(&pCur->base);
	if (colNo == 0) {
		char* end = strchr(pCur->pos, '\n');
		int len = strlen(pCur->pos) - (end ? strlen(end) : 0);
		char line[len + 1];
		memset(line, 0, len + 1);
		memcpy(line, pCur->pos, len - (end && (end - 1)[0] == '\r')); /* Remove \r\n or \n */
		line[len] = 0;

		sqlite3_result_text(ctx, (char*)line, -1, SQLITE_TRANSIENT);
	} else if (colNo == 1) {
		sqlite3_result_text(ctx, (char*)pCur->cmd, -1, SQLITE_TRANSIENT);
	} else {
		sqlite3_result_int(ctx, pCur->len);
	}
	return SQLITE_OK;

}

static int execRowid(sqlite3_vtab_cursor* cur, sqlite_int64* pRowid){
	exec_cursor *pCur = (exec_cursor*)cur;
	*pRowid = pCur->iRowid;

	return SQLITE_OK;
}

static int execEof(sqlite3_vtab_cursor *cur){
	exec_cursor *pCur = (exec_cursor*)cur;
	return pCur->isEof;
}

static int execFilter(sqlite3_vtab_cursor *cur, int idxNum, const char *idxStr, int argc, sqlite3_value **argv){
	exec_cursor *pCur = (exec_cursor *)cur;

	const char* cmd = sqlite3_value_text(argv[0]);
	pCur->cmd = malloc(sizeof(char) * (strlen(cmd) + 1));
	sprintf(pCur->cmd, cmd);

	int len = argc == 2 ? sqlite3_value_int(argv[1]) : 0;
	len = len <= 0 ? 32000 : len;
	pCur->len = len;

	if (!pCur->data) {
		pCur->data = (char*)calloc(sizeof(char), len + 1);
		if (!execCmd(pCur->cmd, pCur->data, pCur->len)) {
			execClose(cur);
			return SQLITE_ERROR;
		}
	}

	pCur->iRowid = 1;
	pCur->pos = pCur->data;
	pCur->isEof = FALSE;
	return SQLITE_OK;
}

static int execBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){	
	if (!pIdxInfo->nConstraint)
		return SQLITE_RANGE;

	// I don't know what is it :(
	int i, j;
	int idxNum = 0;
	int unusableMask = 0;
	int nArg = 0;
	int aIdx[2];
	const struct sqlite3_index_constraint *pConstraint;

	aIdx[0] = aIdx[1] = -1;
	pConstraint = pIdxInfo->aConstraint;
	for(i = 0; i < pIdxInfo->nConstraint; i++, pConstraint++) {
		int iCol;
		int iMask;
		if(pConstraint->iColumn < 1) 
		continue;
		
		iCol = pConstraint->iColumn - 1;
		iMask = 1 << iCol;
		if(pConstraint->usable==0) {
			unusableMask |=  iMask;
			continue;
		} else if (pConstraint->op == SQLITE_INDEX_CONSTRAINT_EQ) {
			idxNum |= iMask;
			aIdx[iCol] = i;
		}
	}
	
	for(i = 0; i < 2; i++) {
		if((j = aIdx[i]) >= 0){
			pIdxInfo->aConstraintUsage[j].argvIndex = ++nArg;
			pIdxInfo->aConstraintUsage[j].omit = 0;
		}
	}
	
	if((unusableMask & ~idxNum) != 0)
		return SQLITE_CONSTRAINT;
	
	pIdxInfo->estimatedCost = (double)10;
	pIdxInfo->estimatedRows = 10;
	pIdxInfo->idxNum = idxNum;
	return SQLITE_OK;
}

static sqlite3_module execModule = {
	/* iVersion    */ 0,
	/* xCreate     */ 0,
	/* xConnect    */ execConnect,
	/* xBestIndex  */ execBestIndex,
	/* xDisconnect */ execDisconnect,
	/* xDestroy    */ 0,
	/* xOpen       */ execOpen,
	/* xClose      */ execClose,
	/* xFilter     */ execFilter,
	/* xNext       */ execNext,
	/* xEof        */ execEof,
	/* xColumn     */ execColumn,
	/* xRowid      */ execRowid,
	/* xUpdate     */ 0,
	/* xBegin      */ 0,
	/* xSync       */ 0,
	/* xCommit     */ 0,
	/* xRollback   */ 0,
	/* xFindMethod */ 0,
	/* xRename     */ 0,
	/* xSavepoint  */ 0,
	/* xRelease    */ 0,
	/* xRollbackTo */ 0,
	/* xShadowName */ 0
};

static void exec(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	if (argc == 0) {
		sqlite3_result_null(ctx);
		return;		
	}

	int len = argc > 1 ? sqlite3_value_int(argv[1]) : 32000;
	len = len < 0 ? 32000 : len;
	char output[len + 1];
	memset(output, 0, len + 1);
			
	if (execCmd((const char*)sqlite3_value_text(argv[0]), output, len)) {
		sqlite3_result_text(ctx, (char*)output, -1, SQLITE_TRANSIENT);
	} else {
		sqlite3_result_text(ctx, (char*)"An error has occurred while creating a process", -1, SQLITE_TRANSIENT);
	}
}

__declspec(dllexport) int sqlite3_exec_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	int rc = SQLITE_OK;
	SQLITE_EXTENSION_INIT2(pApi);
	return SQLITE_OK == sqlite3_create_module(db, "exec", &execModule, 0) && SQLITE_OK == sqlite3_create_function(db, "exec", -1, SQLITE_UTF8, 0, exec, 0, 0) ? SQLITE_OK : SQLITE_ERROR;
}
