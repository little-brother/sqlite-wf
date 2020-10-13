#include <ctime>
#include "prefs.h"
#include "sqlite3.h"

namespace prefs {
	sqlite3* db;

	const int ICOUNT = 5;
	const char* iprops[ICOUNT] = {
		"wf-x", "wf-y", "wf-width", "wf-height",
		"wf-last-workflow-id"
	};

	int ivalues[ICOUNT] = {
		100, 100, 800, 600,
		0
	};

	int get(const char* name) {
		for (int i = 0; i < ICOUNT; i++)
			if (!strcmp(iprops[i], name))
				return ivalues[i];

		return 0;
	}

	void set(const char* name, int value) {
		for (int i = 0; i < ICOUNT; i++)
			if (!strcmp(iprops[i], name))
				ivalues[i] = value;
	}

	char* get(const char* name, const char* def) {
		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare(db, "select value from \"prefs\" where name = ?;", -1, &stmt, 0))
			return NULL;

		sqlite3_bind_text(stmt, 1, name, strlen(name),  SQLITE_TRANSIENT);
		const char* val = SQLITE_ROW == sqlite3_step(stmt) ? (char*)sqlite3_column_text(stmt, 0) : def;
		char* value = (char*)calloc (strlen(val) + 1, sizeof(char));
		strcpy(value, val);

		sqlite3_finalize(stmt);
		return value;
	}

	bool set(const char* name, char* value) {
		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare(db, "replace into \"prefs\" (name, value) values (?1, ?2);", -1, &stmt, 0))
			return false;

		sqlite3_bind_text(stmt, 1, name, strlen(name),  SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, value, strlen(value),  SQLITE_TRANSIENT);

		bool res = SQLITE_OK == sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		return res;
	}

	sqlite3* load(char* path) {
		if (SQLITE_OK != sqlite3_open(path, &db))
			return NULL;

		char sql8[] = "" \
			"begin;\n" \
			"create table if not exists prefs (name text not null, value text, primary key (name));" \
			"create table if not exists workflows (id integer primary key autoincrement, name text unique not null, category text, dbpath text, args text, description text, onerror text);" \
			"create table if not exists workflow_nodes (id integer primary key autoincrement, workflow_id integer, name text, type integer, data text, x integer, y integer, is_stop_on_error integer default 1);" \
			"create table if not exists workflow_links (id integer primary key autoincrement, workflow_id integer, from_id integer, port_no integer, to_id integer);" \
			"create table if not exists workflow_sessions (id integer primary key autoincrement, workflow_id integer, dbpath text, start_time integer, end_time integer, parent_sid integer, status text, error text, nodes json, args text);" \
			"create table if not exists workflow_vars (sid integer not null, var text not null, value text, primary key(sid, var));" \
			"create table if not exists workflow_globals (id integer primary key autoincrement, var text not null, value text);" \
			"create table if not exists workflow_session_details (id integer primary key autoincrement, sid integer, node_id integer, args text, start_time real, end_time real, error text, data text, status text);" \
			"create trigger if not exists trg_workflows after delete on workflows begin delete from workflow_nodes where workflow_id = OLD.id; end;" \
			"create trigger if not exists trg_workflow_nodes after delete on workflow_nodes begin delete from workflow_links where from_id = OLD.id or to_id = OLD.id; end;" \
			"commit;" \
			"pragma synchronous = 0;";

		if (SQLITE_OK != sqlite3_exec(db, sql8, 0, 0, 0))
			return NULL;

		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare(db, "select name, value from prefs where value GLOB '*[0-9]*'", -1, &stmt, 0)) {
			sqlite3_finalize(stmt);
			return NULL;
		}

		while(sqlite3_step(stmt) == SQLITE_ROW)
			set((char*) sqlite3_column_text(stmt, 0), atoi((char*) sqlite3_column_text(stmt, 1)));

		sqlite3_finalize(stmt);
		return db;
	}

	bool save() {
		sqlite3_stmt* stmt;
		if (SQLITE_OK != sqlite3_prepare(db, "replace into 'prefs' (name, value) values (?1, ?2);", -1, &stmt, 0))
			return false;

		for(int i = 0; i < ICOUNT; i++) {
			sqlite3_bind_text(stmt, 1, iprops[i], strlen(iprops[i]),  SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, ivalues[i]);
			sqlite3_step(stmt);
			sqlite3_reset(stmt);
		}

		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return true;
	}
}
