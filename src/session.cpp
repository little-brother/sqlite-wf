#include <windows.h>
#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "utils.h"
#include "session.h"

#define TASK_FAIL     0
#define TASK_DONE     1
#define NO_MORE_TASK  2

namespace session {
	static void _assert(sqlite3_context *ctx, int argc, sqlite3_value **argv){
		char msg[255 + 1] = "Error";
		if (argc > 1)
			snprintf(msg, 255, "%s", sqlite3_value_text(argv[1]));

		if (argc > 0 && sqlite3_value_int(argv[0]) == 0)
			sqlite3_result_error(ctx, msg, -1);
	}

	int setArg(int sid, const char* var, const char* value) {
		sqlite3_stmt* stmt;
		sqlite3_prepare_v2(db, "replace into workflow_vars (sid, var, value) select ?1, ?2, ?3", -1, &stmt, NULL);

		sqlite3_bind_int(stmt, 1, sid);
		sqlite3_bind_text(stmt, 2, var, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, value ? value : "", -1, SQLITE_TRANSIENT);
		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		return rc;
	}

	int setArg(int sid, const char* var, const int value) {
		char buf[64] = {0};
		sprintf(buf, "%i", value);
		return setArg(sid, var, buf);
	}

	void setError(int sid, int nid, const char* err) {
		if (!err)
			return;

		sqlite3_stmt* stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(db, "update workflow_session_details set error = iif(error is null, ?1, error || ', ' || ?1) where sid = ?2 and node_id = ?3", -1, &stmt, NULL)) {
			sqlite3_bind_text(stmt, 1, err, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, sid);
			sqlite3_bind_int(stmt, 3, nid);
			sqlite3_step(stmt);
		}

		sqlite3_finalize(stmt);
	}

	char* applyArgs(int sid, const char* str) {
		if (!str || !strlen(str))
			return (char*)calloc (1, sizeof (char));

		char* res = (char*)calloc (strlen(str) + 1, sizeof (char*));
		strcpy(res, str);

		sqlite3_stmt* stmt;
		sqlite3_prepare_v2(db, "select var, value from workflow_vars where sid = ?1 union select var, value from workflow_globals", -1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, sid);

		while (SQLITE_ROW == sqlite3_step(stmt)) {
			char* res2 = utils::replaceVars(res, (const char*)sqlite3_column_text(stmt, 0), (const char*)sqlite3_column_text(stmt, 1));
			free(res);
			res = res2;
		}
		sqlite3_finalize(stmt);
		utils::removeUnusedVars(res);

		return res;
	}

	bool runTaskSQL(sqlite3* sdb, int sid, int nid) {
		sqlite3_stmt* stmt;
		int rc = SQLITE_OK == sqlite3_prepare_v2(db, "select coalesce(data, '') from workflow_nodes where id = ?1", -1, &stmt, NULL);
		if (rc)
		sqlite3_bind_int(stmt, 1, nid);
		if(!rc || SQLITE_ROW != sqlite3_step(stmt)) {
			setError(sid, nid, "Node data corrupted");
			sqlite3_finalize(stmt);
			return TASK_FAIL;
		}

		char* raw = applyArgs(sid, (char*)sqlite3_column_text(stmt, 0));
		sqlite3_finalize(stmt);

		bool isEmpty = true;
		for (size_t i = 0; isEmpty && i < strlen(raw); i++)
			isEmpty = !isalpha(raw[i]);
		if (isEmpty) {
			free(raw);
			return TASK_DONE;
		}
		char query[strlen(raw) + 32];
		strcpy(query, raw);

		rc = sqlite3_prepare_v2(sdb, query, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_finalize(stmt);
			sprintf(query, "select %s", raw);
			rc = sqlite3_prepare_v2(sdb, query, -1, &stmt, NULL);
		}
		free(raw);

		if (rc == SQLITE_OK) {
			rc = sqlite3_step(stmt);
		} else
			setError(sid, nid, sqlite3_errmsg(sdb));

		if (rc == SQLITE_ROW) {
			char buf[64] = {0};
			sprintf(buf, "$$NODE%i.DATA", nid);
			setArg(sid, buf, sqlite3_column_type(stmt, 0) == SQLITE_NULL ? "NULL" : (const char*)sqlite3_column_text(stmt, 0));
		}

		sqlite3_finalize(stmt);

		return rc == SQLITE_ROW || rc == SQLITE_DONE ? TASK_DONE : TASK_FAIL;
	}

	bool runTaskVar(sqlite3* sdb, int sid, int nid) {
		sqlite3_stmt* stmt;
		bool rc = SQLITE_OK == sqlite3_prepare_v2(db,
			"select json_extract(v.value, '$[0]') var, json_extract(v.value, '$[1]') value " \
			"from workflow_nodes n, json_each(data) v where n.id = ?1",
			-1, &stmt, NULL);

		sqlite3_bind_int(stmt, 1, nid);

		while (rc && SQLITE_ROW == sqlite3_step(stmt)) {
			char* var = (char*)sqlite3_column_text(stmt, 0);
			char* value = applyArgs(sid, (char*)sqlite3_column_text(stmt, 1));
			char query[MAX_TEXT_LENGTH] = {0};
			sprintf(query, "%s", value);

			sqlite3_stmt* stmt2;
			rc = SQLITE_OK == sqlite3_prepare_v2(sdb, query, -1, &stmt2, NULL);
			if (!rc) {
				sqlite3_finalize(stmt2);
				sprintf(query, "select %s", value);
				rc = SQLITE_OK == sqlite3_prepare_v2(sdb, query, -1, &stmt2, NULL);
			}

			if (rc) {
				if (SQLITE_ROW == sqlite3_step(stmt2)) {
					int colCount = sqlite3_column_count(stmt2);
					if (colCount > 1) {
						for (int i = 0; rc && i < colCount; i++) {
							char* upper = StrDupA((char*)sqlite3_column_name(stmt2, i));
							char varcol[strlen(var) + strlen(upper) + 2]{0};
							snprintf(varcol, 255, "%s.%s", var, upper);
							rc = setArg(sid, varcol, (char*)sqlite3_column_text(stmt2, i));
							LocalFree(upper);
						}
					} else {
						rc = setArg(sid, var, (char*)sqlite3_column_text(stmt2, 0));
					}
				}
			} else {
				rc = false;
				setError(sid, nid, sqlite3_errmsg(sdb));
			}

			sqlite3_finalize(stmt2);
			free(value);
		}
		sqlite3_finalize(stmt);

		return rc ? TASK_DONE : TASK_FAIL;
	}

	bool runTaskSub(sqlite3* sdb, int sid, int nid) {
		bool rc = true;

		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare_v2(db,
			"select json_extract(data,'$.workflow_id'), json_extract(data,'$.args'), json_extract(data,'$.is_parallel') " \
			"from workflow_nodes where id = ?1 and json_extract(data,'$.workflow_id') and exists (select id from workflows)",
			-1, &stmt, NULL)) {
			sqlite3_finalize(stmt);
			setError(sid, nid, "Incorrect subflow data");
			return TASK_FAIL;
		}
		sqlite3_bind_int(stmt, 1, nid);
		sqlite3_step(stmt);

		int wid = sqlite3_column_int(stmt, 0);

		char* args = applyArgs(sid, (const char*)sqlite3_column_text(stmt, 1));
		sqlite3_finalize(stmt);

		char query[strlen(args) + 32];
		sprintf(query, "%s", args);

		if (SQLITE_OK != sqlite3_prepare_v2(sdb, query, -1, &stmt, NULL)) {
			sqlite3_finalize(stmt);
			sprintf(query, "select %s", args);
		}
		free(args);

		if (!wid) {
			setError(sid, nid, "Subflow doesn't specified");
			return TASK_FAIL;
		}

		if (SQLITE_OK == sqlite3_prepare_v2(sdb, query, -1, &stmt, NULL)) {
			int rowNo = 0;
			const char* dbname = sqlite3_db_filename(sdb, 0);
			while (rc && (SQLITE_ROW == sqlite3_step(stmt))) {
				int argCount = sqlite3_column_count(stmt);
				const char* values[argCount];
				for (int i = 0; i < argCount; i++)
					values[i] = (const char*)sqlite3_column_text(stmt, i);
				rc = run(wid, dbname, sid, argCount, values);

				rowNo++;
			}

			if (!rowNo)
				rc = run(wid, dbname, sid, 0, NULL);
		} else {
			setError(sid, nid, sqlite3_errmsg(sdb));
			rc = false;
		}
		sqlite3_finalize(stmt);

		return TASK_DONE;
	}

	bool runTaskIf(sqlite3 *sdb, int sid, int nid) {
		bool rc = true;
		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare_v2(db, "select json_extract(value, '$[1]') from workflow_nodes n, json_each(coalesce(data, '[]')) where n.id = ?1", -1, &stmt, NULL)) {
			setError(sid, nid, "Node data corrupted");
			sqlite3_finalize(stmt);
			return TASK_FAIL;
		}

		sqlite3_bind_int(stmt, 1, nid);
		char res[255];
		sprintf(res, "[");

		while (rc && (SQLITE_ROW == sqlite3_step(stmt))) {
			char* value = applyArgs(sid, (const char*)sqlite3_column_text(stmt, 0));
			if (strlen(res) > 1)
				strcat(res, ",");
			if (!strlen(value)) {
				strcat(res, "0");
				continue;
			}

			char query[MAX_TEXT_LENGTH] = {0};
			sprintf(query, "%s", value);

			sqlite3_stmt* stmt2;
			rc = SQLITE_OK == sqlite3_prepare_v2(sdb, query, -1, &stmt2, NULL);
			if (!rc) {
				sqlite3_finalize(stmt2);
				sprintf(query, "select %s", value);
				rc = SQLITE_OK == sqlite3_prepare_v2(sdb, query, -1, &stmt2, NULL);
			}

			if (rc) {
				int src = sqlite3_step(stmt2);
				if (src == SQLITE_ROW)
					strcat(res, sqlite3_column_int(stmt2, 0) ? "1" : "0");
				else if (src == SQLITE_ERROR) {
					strcat(res, "-1");
					setError(sid, nid, sqlite3_errmsg(sdb));
				} else
					strcat(res, "0");
			} else {
				setError(sid, nid, sqlite3_errmsg(sdb));
				rc = false;
			}
			sqlite3_finalize(stmt2);
		}
		sqlite3_finalize(stmt);

		strcat(res, "]");

		char buf[64] = {0};
		sprintf(buf, "$$NODE%i.DATA", nid);
		setArg(sid, buf, res);

		return rc;
	}

	int runNextTask(sqlite3* sdb, int sid) {
		int nid = 0;
		int type = 0;
		bool isContinue = false;
		int nrc = NO_MORE_TASK;

		sqlite3_stmt* stmt;
		bool rc = SQLITE_OK == sqlite3_prepare(db,
			"with ids as ( " \
			"select value id from workflow_sessions s, json_each(s.nodes) where s.id = ?1) " \
			"select ids.id, n.type, n.is_stop_on_error, count(l.id) from ids " \
			"left join workflow_links l on l.to_id = ids.id " \
			"inner join workflow_nodes n on n.id = ids.id " \
			"group by ids.id order by 4 limit 1", -1, &stmt, 0);
		if (rc) {
			sqlite3_bind_int(stmt, 1, sid);
			int rc2 = sqlite3_step(stmt);
			if (rc2 == SQLITE_ROW) {
				nid = sqlite3_column_int(stmt, 0);
				type = sqlite3_column_int(stmt, 1);
				isContinue = !sqlite3_column_int(stmt, 2);

				char jquery[MAX_TEXT_LENGTH];
				sprintf(jquery,
					"insert into workflow_session_details (sid, node_id, args, start_time, status) " \
					"select %i, %i, (select json_group_object(var, value) from workflow_vars where sid = %i), strftime('%%s', 'now'), 'RUNNING'",
					sid, nid, sid);
				sqlite3_exec(db, jquery, 0, 0, 0);
				int jid = sqlite3_last_insert_rowid(db);

				char buf[64] = {0};
				sprintf(buf, "$$NODE%i.STATUS", nid);
				setArg(sid, buf, "RUNNING");

				nrc =
					type == NODE_SQL ? runTaskSQL(sdb, sid, nid) :
					type == NODE_VAR ? runTaskVar(sdb, sid, nid) :
					type == NODE_SUB ? runTaskSub(sdb, sid, nid) :
					type == NODE_IF ? runTaskIf(sdb, sid, nid) :
					TASK_DONE;

				sprintf(jquery,
					"update workflow_session_details set end_time = strftime('%%s', 'now'), data = (select value from workflow_vars where sid = %i and var = '$$NODE%i.DATA'), status = '%s' where id = %i",
					sid, nid, nrc ? "DONE" : "FAIL", jid);
				sqlite3_exec(db, jquery, 0, 0, 0);
			} else if (rc2 == SQLITE_DONE) {
				nrc = NO_MORE_TASK;
			} else {
				nrc = TASK_FAIL;
			}
		}
		sqlite3_finalize(stmt);

		// Update execition cursor workflow_sessions.nodes
		// Remove node and add linked nodes to execution cursor
		if (nid && (rc || isContinue)) {
			char query[1024] = {0};
			if (type != NODE_IF)
				sprintf(query,
					"with t as ("
					"select ids.value id " \
					"from workflow_sessions s, json_each(s.nodes) ids " \
					"where s.id = %i and ids.value <> %i " \
					"union " \
					"select to_id from workflow_links where from_id = %i) " \
					"update workflow_sessions set nodes = (select json_group_array(id) from t) where id = %i",
					sid, nid, nid, sid);
			else
				sprintf(query,
					"with t as ("
					"select ids.value id " \
					"from workflow_sessions s, json_each(s.nodes) ids " \
					"where s.id = %i and ids.value <> %i " \
					"union " \
					"select to_id from workflow_links where "\
					"from_id = %i and json_extract((select value from workflow_vars where sid = %i and var = '$$NODE%i.DATA'), '$[' || port_no || ']')) " \
					"update workflow_sessions set nodes = (select json_group_array(id) from t) where id = %i",
					sid, nid, nid, sid, nid, sid);

			rc = SQLITE_OK == sqlite3_exec(db, query, NULL, 0, NULL);
		}

		if (nid) {
			char buf[64] = {0};
			sprintf(buf, "$$NODE%i.STATUS", nid);
			setArg(sid, buf, nrc == TASK_FAIL ? "FAIL" : "DONE");
		}

		return (nrc == TASK_FAIL) && isContinue? TASK_DONE : nrc;
	}

	bool run(int workflow_id, const char* dbpath, int parent_sid, int argc, const char** args) {
		sqlite3* sdb = NULL;
		sqlite3_stmt* stmt;

		char query[MAX_TEXT_LENGTH];
		int rc = SQLITE_OK == sqlite3_open(dbpath, &sdb);
		if (!rc) {
			sprintf(query,
					"insert into workflow_sessions (workflow_id, start_time, end_time, dbpath, parent_sid, status, error) " \
					"values (%i, strftime('%%s', 'now'), strftime('%%s', 'now'), '%s', %i, 'FAIL', 'Couldn\'t open database')",
					workflow_id, dbpath, parent_sid);
			sqlite3_exec(db, query, 0, 0, 0);
			return false;
		}

		sprintf(query, "attach database '%s' as meta", sqlite3_db_filename(sdb, 0));
		sqlite3_exec(sdb, query, 0, 0, 0);

		// load extensions
		TCHAR searchPath[MAX_PATH]{0};
		_stprintf(searchPath, TEXT("%s\\extensions\\*.dll"), APP_PATH);
		WIN32_FIND_DATA ffd;
		HANDLE hFind = FindFirstFile(searchPath, &ffd);
		if (hFind != INVALID_HANDLE_VALUE) {
			sqlite3_enable_load_extension(sdb, true);
			do {
				TCHAR file16[MAX_PATH]{0};
				_stprintf(file16, TEXT("%s/extensions/%s"), APP_PATH, ffd.cFileName);
				char* file8 = utils::utf16to8(file16);
				if (SQLITE_OK == sqlite3_load_extension(sdb, file8, NULL, NULL));
				free(file8);
			} while ((FindNextFile(hFind, &ffd)));
			FindClose(hFind);
		}
		sqlite3_create_function(sdb, "assert", -1, SQLITE_UTF8, 0, _assert, 0, 0);

		// Register session
		rc = SQLITE_OK == sqlite3_prepare_v2(db,
			"with n as ( " \
			"select workflow_id, json_group_array(id) nodes "\
			"from workflow_nodes " \
			"where workflow_id = ?1 and id not in (select to_id from workflow_links)) " \
			"insert into workflow_sessions (workflow_id, start_time, dbpath, parent_sid, nodes, status) " \
			"select workflow_id, strftime('%s', 'now'), ?2, ?3, nodes, 'RUNNING' from n", -1, &stmt, 0);
		if (rc) {
			sqlite3_bind_int(stmt, 1, workflow_id);
			sqlite3_bind_text(stmt, 2, dbpath, -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 3, parent_sid);

			rc = SQLITE_DONE == sqlite3_step(stmt);
		}
		sqlite3_finalize(stmt);

		if (!rc) {
			sqlite3_close(sdb);
			sprintf(query,
					"insert into workflow_sessions (workflow_id, start_time, end_time, dbpath, parent_sid, status, error) " \
					"values (%i, strftime('%%s', 'now'), '%s', %i, 'FAIL', 'Coludn\'t build workflow')",
					workflow_id, dbpath, parent_sid);
			sqlite3_exec(db, query, 0, 0, 0);

			return false;
		}

		int sid = sqlite3_last_insert_rowid(db);

		// Set session args
		setArg(sid, "$$SID", sid);
		for (int argNo = 0; rc && argNo < argc; argNo++) {
			char var[32] = {0};
			sprintf(var, "$$ARG%i", argNo + 1);
			rc = setArg(sid, var, args[argNo]);
		}

		sprintf(query,
			"update workflow_sessions set args = (select json_group_object(var, value) " \
			"from workflow_vars where sid = %i and var like '$$ARG%%') where id = %i",
			sid, sid);
		sqlite3_exec(db, query, 0, 0, 0);

		// Run processing
		int status = TASK_FAIL;
		if (rc) {
			status = runNextTask(sdb, sid);
			while (status == TASK_DONE)
				status = runNextTask(sdb, sid);
		}

		// Close session
		rc = SQLITE_OK == sqlite3_prepare_v2(db, "update workflow_sessions set end_time = strftime('%s', 'now'), status = ?2 where id = ?1", -1, &stmt, 0);
		if (rc) {
			sqlite3_bind_int(stmt, 1, sid);
			sqlite3_bind_text(stmt, 2, status ? "DONE" : "FAIL", -1, SQLITE_TRANSIENT);
			rc = SQLITE_DONE == sqlite3_step(stmt);
		}
		sqlite3_finalize(stmt);

		if (!status) {
			sqlite3_prepare_v2(db, "select onerror from workflows where id = ?1 and length(onerror) > 0", -1, &stmt, 0);
			sqlite3_bind_int(stmt, 1, workflow_id);
			if (SQLITE_ROW == sqlite3_step(stmt))
				sqlite3_exec(sdb, (const char*)sqlite3_column_text(stmt, 0), 0, 0, 0);
		}

		sqlite3_close(sdb);
		return status == NO_MORE_TASK;
	}
}
