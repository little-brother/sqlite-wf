#include <stdio.h>
#include "sqlite3.h"
#include "global.h"
#include "resource.h"
#include "utils.h"
#include "dialogs.h"
#include "session.h"

namespace dialogs {
	HFONT hDefFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	BOOL CALLBACK cbDlgWorkflowAddEdit (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				int id = lParam;
				SetWindowLong(hWnd, GWL_USERDATA, id);
				if (id) {
					SetWindowText(hWnd, TEXT("Edit workflow"));
					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, "select id, name, category, description, onerror from workflows where id = ?1 limit 1", -1, &stmt, 0)) {
						sqlite3_bind_int(stmt, 1, id);
						if (SQLITE_ROW == sqlite3_step(stmt)) {
							TCHAR* id16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
							TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 1));
							TCHAR* category16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 2));
							TCHAR* desc16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 3));
							TCHAR* onerror16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 4));

							SetWindowText(GetDlgItem(hWnd, IDC_DLG_WORKFLOW_ID), id16);
							SetWindowText(GetDlgItem(hWnd, IDC_DLG_WORKFLOW_NAME), name16);
							SetWindowText(GetDlgItem(hWnd, IDC_DLG_WORKFLOW_CATEGORY), category16);
							SetWindowText(GetDlgItem(hWnd, IDC_DLG_WORKFLOW_DESC), desc16);
							SetWindowText(GetDlgItem(hWnd, IDC_DLG_WORKFLOW_ONERROR), onerror16);

							free(id16);
							free(name16);
							free(category16);
							free(desc16);
							free(onerror16);
						}
					}
					sqlite3_finalize(stmt);
				}

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select distinct category from workflows where length(category) > 0", -1, &stmt, 0)) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_CATEGORY);
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* category16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hListWnd, category16);
						free(category16);
					}
				}
				sqlite3_finalize(stmt);
				SetFocus(hWnd);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					int id = GetWindowLong(hWnd, GWL_USERDATA);

					bool rc = false;
					sqlite3_stmt *stmt;
					rc = SQLITE_OK == sqlite3_prepare_v2(db, "replace into workflows (id, name, category, description, onerror) values (?1, ?2, ?3, ?4, ?5)", -1, &stmt, 0);
					if (rc) {
						if (id)
							sqlite3_bind_int(stmt, 1, id);
						else
							sqlite3_bind_null(stmt, 1);


						int colNo = 2;
						int range[] = {IDC_DLG_WORKFLOW_NAME, IDC_DLG_WORKFLOW_CATEGORY, IDC_DLG_WORKFLOW_DESC, IDC_DLG_WORKFLOW_ONERROR};
						for (int idc : range) {
							TCHAR text16[MAX_TEXT_LENGTH] = {0};
							GetDlgItemText(hWnd, idc, text16, MAX_TEXT_LENGTH);
							char* text8 = utils::utf16to8(text16);
							sqlite3_bind_text(stmt, colNo, text8, strlen(text8), SQLITE_TRANSIENT);
							free(text8);
							colNo++;
						}

						rc = SQLITE_DONE == sqlite3_step(stmt);
						if (rc && !id)
							id = sqlite3_last_insert_rowid(db);
					}
					sqlite3_finalize(stmt);

					if (rc)
						EndDialog(hWnd, id);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgWorkflowOpen (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowLong(hWnd, GWL_USERDATA, lParam);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select distinct category from workflows where length(category) > 0", -1, &stmt, 0)) {
					HWND hCategoryWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_CATEGORY);
					ComboBox_AddString(hCategoryWnd, TEXT("All"));
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* category16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hCategoryWnd, category16);
						free(category16);
					}
					ComboBox_SetCurSel(hCategoryWnd, 0);
				}
				sqlite3_finalize(stmt);

				SendMessage(hWnd, WMU_WORKFLOW_UPDATE_LIST, 0, 0);
				SetFocus(hWnd);
			}
			break;

			case WM_COMMAND: {
				if (LOWORD(wParam) == IDC_DLG_WORKFLOW_CATEGORY && HIWORD(wParam) == CBN_SELCHANGE)
					SendMessage(hWnd, WMU_WORKFLOW_UPDATE_LIST, 0, 0);

				if (LOWORD(wParam) == IDC_DLG_WORKFLOW_LIST && HIWORD(wParam) == LBN_DBLCLK) {
					SendMessage(hWnd, WM_COMMAND, IDC_DLG_OK, 0);
					return 1;
				}

				if (wParam == IDC_DLG_OK) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_LIST);
					int pos = ListBox_GetCurSel(hListWnd);
					if (pos == -1)
						return false;

					TCHAR name16[MAX_NAME_LENGTH] = {0};
					ListBox_GetText(hListWnd, pos, name16);
					int id = 0;
					sqlite3_stmt *stmt;
					int rc = SQLITE_OK == sqlite3_prepare_v2(db, "select id from workflows where name = ?1 limit 1", -1, &stmt, 0);
					if (rc) {
						char* name8 = utils::utf16to8(name16);
						sqlite3_bind_text(stmt, 1, name8, -1, SQLITE_TRANSIENT);
						free(name8);
						if (SQLITE_ROW == sqlite3_step(stmt))
							id = sqlite3_column_int(stmt, 0);
					}
					sqlite3_finalize(stmt);

					if (rc)
						EndDialog(hWnd, id);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WMU_WORKFLOW_UPDATE_LIST: {
				int cur = GetWindowLong(hWnd, GWL_USERDATA);
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_LIST);
				ListBox_ResetContent(hListWnd);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name, id from workflows where coalesce(?1, category, 'NULL') = coalesce(category, 'NULL') order by 1", -1, &stmt, 0)) {
					TCHAR category16[255 + 1] = {0};
					GetDlgItemText(hWnd, IDC_DLG_WORKFLOW_CATEGORY, category16, 255);
					if (_tcscmp(category16, TEXT("All"))) {
						char* category8 = utils::utf16to8(category16);
						sqlite3_bind_text(stmt, 1, category8, strlen(category8), SQLITE_TRANSIENT);
						free(category8);
					} else {
						sqlite3_bind_null(stmt, 1);
					}

					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						int pos = ListBox_AddString(hListWnd, name16);
						free(name16);

						if (sqlite3_column_int(stmt, 1) == cur)
							ListBox_SetCurSel(hListWnd, pos);
					}
				}
				sqlite3_finalize(stmt);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}


	WNDPROC cbOldWorkflowNodeEdit;
	LRESULT CALLBACK cbNewWorkflowNodeEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_GETDLGCODE)
			return (DLGC_WANTALLKEYS | CallWindowProc(cbOldWorkflowNodeEdit, hWnd, msg, wParam, lParam));

		switch(msg){
			case WM_KILLFOCUS: {
				SendMessage(hWnd, WM_KEYDOWN, VK_RETURN, 0);
				DestroyWindow(hWnd);
			}
			break;

			case WM_KEYDOWN: {
				if (wParam == VK_RETURN) {
					HWND hListWnd = GetParent(hWnd);
					int size = GetWindowTextLength(hWnd);
					TCHAR value16[size + 1]{0};
					GetWindowText(hWnd, value16, size + 1);

					int data = GetWindowLong(hWnd, GWL_USERDATA);
					int rowNo = LOWORD(data);
					int colNo = HIWORD(data);
					ListView_SetItemText(hListWnd, rowNo, colNo, value16);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}

				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;
		}

		return CallWindowProc(cbOldWorkflowNodeEdit, hWnd, msg, wParam, lParam);
	}

	BOOL CALLBACK cbDlgWorkflowNode (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				int id = LOWORD(lParam);
				int type = HIWORD(lParam);
				SetWindowLong(hWnd, GWL_USERDATA, lParam);

				sqlite3_stmt *stmt;
				int rc = SQLITE_OK == sqlite3_prepare_v2(db, "select name, data, is_stop_on_error from workflow_nodes where id = ?1 limit 1", -1, &stmt, 0);
				if (rc) {
					sqlite3_bind_int(stmt, 1, id);
					if (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						SetDlgItemText(hWnd, IDC_DLG_NODE_NAME, name16);
						free(name16);

						if (type == NODE_SQL) {
							TCHAR* data16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 1));
							SetDlgItemText(hWnd, IDC_DLG_NODE_SQL, data16);
							free(data16);
						}

						HWND hStopOnErrorWnd = GetDlgItem(hWnd, IDC_DLG_NODE_ON_ERROR);
						Button_SetCheck(hStopOnErrorWnd, type == NODE_IF || sqlite3_column_int(stmt, 2) ? BST_CHECKED : BST_UNCHECKED);
						EnableWindow(hStopOnErrorWnd, type != NODE_IF);
					}
				}
				sqlite3_finalize(stmt);

				if (!rc) {
					showDbError();
					return 0;
				}

				if (type == NODE_SQL) {
					SetWindowText(hWnd, TEXT("SQL"));
					SetDlgItemText(hWnd, IDC_DLG_NODE_LABEL, TEXT("SQL statement"));
					HWND hSQLWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SQL);
					ShowWindow(hSQLWnd, SW_SHOW);
				}

				if (type == NODE_VAR || type == NODE_IF) {
					SetWindowText(hWnd, type == NODE_VAR ? TEXT("VARIABLE") : TEXT("ROUTER"));
					SetDlgItemText(hWnd, IDC_DLG_NODE_LABEL, type == NODE_VAR ? TEXT("Variables") : TEXT("Routing"));
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_NODE_VARIF);
					ShowWindow(hListWnd, SW_SHOW);

					char var8[] = "select json_extract(value, '$[0]') 'variable', json_extract(value, '$[1]') 'value or expression' from workflow_nodes n, json_each(coalesce(data, '[]')) where n.id = ?1";
					char if8[] = "select json_extract(value, '$[0]') 'name', json_extract(value, '$[1]') 'condition' from workflow_nodes n, json_each(coalesce(data, '[]')) where n.id = ?1";
					int rc = SQLITE_OK == sqlite3_prepare_v2(db, type == NODE_VAR ? var8 : if8, -1, &stmt, 0);
					if (rc) {
						sqlite3_bind_int(stmt, 1, id);
						setListViewData(hListWnd, stmt);
						ListView_SetColumnWidth(hListWnd, 1, 125);
						ListView_SetColumnWidth(hListWnd, 2, 200);
					}
					sqlite3_finalize(stmt);
				}

				if (type == NODE_SUB) {
					SetWindowText(hWnd, TEXT("SUBFLOW"));
					SetDlgItemText(hWnd, IDC_DLG_NODE_LABEL, TEXT("Subflow"));

					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SUB);
					ShowWindow(hListWnd, SW_SHOW);

					ShowWindow(GetDlgItem(hWnd, IDC_DLG_NODE_SUB_LABEL), SW_SHOW);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_NODE_SUB_MODE), SW_SHOW);

					HWND hArgsWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SUB_ARGS);
					ShowWindow(hArgsWnd, SW_SHOW);

					rc = (SQLITE_OK == sqlite3_prepare_v2(db, "select name, id from workflows order by 1", -1, &stmt, 0));
					while (rc && (SQLITE_ROW == sqlite3_step(stmt))) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						int pos = ComboBox_AddString(hListWnd, name16);
						free(name16);

						int id = sqlite3_column_int(stmt, 1);
						ComboBox_SetItemData(hListWnd, pos, id);
					}
					rc && sqlite3_finalize(stmt);

					rc = rc && (SQLITE_OK == sqlite3_prepare_v2(db,
						"select json_extract(coalesce(data, '{}'), '$.workflow_id'),  json_extract(coalesce(data, '{}'), '$.args'), json_extract(coalesce(data, '{}'), '$.is_parallel') " \
						"from workflow_nodes n where n.id = ?1 limit 1", -1, &stmt, 0));
					rc && sqlite3_bind_int(stmt, 1, id);
					if (rc && (SQLITE_ROW == sqlite3_step(stmt))) {
						// ComboBox_FindItemData searchs by an item string, not the data
						int workflow_id = sqlite3_column_int(stmt, 0);
						for (int i = 0; workflow_id && i < ComboBox_GetCount(hListWnd); i++) {
							if (ComboBox_GetItemData(hListWnd, i) != workflow_id)
								continue;
							ComboBox_SetCurSel(hListWnd, i);
						}

						TCHAR* args16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 1));
						SetDlgItemText(hWnd, IDC_DLG_NODE_SUB_ARGS, args16);
						free(args16);

						HWND hModeWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SUB_MODE);
						Button_SetCheck(hModeWnd, sqlite3_column_int(stmt, 2) ? BST_CHECKED : BST_UNCHECKED);
					}
					rc && sqlite3_finalize(stmt);
				}

				if (!rc)
					showDbError();

				TCHAR tid[64] = {0};
				_stprintf(tid, TEXT("%i"), id);
				SetDlgItemText(hWnd, IDC_DLG_NODE_ID, tid);
				SetFocus(hWnd);
			}

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					int data = GetWindowLong(hWnd, GWL_USERDATA);
					int id = LOWORD(data);
					int type = HIWORD(data);

					bool rc = false;
					sqlite3_exec(db, "create table if not exists temp.tmp_node_data (id integer primary key autoincrement, key, value, unique(key)); delete from temp.tmp_node_data;", 0, 0, 0);

					sqlite3_stmt *stmt;
					rc = SQLITE_OK == sqlite3_prepare_v2(db, "insert into temp.tmp_node_data (key, value) values (?1, ?2)", -1, &stmt, 0);
					if (rc && type == NODE_SQL) {
						TCHAR data16[MAX_TEXT_LENGTH] = {0};
						GetDlgItemText(hWnd, IDC_DLG_NODE_SQL, data16, MAX_TEXT_LENGTH);
						char* data8 = utils::utf16to8(data16);
						sqlite3_bind_text(stmt, 1, "sql", 3, SQLITE_TRANSIENT);
						sqlite3_bind_text(stmt, 2, data8, strlen(data8), SQLITE_TRANSIENT);
						rc = SQLITE_DONE == sqlite3_step(stmt);
						free(data8);
					}

					if (rc && type == NODE_SUB) {
						HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SUB);
						int pos = ComboBox_GetCurSel(hListWnd);
						int workflow_id = SendMessage(hListWnd, CB_GETITEMDATA, pos, 0);//ComboBox_GetItemData(hListWnd, pos);
						sqlite3_bind_text(stmt, 1, "workflow_id", strlen("workflow_id"), SQLITE_TRANSIENT);
						sqlite3_bind_int(stmt, 2, workflow_id);
						rc = SQLITE_DONE == sqlite3_step(stmt);
						sqlite3_reset(stmt);

						HWND hModeWnd = GetDlgItem(hWnd, IDC_DLG_NODE_SUB_MODE);
						bool is_parallel = Button_GetCheck(hModeWnd) == BST_CHECKED;
						sqlite3_bind_text(stmt, 1, "is_parallel", strlen("is_parallel"), SQLITE_TRANSIENT);
						sqlite3_bind_int(stmt, 2, is_parallel);
						rc = SQLITE_DONE == sqlite3_step(stmt);
						sqlite3_reset(stmt);

						TCHAR args16[MAX_TEXT_LENGTH] = {0};
						GetDlgItemText(hWnd, IDC_DLG_NODE_SUB_ARGS, args16, MAX_TEXT_LENGTH);
						char* args8 = utils::utf16to8(args16);
						sqlite3_bind_text(stmt, 1, "args", 4, SQLITE_TRANSIENT);
						sqlite3_bind_text(stmt, 2, args8, strlen(args8), SQLITE_TRANSIENT);
						rc = SQLITE_DONE == sqlite3_step(stmt);
						free(args8);
					}

					int portCount = 0;
					if (rc && (type == NODE_IF || type == NODE_VAR)) {
						HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_NODE_VARIF);

						for (int rowNo = 0; rc && rowNo < ListView_GetItemCount(hListWnd); rowNo++) {
							TCHAR buf1[MAX_NAME_LENGTH] = {0}, buf2[MAX_NAME_LENGTH] = {0};
							ListView_GetItemText(hListWnd, rowNo, 1, buf1, MAX_NAME_LENGTH);
							ListView_GetItemText(hListWnd, rowNo, 2, buf2, MAX_NAME_LENGTH);
							if (_tcslen(buf1) + _tcslen(buf2) == 0)
								continue;

							if (type == NODE_VAR)
								_tcsupr(buf1);

							char* buf1_8 = utils::utf16to8(buf1);
							char* buf2_8 = utils::utf16to8(buf2);
							sqlite3_bind_text(stmt, 1, buf1_8, strlen(buf1_8), SQLITE_TRANSIENT);
							sqlite3_bind_text(stmt, 2, buf2_8, strlen(buf2_8), SQLITE_TRANSIENT);
							rc = SQLITE_DONE == sqlite3_step(stmt);
							sqlite3_reset(stmt);
							free(buf1_8);
							free(buf2_8);

							portCount++;
						}
					}
					sqlite3_finalize(stmt);

					char query[2000];
					sprintf(query, "update workflow_nodes set name = ?2, is_stop_on_error = ?3, data = (%s) where id = ?1",
						type == NODE_SQL ? "select value from temp.tmp_node_data limit 1" :
						type == NODE_SUB ? "select json_group_object(key, value) from temp.tmp_node_data" :
						"select json_group_array(val) from (select json_array(key, value) val from temp.tmp_node_data order by id)");


					rc = SQLITE_OK == sqlite3_prepare_v2(db, query, -1, &stmt, 0);
					if (rc) {
						sqlite3_bind_int(stmt, 1, id);

						TCHAR name16[MAX_TEXT_LENGTH] = {0};
						GetDlgItemText(hWnd, IDC_DLG_NODE_NAME, name16, MAX_TEXT_LENGTH);
						char* name8 = utils::utf16to8(name16);
						sqlite3_bind_text(stmt, 2, name8, strlen(name8), SQLITE_TRANSIENT);
						free(name8);

						sqlite3_bind_int(stmt, 3, IsDlgButtonChecked(hWnd, IDC_DLG_NODE_ON_ERROR) == BST_CHECKED);

						rc = SQLITE_DONE == sqlite3_step(stmt);
						if (rc)
							_stprintf(APP_BUFFER, TEXT("%s"), name16);
					}
					sqlite3_finalize(stmt);

					if (rc && type == NODE_IF) {
						char query8[512] = {0};
						sprintf(query8, "delete from workflow_links where from_id = %i and port_no >= %i", id, portCount);
						sqlite3_exec(db, query8, 0, 0, 0);
					}

					if (!rc) {
						showDbError();
						return 0;
					}

					EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_NODE_VARIF);

				if (pHdr->code == (DWORD)NM_DBLCLK && pHdr->hwndFrom == hListWnd) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					if (ia->iSubItem == 0)
						return true;

					if (ia->iItem == -1) {
						int rowCount = ListView_GetItemCount(hListWnd);

						TCHAR buf1[2] = {0}, buf2[2] = {0};
						if (rowCount > 0) {
							ListView_GetItemText(hListWnd, rowCount - 1, 1, buf1, 2);
							ListView_GetItemText(hListWnd, rowCount - 1, 2, buf2, 2);
						}

						if (rowCount == 0 || (rowCount > 0 && _tcslen(buf1) + _tcslen(buf2) > 0)) {
							LVITEM lvi = {0};
							lvi.mask = LVIF_TEXT | LVIF_PARAM;
							lvi.iSubItem = 0;
							lvi.iItem = rowCount;
							lvi.pszText = 0;
							lvi.cchTextMax = 1;
							lvi.lParam = rowCount;
							ListView_InsertItem(hListWnd, &lvi);

							ia->iItem = rowCount;
						} else {
							ia->iItem = rowCount - 1;
						}
					}

					RECT rect;
					ListView_GetSubItemRect(hListWnd, ia->iItem, ia->iSubItem, LVIR_BOUNDS, &rect);
					int h = rect.bottom - rect.top;
					int w = ListView_GetColumnWidth(hListWnd, ia->iSubItem);

					TCHAR buf[MAX_TEXT_LENGTH];
					ListView_GetItemText(hListWnd, ia->iItem, ia->iSubItem, buf, MAX_TEXT_LENGTH);

					HWND hEdit = CreateWindowEx(0, WC_EDIT, buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rect.left, rect.top, w, h, hListWnd, 0, GetModuleHandle(NULL), NULL);
					SetWindowLong(hEdit, GWL_USERDATA, MAKELPARAM(ia->iItem, ia->iSubItem));
					int end = GetWindowTextLength(hEdit);
					SendMessage(hEdit, EM_SETSEL, end, end);
					SendMessage(hEdit, WM_SETFONT, (LPARAM)hDefFont, true);
					SetFocus(hEdit);

					cbOldWorkflowNodeEdit = (WNDPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LONG)cbNewWorkflowNodeEdit);
				}

				if (pHdr->code == LVN_KEYDOWN && pHdr->hwndFrom == hListWnd) {
					NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
					if (kd->wVKey == VK_DELETE) {
						int rowNo = SendMessage(hListWnd, LVM_GETNEXTITEM, -1, LVNI_SELECTED | LVNI_FOCUSED);
						ListView_DeleteItem(hListWnd, rowNo);
					}
				}
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgWorkflowRun (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				int workflow_id = lParam;
				SetWindowLong(hWnd, GWL_USERDATA, workflow_id);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select dbpath, args from workflows where id = ?1", -1, &stmt, 0)) {
					sqlite3_bind_int(stmt, 1, workflow_id);
					if (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* dbpath = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
						SetDlgItemText(hWnd, IDC_DLG_DBPATH, dbpath);
						free(dbpath);

						TCHAR* args = utils::utf8to16((const char*)sqlite3_column_text(stmt, 1));
						SetDlgItemText(hWnd, IDC_DLG_ARGS, args);
						free(args);
					}
				}
				sqlite3_finalize(stmt);

				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_DBPATH);
				ComboBox_AddString(hListWnd, TEXT(":memory:"));
				if (SQLITE_OK == sqlite3_prepare_v2(db,
					"select distinct dbpath from workflow_sessions where length(trim(coalesce(dbpath, ''))) > 0 and dbpath <> ':memory:' order by id desc limit 20",
					-1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* dbpath = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hListWnd, dbpath);
						free(dbpath);
					}
				}
				sqlite3_finalize(stmt);

				SetFocus(GetDlgItem(hWnd, IDC_DLG_OK));
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OPEN_DB) {
					TCHAR path[MAX_PATH + 1];
					if (utils::openFile(path, TEXT("Databases (*.sqlite, *.sqlite3, *.db)\0*.sqlite;.sqlite3;.id\0All\0*.*\0")))
						SetDlgItemText(hWnd, IDC_DLG_DBPATH, path);
				}

				if (wParam == IDC_DLG_OK) {
					int workflow_id = GetWindowLong(hWnd, GWL_USERDATA);
					TCHAR dbpath16[MAX_PATH + 1];
					GetDlgItemText(hWnd, IDC_DLG_DBPATH, dbpath16, MAX_PATH);
					if (!_tcslen(dbpath16)) {
						MessageBox(hWnd, TEXT("You should specify the database or at least choose :memory:"), NULL, 0);
						return false;
					}

					char* dbpath8 = utils::utf16to8(dbpath16);
					TCHAR cmd16[MAX_TEXT_LENGTH + 1];
					GetDlgItemText(hWnd, IDC_DLG_ARGS, cmd16, MAX_TEXT_LENGTH);

					char* cmd8 = utils::utf16to8(cmd16);
					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, "update workflows set dbpath = ?1, args = ?2 where id = ?3", -1, &stmt, 0)) {
						sqlite3_bind_text(stmt, 1, dbpath8, -1, SQLITE_TRANSIENT);
						sqlite3_bind_text(stmt, 2, cmd8, -1, SQLITE_TRANSIENT);
						sqlite3_bind_int(stmt, 3, workflow_id);
						sqlite3_step(stmt);
					}
					sqlite3_finalize(stmt);
					free(cmd8);

					int nArgs = 0;
					TCHAR** args16 = CommandLineToArgvW(cmd16, &nArgs);
					char* args8[nArgs];
					nArgs = _tcslen(cmd16) ? nArgs : 0;
					for (int i = 0; i < nArgs; i++)
						args8[i] = utils::utf16to8(args16[i]);
					LocalFree(args16);

					bool rc = session::run(workflow_id, dbpath8, 0, nArgs, (const char**)args8);
					free(dbpath8);
					for (int i = 0; i < nArgs; i++)
						free(args8[i]);

					EndDialog(hWnd, rc);
				}
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgJournalDetails (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hJournalWnd = (HWND)lParam;
				int pos = ListView_GetNextItem(hJournalWnd, -1, LVNI_SELECTED);
				TCHAR sid[32];
				ListView_GetItemText(hJournalWnd, pos, 1, sid, 33);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db,
						"select iif(n.name is null or length(n.name) = 0, '(Empty)', n.name) Name, "\
						"strftime('%d-%m-%Y %H:%M:%S', start_time, 'unixepoch', 'localtime') Start, strftime('%d-%m-%Y %H:%M:%S', end_time, 'unixepoch', 'localtime') End," \
						"end_time - start_time \"Elapsed, s\", status Status, error Error, d.data Data, replace(d.args, '\",\"', '\",' || x'0d' || x'0a' || '\"') Variables " \
						"from workflow_session_details d inner join workflow_nodes n on d.node_id = n.id "\
						"where d.sid = ?1 "\
						"order by d.id desc",
						-1, &stmt, 0)) {
					sqlite3_bind_int(stmt, 1, _ttoi(sid));
					setListViewData(GetDlgItem(hWnd, IDC_DLG_JOURNAL), stmt);
				}
				sqlite3_finalize(stmt);

				SetFocus(hWnd);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->idFrom == IDC_DLG_JOURNAL && pHdr->code == (DWORD)LVN_KEYDOWN) {
					NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
					if (kd->wVKey == 0x43 && GetKeyState(VK_CONTROL)) {// Ctrl + C
						// ToDo: copy rows to clipboard
					}
				}
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgJournal (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				int workflow_id = lParam;
				SetWindowLong(hWnd, GWL_USERDATA, lParam);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select id, name from workflows", -1, &stmt, 0)) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_NAME);
					ComboBox_AddString(hListWnd, TEXT("All"));
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 1));
						int pos = ComboBox_AddString(hListWnd, name16);
						free(name16);
						ComboBox_SetItemData(hListWnd, pos, sqlite3_column_int(stmt, 0));
					}

					for (int i = 0; workflow_id && i < ComboBox_GetCount(hListWnd); i++) {
						if (ComboBox_GetItemData(hListWnd, i) != workflow_id)
							continue;
						ComboBox_SetCurSel(hListWnd, i);
					}
				}
				sqlite3_finalize(stmt);
				SendMessage(hWnd, WMU_JOURNAL_UPDATE, 0, 0);
			}
			break;

			case WM_COMMAND: {
				if (LOWORD(wParam) == IDC_DLG_WORKFLOW_NAME && HIWORD(wParam) == CBN_SELCHANGE)
					SendMessage(hWnd, WMU_JOURNAL_UPDATE, 0, 0);

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->code == (DWORD)DTN_DATETIMECHANGE && (pHdr->idFrom == IDC_DLG_STARTDATE || pHdr->idFrom == IDC_DLG_ENDDATE))
					SendMessage(hWnd, WMU_JOURNAL_UPDATE, 0, 0);

				if (pHdr->code == (DWORD)NM_DBLCLK && pHdr->idFrom == IDC_DLG_JOURNAL)
					DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_JOURNAL_DETAILS), hWnd, (DLGPROC)cbDlgJournalDetails, (LPARAM)pHdr->hwndFrom);
			}
			break;

			case WMU_JOURNAL_UPDATE: {
				HWND hWfWnd = GetDlgItem(hWnd, IDC_DLG_WORKFLOW_NAME);
				int pos = ComboBox_GetCurSel(hWfWnd);
				int workflow_id = ComboBox_GetItemData(hWfWnd, pos);

				SYSTEMTIME tStart = {0}, tEnd = {0};
				DateTime_GetSystemtime(GetDlgItem(hWnd, IDC_DLG_STARTDATE), &tStart);
				DateTime_GetSystemtime(GetDlgItem(hWnd, IDC_DLG_ENDDATE), &tEnd);

				char startDate[32] = {0}, endDate[32] = {0};
				sprintf(startDate, "%i-%02i-%02i", tStart.wYear, tStart.wMonth, tStart.wDay);
				sprintf(endDate, "%i-%02i-%02i", tEnd.wYear, tEnd.wMonth, tEnd.wDay);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db,
						"select s.id SID, w.name Name, s.dbpath Database, "\
						"strftime('%d-%m-%Y %H:%M:%S', s.start_time, 'unixepoch', 'localtime') Start, strftime('%d-%m-%Y %H:%M:%S', s.end_time, 'unixepoch', 'localtime') End," \
						"s.end_time - s.start_time \"Elapsed, s\", iif(s.error is null, s.status, s.status || ' (' || s.error || ')') Status, parent_sid \"Parent SID\", s.args Arguments " \
						"from workflow_sessions s inner join workflows w on s.workflow_id = w.id "\
						"where w.id = iif(?1 = 0, w.id, ?1) and strftime('%s', ?2, 'utc') < s.start_time and s.end_time < strftime('%s', ?3, '+1 day', 'utc') "\
						"order by s.id desc",
						-1, &stmt, 0)) {
					sqlite3_bind_int(stmt, 1, workflow_id);
					sqlite3_bind_text(stmt, 2, startDate, -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(stmt, 3, endDate, -1, SQLITE_TRANSIENT);

					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_JOURNAL);
					setListViewData(hListWnd, stmt);
					SetFocus(hListWnd);
				}
				sqlite3_finalize(stmt);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}
}
