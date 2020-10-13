/*
	odbc_read(DSN, query, target)
	Read data via ODBC from external source and write it to SQLite table. If the target table doesn't exist, it'll be created.
	Use TABLES as the query to obtain a table list.

	odbc_write(query, DSN, target)
	Upload query resultset from SQLite to external database. The target table must exists.

	odbc_query(DSN, query)
	Execute query on external database e.g. to create target table.

	Returns json
		OK - {"result": "ok", ...}
		Error - {"error": "<msg>"}

	Cons
	* Some driver are read-only mode.
	* Supports only basic types - numbers and text. 
	* BLOB, dattime and etc are unsupported.

	Remarks
	* Use 32bit ODBC manager: C:\Windows\SysWOW64\odbcad32.exe

	To-Do: 
		* add opts: row limit, time limit, rejected rows limit, commit each X rows, clear before load data, custom column brackets 
		* update/insert/replace modes (requires pk) for odbc_write
		* a table for rejected rows %trg%_rjt (has all columns as text)
		* Mask quotes inside error message
*/
#define UNICODE
#define _UNICODE

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <time.h>
#include <ctype.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sql.h>
#include <assert.h>

#define MAX_DATA_LENGTH 32000
#define MAX_COLUMN_LENGTH 2000
#define MAX_ERROR_LENGTH 2000

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

TCHAR* utf8to16(const char* in) {
	TCHAR *out;
	if (!in || strlen(in) == 0) {
		out = (TCHAR*)calloc (1, sizeof (TCHAR));
	} else {
		DWORD size = MultiByteToWideChar(CP_UTF8, 0, in, -1, NULL, 0);
		out = (TCHAR*)calloc (size, sizeof (TCHAR));
		MultiByteToWideChar(CP_UTF8, 0, in, -1, out, size);
	}
	return out;
}

static void onError(sqlite3_context *ctx, const char* err) {
	char result[strlen(err) + 128];
	sprintf(result, "{\"error\": \"%s\"}", err);
	sqlite3_result_text(ctx, result, strlen(result), SQLITE_TRANSIENT);
}

static void odbc_read(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	int res = 0;
	SQLHANDLE hEnv;
	SQLHANDLE hConn;
	SQLHANDLE hStmt = 0;

	sqlite3* db = (sqlite3*)sqlite3_user_data(ctx);

	srand(time(NULL));
	int sid = rand();

	int rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
	res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;

	rc = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	rc = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hConn);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	if (res) {
		int len  = sqlite3_value_bytes(argv[0]);
		unsigned char dsn8[len + 1];
		strcpy((char*)dsn8, (char*)sqlite3_value_text(argv[0]));
		TCHAR* dsn16 = utf8to16(dsn8);	
		rc = SQLDriverConnect(hConn, NULL, dsn16, _tcslen(dsn16), 0, 0, NULL, SQL_DRIVER_NOPROMPT);
		res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
		free(dsn16);
		if (!res)
			onError(ctx, "DSN is invalid");

		rc = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
		res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

		len  = sqlite3_value_bytes(argv[1]);
		unsigned char query8[len + 1];
		strcpy((char*)query8, (char*)sqlite3_value_text(argv[1]));
		if (strcmp(query8, "TABLES") != 0) {
			TCHAR* query16 = utf8to16(query8);
			rc = SQLExecDirect(hStmt, query16, SQL_NTS);
			free(query16);
		} else {
			rc = SQLTables(hStmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0);
		}
		res = res && (rc != SQL_ERROR);

		if (rc == SQL_ERROR) {
			SQLWCHAR err16[MAX_ERROR_LENGTH + 1];	
			SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, 1, NULL, NULL, err16, MAX_ERROR_LENGTH, NULL);

			char* err8 = utf16to8(err16);
			onError(ctx, err8);
			free(err8);
		}

		len  = sqlite3_value_bytes(argv[2]);
		unsigned char target8[len + 1];
		strcpy((char*)target8, (char*)sqlite3_value_text(argv[2]));
		TCHAR* target16 = utf8to16(target8);

		SQLSMALLINT colCount = 0;
		SQLNumResultCols(hStmt, &colCount);

		int colTypes[colCount];
		TCHAR createQuery16[colCount * MAX_COLUMN_LENGTH + 512];
		TCHAR insertQuery16[colCount * MAX_COLUMN_LENGTH + 512];
		TCHAR tblname16[_tcslen(target16) + 32];
		TCHAR* dot = _tcschr(target16, TEXT('.'));
		TCHAR* tname16 = dot ? dot + 1 : target16;
		BOOL needQuotes = isalpha(tname16[0]);
		_stprintf(tblname16, TEXT("temp.%s%s%i%s"), needQuotes ? TEXT("\"") : TEXT(""), tname16, sid, needQuotes ? TEXT("\"") : TEXT(""));

		free(target16);

		_stprintf(createQuery16, TEXT("create table if not exists %s (\""), tblname16);
		_stprintf(insertQuery16, TEXT("insert into %s (\""), tblname16);


		for (int colNo = 1; colNo <= colCount; colNo++) {
			TCHAR colName[MAX_COLUMN_LENGTH + 1];
			SQLSMALLINT colType = 0;
			SQLDescribeCol(hStmt, colNo, colName, MAX_COLUMN_LENGTH, 0, &colType, 0, 0, 0);
			colTypes[colNo] = colType;
			_tcscat(createQuery16, colName);

			_tcscat(createQuery16, 
				colType == SQL_DECIMAL || colType == SQL_NUMERIC || colType == SQL_REAL || colType == SQL_FLOAT || colType == SQL_DOUBLE ? TEXT("\" real") : 
				colType == SQL_SMALLINT || colType == SQL_INTEGER || colType == SQL_BIT || colType == SQL_TINYINT || colType == SQL_BIGINT ? TEXT("\" integer") : 
				colType == SQL_CHAR || colType == SQL_VARCHAR || colType == SQL_LONGVARCHAR || colType == SQL_WCHAR || colType == SQL_WVARCHAR || colType == SQL_WLONGVARCHAR ? TEXT("\" text") : 
				colType == SQL_BINARY || colType == SQL_VARBINARY ? TEXT("\" blob") : 
				TEXT("\""));
	
			_tcscat(insertQuery16, colName);

			if (colNo != colCount) {
				_tcscat(createQuery16, TEXT(",\""));
				_tcscat(insertQuery16, TEXT("\",\""));
			}
		}

		_tcscat(createQuery16, TEXT(")"));
		_tcscat(insertQuery16, TEXT("\") values ("));
		for (int colNo = 0; colNo < colCount - 1; colNo++)
			_tcscat(insertQuery16, TEXT("?, "));
		_tcscat(insertQuery16, TEXT("?)"));

		char* createQuery8 = utf16to8(createQuery16);
		res = SQLITE_OK == sqlite3_exec(db, createQuery8, 0, 0, 0);
		free(createQuery8);

		if (res) {
			char* insertQuery8 = utf16to8(insertQuery16);
			sqlite3_stmt* stmt;
			rc = SQLITE_OK == sqlite3_prepare_v2(db, insertQuery8, -1, &stmt, 0);
			free(insertQuery8);

			if (!rc) 
				onError(ctx, sqlite3_errmsg(db));

			int insertedRows = 0;
			int rejectedRows = 0;
			SQLLEN res = 0;	
			while(rc && (SQLFetch(hStmt) == SQL_SUCCESS)) {
				for (int colNo = 1; colNo <= colCount; colNo++) {
					int colType = colTypes[colNo];
					if (colType == SQL_DECIMAL || colType == SQL_NUMERIC || colType == SQL_REAL || colType == SQL_FLOAT || colType == SQL_DOUBLE) {
						double val = 0;
						SQLGetData(hStmt, colNo, SQL_C_DOUBLE, &val, sizeof(double), &res);
						if (res != SQL_NULL_DATA)
							sqlite3_bind_double(stmt, colNo, val);
						else
							sqlite3_bind_null(stmt, colNo);
					} else if (colType == SQL_SMALLINT || colType == SQL_INTEGER || colType == SQL_BIT || colType == SQL_TINYINT || colType == SQL_BIGINT) {
						int val = 0;
						SQLGetData(hStmt, colNo, SQL_C_SLONG, &val, sizeof(int), &res);
						if (res != SQL_NULL_DATA)
							sqlite3_bind_int(stmt, colNo, val);
						else
							sqlite3_bind_null(stmt, colNo);
					} else {
						SQLWCHAR val16[MAX_DATA_LENGTH + 1];
						SQLGetData(hStmt, colNo, SQL_WCHAR, val16, MAX_DATA_LENGTH * sizeof(TCHAR), &res); 
						char* val8 = utf16to8(val16);
						if (res != SQL_NULL_DATA)
							sqlite3_bind_text(stmt, colNo, val8, strlen(val8), SQLITE_TRANSIENT);
						else
							sqlite3_bind_null(stmt, colNo);
						free(val8);
					} 
				}

				if (SQLITE_DONE == sqlite3_step(stmt)) {
					insertedRows++;
				} else {
					rejectedRows++;
				}

				sqlite3_reset(stmt);
			}

			if (rc) {
				int len = strlen(target8) + _tcslen(tblname16) + 128;
				char create8[len];
				char insert8[len];
				BOOL needQuotes = !strchr(target8, '.');
				char* tblname8 = utf16to8(tblname16);
				sprintf(create8, "create table %s%s%s as select * from %s", needQuotes ? "\"" : "", target8, needQuotes ? "\"" : "", tblname8);
				sprintf(insert8, "insert into %s%s%s select * from %s", needQuotes ? "\"" : "", target8, needQuotes ? "\"" : "", tblname8);
				free(tblname8);

				if (SQLITE_OK == sqlite3_exec(db, create8, 0, 0, 0) || SQLITE_OK == sqlite3_exec(db, insert8, 0, 0, 0)) {
					char result[512];
					sprintf(result, "{\"result\":\"ok\", \"read\": %i, \"inserted\": %i, \"rejected\": %i}", insertedRows + rejectedRows, insertedRows, rejectedRows);
					sqlite3_result_text(ctx, result, strlen(result), SQLITE_TRANSIENT);
				} else {
					onError(ctx, sqlite3_errmsg(db));
				}				
			}
			sqlite3_finalize(stmt);
		}
	} else {
		onError(ctx, "Couldn't get access to ODBC driver");
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
	SQLFreeHandle(SQL_HANDLE_DBC, hConn);
	SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
}

static void odbc_write(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	sqlite3* db = (sqlite3*)sqlite3_user_data(ctx);
	sqlite3_stmt* stmt;
	if (SQLITE_OK != sqlite3_prepare_v2(db, sqlite3_value_text(argv[0]), -1, &stmt, 0)) {
		onError(ctx, sqlite3_errmsg(db));
		return;
	}

	int res = 0;
	SQLHANDLE hEnv;
	SQLHANDLE hConn;
	SQLHANDLE hStmt = 0;

	int rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
	res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;

	rc = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	rc = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hConn);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	if (res) {
		int len  = sqlite3_value_bytes(argv[1]);
		unsigned char dsn8[len + 1];
		strcpy((char*)dsn8, (char*)sqlite3_value_text(argv[1]));
		TCHAR* dsn16 = utf8to16(dsn8);	
		rc = SQLDriverConnect(hConn, NULL, dsn16, _tcslen(dsn16), 0, 0, NULL, SQL_DRIVER_NOPROMPT);
		res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
		free(dsn16);
		if (!res)
			onError(ctx, "DSN is invalid");

		SQLSetConnectAttr(hConn, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, SQL_NTS);


		rc = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
		res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

		const char* target8 = sqlite3_value_text(argv[2]);
		unsigned char meta8[strlen(target8) + 64];
		sprintf(meta8, "select * from %s where 1 = 2", target8);
		TCHAR* meta16 = utf8to16(meta8);
		rc = SQLExecDirect(hStmt, meta16, SQL_NTS);
		res = res && (rc != SQL_ERROR);
		free(meta16);

		if (rc != SQL_ERROR) {
			SQLSMALLINT colCount = 0;
			SQLNumResultCols(hStmt, &colCount);

			if (colCount == sqlite3_column_count(stmt)) {
				int colTypes[colCount];
				TCHAR* target16 = utf8to16(target8);
				TCHAR insert16[colCount * MAX_COLUMN_LENGTH + 512];
				_stprintf(insert16, TEXT("insert into %s ("), target16);
				free(target16);
	
				for (int colNo = 1; colNo <= colCount; colNo++) {
					TCHAR colName[MAX_COLUMN_LENGTH + 1];
					SQLSMALLINT colType = 0;
					SQLDescribeCol(hStmt, colNo, colName, MAX_COLUMN_LENGTH, 0, &colType, 0, 0, 0);
					colTypes[colNo] = colType;		
					_tcscat(insert16, colName);
		
					if (colNo != colCount) 
						_tcscat(insert16, TEXT(","));
				}
	
				_tcscat(insert16, TEXT(") values ("));
				for (int colNo = 0; colNo < colCount - 1; colNo++)
					_tcscat(insert16, TEXT("?, "));
				_tcscat(insert16, TEXT("?)"));
				SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
	
				SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
				if(SQLPrepare (hStmt, insert16, SQL_NTS) != SQL_ERROR) {
					int insertedRows = 0;
					int rejectedRows = 0;
					int rowNo = 0;	

					while(SQLITE_ROW == sqlite3_step(stmt)) {
						rowNo++;
						for (int colNo = 1; colNo <= colCount; colNo++) {
							int colType = colTypes[colNo];
							BOOL isNULL = sqlite3_column_type(stmt, colNo - 1) == SQLITE_NULL;
							SQLLEN cbData = SQL_NTS;
							SQLINTEGER cbNull = SQL_NULL_DATA;
	
							if (colType == SQL_DECIMAL || colType == SQL_NUMERIC || colType == SQL_REAL || colType == SQL_FLOAT || colType == SQL_DOUBLE) {
								double val = sqlite3_column_double(stmt, colNo - 1);
								SQLBindParameter(hStmt, colNo, SQL_PARAM_INPUT, SQL_C_DOUBLE, colType, 0, 0, isNULL ? NULL : &val, 0, isNULL ? &cbNull: &cbData);
							} else if (colType == SQL_SMALLINT || colType == SQL_INTEGER || colType == SQL_BIT || colType == SQL_TINYINT || colType == SQL_BIGINT) {
								int val = sqlite3_column_int(stmt, colNo - 1);
								SQLBindParameter(hStmt, colNo, SQL_PARAM_INPUT, SQL_C_SLONG, colType, 0, 0, isNULL ? NULL : &val, 0, isNULL ? &cbNull: &cbData);
							} else {
								const char* val8 = sqlite3_column_text(stmt, colNo - 1);
								TCHAR* _val16 = utf8to16(val8);
								int len = _tcslen(_val16);
								TCHAR val16[len + 1];
								_stprintf(val16, L"%s", _val16);
								free(_val16);								

								SQLBindParameter(hStmt, colNo, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WCHAR, len, 0, isNULL ? NULL : (SQLPOINTER)val16, 0, isNULL ? &cbNull: &cbData);
							}
						}

						if (SQL_ERROR != SQLExecute(hStmt))
							insertedRows++;
						else
							rejectedRows++; 
					}
					SQLEndTran(SQL_HANDLE_DBC, hConn, SQL_COMMIT);

					char result[512];
					sprintf(result, "{\"result\":\"ok\", \"read\": %i, \"inserted\": %i, \"rejected\": %i}", insertedRows + rejectedRows ,insertedRows, rejectedRows);
					sqlite3_result_text(ctx, result, strlen(result), SQLITE_TRANSIENT);
				} else {
					onError(ctx, "Invalid insert statement");
				}
			} else {
				char err8[255];
				sprintf(err8, "Column count mismatches: query returns %i, target has %i", sqlite3_column_count(stmt), colCount);
				onError(ctx, err8);
			}
		} else {
			SQLWCHAR err16[MAX_ERROR_LENGTH + 1];	
			SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, 1, NULL, NULL, err16, MAX_ERROR_LENGTH, NULL);

			char* err8 = utf16to8(err16);
			onError(ctx, err8);
			free(err8);
		}
	} else {
		onError(ctx, "Couldn't get access to ODBC driver");
	}

	sqlite3_finalize(stmt);
	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
	SQLFreeHandle(SQL_HANDLE_DBC, hConn);
	SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
}

static void odbc_query(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	int res = 0;
	SQLHANDLE hEnv;
	SQLHANDLE hConn;
	SQLHANDLE hStmt = 0;

	sqlite3* db = (sqlite3*)sqlite3_user_data(ctx);

	srand(time(NULL));
	int sid = rand();

	int rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
	res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;

	rc = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	rc = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hConn);
	res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);

	if (res) {
		int len  = sqlite3_value_bytes(argv[0]);
		unsigned char dsn8[len + 1];
		strcpy((char*)dsn8, (char*)sqlite3_value_text(argv[0]));
		TCHAR* dsn16 = utf8to16(dsn8);	
		rc = SQLDriverConnect(hConn, NULL, dsn16, _tcslen(dsn16), 0, 0, NULL, SQL_DRIVER_NOPROMPT);
		res = rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
		free(dsn16);
		if (res) {
			rc = SQLAllocHandle(SQL_HANDLE_STMT, hConn, &hStmt);
			res = res && (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
	
			len  = sqlite3_value_bytes(argv[1]);
			unsigned char query8[len + 1];
			strcpy((char*)query8, (char*)sqlite3_value_text(argv[1]));
			TCHAR* query16 = utf8to16(query8);
			rc = SQLExecDirect(hStmt, query16, SQL_NTS);
			res = res && (rc != SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO);
			free(query16);
			if (rc != SQL_ERROR) {
				sqlite3_result_text(ctx, "{\"result\": \"ok\"}", -1, SQLITE_TRANSIENT);		
			} else {
				SQLWCHAR err16[MAX_ERROR_LENGTH + 1];	
				SQLGetDiagRec(SQL_HANDLE_STMT, hStmt, 1, NULL, NULL, err16, MAX_ERROR_LENGTH, NULL);
	
				char* err8 = utf16to8(err16);
				onError(ctx, err8);
				free(err8);	
			}
		} else {
			onError(ctx, "DSN is invalid");
		}
	} else {
		onError(ctx, "Couldn't get access to ODBC driver");
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
	SQLFreeHandle(SQL_HANDLE_DBC, hConn);
	SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
}

__declspec(dllexport) int sqlite3_odbc_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	SQLITE_EXTENSION_INIT2(pApi);
	(void)pzErrMsg;  /* Unused parameter */

	BOOL rc1 = SQLITE_OK == sqlite3_create_function(db, "odbc_read", 3, SQLITE_UTF8, (void*)db, odbc_read, 0, 0);
	BOOL rc2 = SQLITE_OK == sqlite3_create_function(db, "odbc_write", 3, SQLITE_UTF8, (void*)db, odbc_write, 0, 0);
	BOOL rc3 = SQLITE_OK == sqlite3_create_function(db, "odbc_query", 2, SQLITE_UTF8, (void*)db, odbc_query, 0, 0);

	return rc1 && rc2 && rc3 ? SQLITE_OK : SQLITE_ERROR;
}