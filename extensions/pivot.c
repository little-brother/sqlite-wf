/*
	pivot(query, colA, colB, pivotC, target)
	Calculate a pivot result and put it into a target table
	Examples
	select pivot('select manager, product, qty from orders', 'manager', 'product', 'sum(qty)')
	select pivot('select manager, strftime('01-%m-%Y', saledate) date, qty from orders', 'manager', 'date', 'max(qty)')
*/
#define UNICODE
#define _UNICODE

#define MAX_DATA_LENGTH 32000
#define MAX_COLUMN_LENGTH 2000

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <time.h>
#include <assert.h>

static void onError(sqlite3* db, sqlite3_context *ctx, const char* query) {
	char result[strlen(sqlite3_errmsg(db)) + strlen(query) + 128];
	sprintf(result, "{\"error\": \"%s\", \"query\": \"%s\"}", sqlite3_errmsg(db), query);
	sqlite3_result_text(ctx, result, strlen(result), SQLITE_TRANSIENT);
}

static void pivot(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	const char* query = sqlite3_value_text(argv[0]);
	const char* colA = sqlite3_value_text(argv[1]);
	const char* colB = sqlite3_value_text(argv[2]);
	const char* pivotC = sqlite3_value_text(argv[3]);
	const char* target = sqlite3_value_text(argv[4]);
	sqlite3* db = (sqlite3*)sqlite3_user_data(ctx);

	srand(time(NULL));
	int sid = rand();

	char buf[strlen(query) + strlen(colA) + strlen(colB) + strlen(pivotC) + 255];
	sprintf(buf, "create table temp.pivot%i as select %s, %s b, %s c from (%s) group by 1, 2", sid, colA, colB, pivotC, query); 
	if (SQLITE_OK != sqlite3_exec(db, buf, 0, 0, 0)) 
		return onError(db, ctx, buf);

	sqlite3_stmt* stmt;
	char sbuf[256];

	char column1[MAX_COLUMN_LENGTH + 1];
	memset(column1, 0, MAX_COLUMN_LENGTH + 1);
	sprintf(sbuf, "select * from temp.pivot%i where 1 = 2", sid);
	if ((SQLITE_OK == sqlite3_prepare_v2(db, sbuf, -1, &stmt, 0)) && (SQLITE_DONE == sqlite3_step(stmt))) {
		strncpy(column1, sqlite3_column_name(stmt, 0), MAX_COLUMN_LENGTH);
	} else {
		onError(db, ctx, sbuf);
	}
	sqlite3_finalize(stmt);

	sprintf(sbuf, "select distinct b from temp.pivot%i where b is not null", sid);
	if (strlen(column1) && (SQLITE_OK == sqlite3_prepare_v2(db, sbuf, -1, &stmt, 0))) {
		char q[MAX_DATA_LENGTH];
		char c = strchr(target, '.') ? ' ' : '"';
		sprintf(q, "create table %c%s%c as select \"%s\"", c, target, c, column1);
		while (SQLITE_ROW == sqlite3_step(stmt)) {
			const char* val = sqlite3_column_text(stmt, 0);
			char buf[strlen(val)* 2 + 64];
			sprintf(buf, ", sum(c) filter(where b = '%s') \"%s\"", val, val);
			strcat(q, buf);
		}
		sprintf(sbuf, " from temp.pivot%i group by 1", sid);
		strcat(q, sbuf);

		if (SQLITE_OK != sqlite3_exec(db, q, 0, 0, 0))
			return onError(db, ctx, q);

		sprintf(sbuf, "drop table temp.pivot%i", sid);
		sqlite3_exec(db, sbuf, 0, 0, 0);

		char result[] = "{\"result\": \"ok\"}";
		sqlite3_result_text(ctx, result, strlen(result), SQLITE_TRANSIENT);
	}
	sqlite3_finalize(stmt);
}

__declspec(dllexport) int sqlite3_pivot_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	int rc = SQLITE_OK;
	SQLITE_EXTENSION_INIT2(pApi);
	(void)pzErrMsg;  /* Unused parameter */
	return sqlite3_create_function(db, "pivot", 5, SQLITE_UTF8, (void*)db, pivot, 0, 0);
}