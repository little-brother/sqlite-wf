/* 
	ASSERT(condition, msg = NULL)
	Throw error if the condition returns FALSE
	select ASSERT(1 = 2, 'INEQUALITY')
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <string.h>

static void _assert(sqlite3_context *ctx, int argc, sqlite3_value **argv){
	char msg[255 + 1] = "Error"; 
	if (argc > 1)
		strncpy(msg, sqlite3_value_text(argv[1]), 255);

	if (argc > 0 && sqlite3_value_int(argv[0]) == 0) 
		sqlite3_result_error(ctx, msg, -1);
}

__declspec(dllexport) int sqlite3_assert_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	int rc = SQLITE_OK;
	SQLITE_EXTENSION_INIT2(pApi);
	(void)pzErrMsg;  /* Unused parameter */
	return sqlite3_create_function(db, "assert", -1, SQLITE_UTF8, 0, _assert, 0, 0);
}