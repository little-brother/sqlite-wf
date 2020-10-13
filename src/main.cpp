#define _WIN32_WINNT 0x0501

#include "uthash.h"
#include "global.h"
#include "utils.h"
#include "prefs.h"
#include "dialogs.h"
#include "session.h"
#include "resource.h"

HBITMAP hNodeIcons [] = {0,
	(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_NODE_SQL), IMAGE_BITMAP, 64, 64, LR_DEFAULTCOLOR),
	(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_NODE_VAR), IMAGE_BITMAP, 64, 64, LR_DEFAULTCOLOR),
	(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_NODE_IF), IMAGE_BITMAP, 64, 64, LR_DEFAULTCOLOR),
	(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_NODE_SUB), IMAGE_BITMAP, 64, 64, LR_DEFAULTCOLOR)
};
HFONT hDefFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
POINT cursor = {0, 0};
int cursorMode = MODE_NONE;
HWND hToolbarWnd, hMainWnd;

struct TLink {
	int id;
	int from_id;
	int port_no;
	int to_id;
};
TLink links[MAX_LINK_COUNT] = {0};

struct TNode {
    int id;
    int type;
    HWND hWnd;
    TCHAR name[MAX_NAME_LENGTH];
    int portCount;
    bool isFlip;
    int x;
    int y;
    UT_hash_handle hh; /* makes this structure hashable */
};
struct TNode *nodes = NULL;

TNode* findNode(int id) {
	TNode* node;
	HASH_FIND_INT(nodes, &id, node);
	return node;
}

TNode* createNode(int id, int type, int portCount, int x, int y, const TCHAR* name) {
	HWND hNodeWnd = CreateWindow(WC_STATIC, NULL, WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_NOTIFY | WS_CLIPSIBLINGS, x, y, NODE_SIZE, NODE_SIZE, hMainWnd, (HMENU)0, GetModuleHandle(0), 0);
	SetWindowLong(hNodeWnd, GWL_USERDATA, MAKELPARAM(id, type));
	SendMessage(hNodeWnd, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)hNodeIcons[type]);

	TNode* node = (TNode*)malloc(sizeof(TNode));
	node->id = id;
	node->hWnd = hNodeWnd;
	node->portCount = portCount;
	node->type = type;
	node->x = x;
	node->y = y;
	_tcsncpy(node->name, name, MAX_NAME_LENGTH - 1);
	HASH_ADD_INT(nodes, id, node);

	return node;
}

LRESULT CALLBACK cbMainWnd (HWND, UINT, WPARAM, LPARAM);
int cli();

TCHAR APP_BUFFER[MAX_TEXT_LENGTH]{0};
TCHAR APP_PATH[MAX_PATH]{0};
sqlite3* db = NULL;

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	bool isCLI = strlen(lpCmdLine);

	GetModuleFileName(0, APP_PATH, MAX_PATH);
	PathRemoveFileSpec(APP_PATH);
	TCHAR prefPath16[MAX_PATH] = {0};
	_stprintf(prefPath16, TEXT("%s/prefs.sqlite"), APP_PATH);
	char* prefPath8 = utils::utf16to8(prefPath16);

	TCHAR searchPath[MAX_PATH]{0};
	_stprintf(searchPath, TEXT("%s\\extensions\\*.dll"), APP_PATH);
	if (!(db = prefs::load(prefPath8))) {
		if (isCLI)
			printf("Settings loading failed");
		else
			MessageBox(0, TEXT("Settings loading failed"), TEXT("Error"), MB_OK);
		return EXIT_FAILURE;
	}
	free(prefPath8);

	// load extensions
	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFile(searchPath, &ffd);
	if (hFind != INVALID_HANDLE_VALUE) {
		sqlite3_enable_load_extension(db, true);
		do {
			TCHAR file16[MAX_PATH]{0};
			_stprintf(file16, TEXT("%s/extensions/%s"), APP_PATH, ffd.cFileName);
			char* file8 = utils::utf16to8(file16);
			if (SQLITE_OK == sqlite3_load_extension(db, file8, NULL, NULL));
			free(file8);
		} while ((FindNextFile(hFind, &ffd)));
		FindClose(hFind);
	}

	if (isCLI)
		return cli();

	WNDCLASSEX wc;

	wc.hInstance = hInstance;
	wc.lpszClassName = TEXT("sqlite-wf-class");
	wc.lpfnWndProc = cbMainWnd;
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.cbSize = sizeof (WNDCLASSEX);
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LOGO));
	wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LOGO));
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.lpszMenuName = NULL;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

	if (!RegisterClassEx (&wc))
		return 0;

	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_DATE_CLASSES;
	InitCommonControlsEx(&icex);

	LoadLibrary(TEXT("msftedit.dll"));

	hMainWnd = CreateWindowEx (0, TEXT("sqlite-wf-class"), NULL, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		prefs::get("wf-x"), prefs::get("wf-y"), prefs::get("wf-width"), prefs::get("wf-height"), HWND_DESKTOP, NULL, hInstance, NULL);

	TBBUTTON tbButtons [] = {
		{0, IDM_NEW, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("New...")},
		{1, IDM_OPEN, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Open")},
		{2, IDM_EDIT, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Edit")},
		{3, IDM_DELETE, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Delete")},
		{-1, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0L, 0},
		{4, IDM_ADD_SQL, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("SQL")},
		{5, IDM_ADD_VAR, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Vars")},
		{6, IDM_ADD_IF, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Router")},
		{7, IDM_ADD_SUB, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Subflow")},
		{-1, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0L, 0},
		{8, IDM_RUN, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Run")},
		{9, IDM_JOURNAL, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Journal")},
		{-1, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0L, 0},
		{10, IDM_HELP, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Help")}
	};

	hToolbarWnd = CreateToolbarEx (hMainWnd, WS_CHILD |  WS_BORDER | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_LIST, IDC_TOOLBAR, 0, NULL, 0,
		tbButtons, sizeof(tbButtons)/sizeof(tbButtons[0]), 0, 0, 0, 0, sizeof (TBBUTTON));

	HIMAGELIST tbImages = ImageList_LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_TOOLBAR), 16, 0, RGB(255,255,255));
	SendMessage(hToolbarWnd, TB_SETIMAGELIST, 0, (LPARAM)tbImages);

	SendMessage(hMainWnd, WMU_WORKFLOW_LOAD, prefs::get("wf-last-workflow-id"), 0);

	ShowWindow (hMainWnd, nCmdShow);

	MSG msg;
	while (GetMessage (&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

LRESULT CALLBACK cbMainWnd (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_EXITSIZEMOVE: {
			RECT rc;
			GetWindowRect(hWnd, &rc);
			prefs::set("wf-x", rc.left);
			prefs::set("wf-y", rc.top);
			prefs::set("wf-width", rc.right - rc.left);
			prefs::set("wf-height", rc.bottom - rc.top);
		}
		break;

		case WM_DESTROY: {
			if (!prefs::save())
				MessageBox(0, TEXT("Settings saving failed"), TEXT("Error"), MB_OK);

			RemoveProp(hWnd, TEXT("CURRENT-NODE"));
			RemoveProp(hWnd, TEXT("DRAWING-LINK"));
			PostQuitMessage (0);
		}
		break;

		case WM_KEYDOWN: {
			if (wParam == VK_ESCAPE)
				SendMessage(hWnd, WM_CLOSE, 0, 0);

			if (wParam == VK_DELETE)
				SendMessage(hWnd, WMU_NODE_DELETE, 0, 0);
		}
		break;

		case WM_SIZE: {
			SendMessage(GetDlgItem(hWnd, IDC_TOOLBAR), WM_SIZE, 0, 0);
		}
		break;

		case WM_COMMAND: {
			if (wParam == IDM_NEW || wParam == IDM_EDIT) {
				bool isNew = wParam == IDM_NEW;
				int id = DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_WORKFLOW_ADDEDIT), hWnd, (DLGPROC)dialogs::cbDlgWorkflowAddEdit, isNew ? 0 : GetWindowLong(hWnd, GWL_USERDATA));
				if (id != DLG_CANCEL) {
					if (isNew && id)
						SendMessage(hWnd, WMU_WORKFLOW_LOAD, id, 0);

					if (!isNew && id)
						SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 0, 0);
				}
			}

			if (wParam == IDM_OPEN) {
				int id = DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_WORKFLOW_OPEN), hWnd, (DLGPROC)dialogs::cbDlgWorkflowOpen, GetWindowLong(hWnd, GWL_USERDATA));
				if (id != DLG_CANCEL)
					SendMessage(hWnd, WMU_WORKFLOW_LOAD, id, 0);
			}

			if (wParam == IDM_DELETE && MessageBox(hWnd, TEXT("Are you sure you want to delete the workflow?"), TEXT("Delete confirmation"), MB_OKCANCEL) == IDOK) {
				sqlite3_stmt *stmt;
				bool rc = SQLITE_OK == sqlite3_prepare_v2(db, "delete from workflows where id = ?1; ", -1, &stmt, 0);
				if (rc) {
					sqlite3_bind_int(stmt, 1, GetWindowLong(hWnd, GWL_USERDATA));
					rc = SQLITE_DONE == sqlite3_step(stmt);
				}
				sqlite3_finalize(stmt);

				if (rc)
					SendMessage(hWnd, WMU_WORKFLOW_LOAD, 0, 0);
				else
					showDbError();
			}

			if (wParam == IDM_ADD_SQL || wParam == IDM_ADD_VAR || wParam == IDM_ADD_IF || wParam == IDM_ADD_SUB) {
				int type = wParam == IDM_ADD_SQL ? NODE_SQL : wParam == IDM_ADD_VAR ? NODE_VAR : wParam == IDM_ADD_IF ? NODE_IF : NODE_SUB;
				int x  = 100;
				int y = 100;

				int id = GetWindowLong(hWnd, GWL_USERDATA);
				bool rc = false;
				sqlite3_stmt *stmt;
				rc = SQLITE_OK == sqlite3_prepare_v2(db, "insert into workflow_nodes (workflow_id, type, x, y) values (?1, ?2, ?3, ?4)", -1, &stmt, 0);
				if (rc) {
					sqlite3_bind_int(stmt, 1, id);
					sqlite3_bind_int(stmt, 2, type);
					sqlite3_bind_int(stmt, 3, x);
					sqlite3_bind_int(stmt, 4, y);

					rc = SQLITE_DONE == sqlite3_step(stmt);
				}
				sqlite3_finalize(stmt);

				if (rc) {
					int id = sqlite3_last_insert_rowid(db);
					createNode(id, type, type != NODE_IF, x, y, TEXT(""));
				} else {
					showDbError();
				}
			}

			if (wParam == IDM_RUN) {
				int workflow_id = GetWindowLong(hWnd, GWL_USERDATA);
				int rc = DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_WORKFLOW_RUN), hWnd, (DLGPROC)dialogs::cbDlgWorkflowRun, workflow_id);
				if (rc != -1)
					MessageBox(0, rc ? L"DONE" : L"FAIL", 0, 0);
			}

			if (wParam == IDM_JOURNAL) {
				int workflow_id = GetWindowLong(hWnd, GWL_USERDATA);
				DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_JOURNAL), hWnd, (DLGPROC)dialogs::cbDlgJournal, workflow_id);
			}

			if (wParam == IDM_HELP)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-wf/wiki"), 0, 0, SW_SHOW);

			if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
				EndDialog(hWnd, DLG_CANCEL);

			int cmd = HIWORD(wParam);
			if ((cmd == STN_CLICKED || cmd == STN_DBLCLK) && ((HWND)lParam != hToolbarWnd)) {
				HWND hPrevWnd = (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
				HWND hNodeWnd = (HWND)lParam;
				bool isNode = hNodeWnd && (GetParent(hNodeWnd) == hWnd) && (hNodeWnd != GetDlgItem(hWnd, IDC_TOOLBAR));

				if (isNode && (cmd == STN_CLICKED)) {
					POINT p{0};
					GetCursorPos(&p);
					ScreenToClient(hNodeWnd, &p);

					if (hPrevWnd == hNodeWnd && (NODE_SIZE - p.x < 10) && (p.y < 10))
						SendMessage(hWnd, WMU_NODE_DELETE, (WPARAM)hNodeWnd, 0);

					cursor = {p.x, p.y};
					SetCapture(hWnd);
				}

				if (isNode && (cmd == STN_DBLCLK)) {
					LONG data = GetWindowLong(hNodeWnd, GWL_USERDATA);
					if(DLG_OK == DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_WORKFLOW_NODE), hWnd, (DLGPROC)dialogs::cbDlgWorkflowNode, data)) {
						int id = LOWORD(data);
						int type = HIWORD(data);

						TNode* node = findNode(id);
						_tcsncpy(node->name, APP_BUFFER, MAX_NAME_LENGTH - 1);

						int portCount = 1;
						if (type == NODE_IF) {
							sqlite3_stmt* stmt;
							char query8[] = "select json_array_length(coalesce(data, '[]')) from workflow_nodes n where n.id = ?1";
							if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
								sqlite3_bind_int(stmt, 1, id);
								sqlite3_step(stmt);
								portCount = sqlite3_column_int(stmt, 0);
							}
						}
						node->portCount = portCount;
						for (int linkNo = 0; linkNo < MAX_LINK_COUNT; linkNo++) {
							TLink* link = &links[linkNo];
							if (link->from_id == id && link->port_no >= portCount)
								link->id = 0;
						}
					}
				}

				SetProp(hWnd, TEXT("CURRENT-NODE"), isNode ? hNodeWnd : 0);
				cursorMode = isNode && (cmd == STN_CLICKED) ? MODE_MOVE_NODE : MODE_NONE;
				SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 0, 0);
			}
		}
		break;

		case WM_LBUTTONDOWN: {
			HWND hNodeWnd = (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
			if (hNodeWnd) {
				POINT p = {LOWORD(lParam), HIWORD(lParam)};
				RECT rc{0};
				GetWindowRect(hNodeWnd, &rc);
				POINT p2 = {rc.left, rc.top};
				ScreenToClient(hWnd, &p2);

				int id = LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA));
				TNode* node = findNode(id);

				int portNo = -1;
				int r = CX_LINK_HANDLE_RADIUS;

				for (int i = 0; i < node->portCount; i++) {
					RECT rcOut = {
						p2.x + (node->isFlip ? - 2 * r : NODE_SIZE),
						p2.y + (i + 1) * NODE_SIZE / (node->portCount + 1) - r,
						p2.x + (node-> isFlip ? 0 : NODE_SIZE + 2 * r),
						p2.y + (i + 1) * NODE_SIZE / (node->portCount + 1) + r
					};

					if (PtInRect(&rcOut, p))
						portNo = i;
				}

				// Touch the node's link out
				if (portNo != -1) {
					cursorMode = MODE_DRAW_LINK;
					SetCapture(hWnd);

					int linkNo;
					for (linkNo = 0; links[linkNo].id != 0 && linkNo < MAX_LINK_COUNT; linkNo++);

					TLink* link = &links[linkNo];
					link->id = -1;
					link->from_id = LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA));
					link->port_no = portNo;
					link->to_id = 0;

					SetProp(hWnd, TEXT("DRAWING-LINK"), (HANDLE)linkNo);
				} else {
					bool isFound = false;
					int nodeId = LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA));
					for (int i = 0; i < MAX_LINK_COUNT; i++) {
						TLink* link = &links[i];
						if (!link->id || link->from_id != nodeId)
							continue;

						TNode* node = findNode(link->to_id);

						RECT rcIn = {
							node->x - (node->isFlip ? NODE_SIZE : 2 * r),
							node->y + NODE_SIZE / 2 - 2 * r,
							node->x + (node->isFlip ? NODE_SIZE + 2 * r : 0),
							node->y + NODE_SIZE / 2 + 2 * r
						};

						if (PtInRect(&rcIn, p)) {
							int rc = SendMessage(hWnd, WMU_LINK_DELETE, link->id, 0);
							if (!rc)
								showDbError();
							isFound = true;
						}
					}

					if (!isFound) {
						SetProp(hWnd, TEXT("CURRENT-NODE"), 0);
						SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 0, 0);
					}
				}
			}
		}
		break;

		case WM_MOUSEMOVE: {
			if ((cursorMode == MODE_MOVE_NODE) && (wParam == MK_LBUTTON)) {
				POINT p = {GET_X_LPARAM(lParam) - cursor.x, GET_Y_LPARAM(lParam) - cursor.y};
				HWND hNodeWnd = (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
				SetWindowPos(hNodeWnd, HWND_TOP, p.x, p.y, 0, 0, SWP_NOSIZE);
				int id = LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA));

				TNode *node = findNode(id);
				node->x = p.x;
				node->y = p.y;

				for (int i = 0; i < MAX_LINK_COUNT; i++) {
					if((links[i].from_id == id) || (links[i].to_id == id)) {
						SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 1, 0);
						break;
					}
				}
			}

			if (cursorMode == MODE_DRAW_LINK && wParam == MK_LBUTTON)
				SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 0, 0);
		}
		break;

		case WM_LBUTTONUP: {
			if (cursorMode == MODE_MOVE_NODE) {
				HWND hNodeWnd = (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
				HWND data = (HWND)GetWindowLong(hNodeWnd, GWL_USERDATA);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "update workflow_nodes set x = ?1, y = ?2 where id = ?3", -1, &stmt, 0)) {
					sqlite3_bind_int(stmt, 1, GET_X_LPARAM(lParam) - cursor.x);
					sqlite3_bind_int(stmt, 2, GET_Y_LPARAM(lParam) - cursor.y);
					sqlite3_bind_int(stmt, 3, LOWORD(data));
					if (SQLITE_DONE != sqlite3_step(stmt))
						showDbError();
				}
				sqlite3_finalize(stmt);

				ReleaseCapture();
				cursorMode = MODE_NONE;
			}

			if (cursorMode == MODE_DRAW_LINK) {
				POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				ClientToScreen(hWnd, &p);
				HWND hNodeWnd = WindowFromPoint(p);
				TLink* link = &links[(int)GetProp(hWnd, TEXT("DRAWING-LINK"))];

				int rc = hNodeWnd && (hNodeWnd != hWnd) && (hNodeWnd != hToolbarWnd);
				link->to_id = rc ? LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA)) : 0;
				link->to_id = rc ? LOWORD(GetWindowLong(hNodeWnd, GWL_USERDATA)) : 0;

				for (int i = 0; rc && i < MAX_LINK_COUNT; i++)
					rc = !(links[i].id != link->id && links[i].from_id == link->from_id && links[i].to_id == link->to_id);

				rc = rc && (link->from_id != link->to_id);

				if (rc) {
					sqlite3_stmt *stmt;
					rc = SQLITE_OK == sqlite3_prepare_v2(db, "insert into workflow_links (workflow_id, from_id, port_no, to_id) values (?1, ?2, ?3, ?4)", -1, &stmt, 0);
					if (rc) {
						sqlite3_bind_int(stmt, 1, GetWindowLong(hWnd, GWL_USERDATA));
						sqlite3_bind_int(stmt, 2, link->from_id);
						sqlite3_bind_int(stmt, 3, link->port_no);
						sqlite3_bind_int(stmt, 4, link->to_id);

						rc = SQLITE_DONE == sqlite3_step(stmt);
					}
					sqlite3_finalize(stmt);

					if (!rc)
						showDbError();
				}

				link->id = rc ? sqlite3_last_insert_rowid(db) : 0;

				ReleaseCapture();
				cursorMode = MODE_NONE;
				SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 1, 0);
			}
		}
		break;

		case WM_ERASEBKGND: {
			RECT rc{0};
			GetClientRect(hWnd, &rc);
			HBRUSH hBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
			FillRect((HDC)wParam, &rc, hBrush);
			DeleteObject(hBrush);
			if (GetWindowLong(hWnd, GWL_USERDATA)) {
				rc.top = 30;
				FillRect((HDC)wParam, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
			}
			return 1;
		}
		break;

		case WM_PAINT: {
			InvalidateRect(hWnd, NULL, true);

			PAINTSTRUCT ps{0};
			ps.fErase = true;
			HDC hDC = BeginPaint(hWnd, &ps);
			HFONT hOldFont = (HFONT)SelectObject(hDC, hDefFont);
			SetTextColor(hDC, GetSysColor(COLOR_BTNTEXT));

			// Links
			HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
			SelectObject(hDC, hPen);
			for (int i = 0; i < MAX_LINK_COUNT; i++) {
				TLink* link = &links[i];
				if (!link->id)
					continue;

				TNode *from = findNode(link->from_id);
				TNode *to = findNode(link->to_id);

				POINT a = {
					from->x + NODE_SIZE * !from->isFlip,
					from->y + (link->port_no + 1) * NODE_SIZE / (from->portCount + 1)
				};
				POINT b;
				if (!to) {
					GetCursorPos(&b);
					ScreenToClient(hWnd, &b);
				} else
					b = {
						to->x + NODE_SIZE * to->isFlip,
						to->y + NODE_SIZE / 2
					};

				int af = from->isFlip ? -1 : 1;
				int bf = to && to->isFlip ? -1 : 1;

				if (abs(a.y - b.y) < NODE_SIZE) {
					MoveToEx(hDC, a.x, a.y, NULL);
					LineTo(hDC, b.x, b.y);
					utils::drawArrow(hDC, a, b);
				} else {
					MoveToEx(hDC, a.x, a.y, NULL);
					LineTo(hDC, a.x + af * CX_LINK_INDENT, a.y);

					POINT points[4] = {0};
					points[0] = {a.x + af * CX_LINK_INDENT, a.y};
					points[1] = {a.x + af * CX_LINK_INDENT * 4, a.y};
					points[2] = {b.x - bf * CX_LINK_INDENT * 4, b.y};
					points[3] = {b.x - bf* CX_LINK_INDENT, b.y};
					PolyBezier(hDC, points, 4);

					MoveToEx(hDC, b.x - bf * CX_LINK_INDENT, b.y, NULL);
					LineTo(hDC, b.x, b.y);

					utils::drawBezierArrow(hDC, points);
				}
			}
			DeleteObject(hPen);

			// Current node attributes
			HWND hCurrWnd = (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
			if (hCurrWnd) {
				int data = GetWindowLong(hCurrWnd, GWL_USERDATA);
				int id = LOWORD(data);
				TNode* node = findNode(id);

				RECT rc{0};
				GetWindowRect(hCurrWnd, &rc);
				POINT p = {rc.left, rc.top};
				ScreenToClient(hWnd, &p);

				HPEN hPen = CreatePen(PS_DOT, 1, node->isFlip ? RGB(255, 0, 0) : RGB(128, 128, 128));
				SelectObject(hDC, hPen);
				Rectangle(hDC, p.x - 5, p.y - 5,  p.x + rc.right - rc.left + 5, p.y + rc.bottom - rc.top + 5);
				DeleteObject(hPen);

				hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

				HBRUSH hOutBrush = CreateSolidBrush(RGB(0, 0, 255));
				HBRUSH hInBrush = CreateSolidBrush(RGB(255, 0, 0));
				int r = CX_LINK_HANDLE_RADIUS;

				SelectObject(hDC, hPen);
				SelectObject(hDC, hOutBrush);
				for (int i = 0; i < node->portCount; i++) {
					POINT c = {
						p.x + (node->isFlip ? - r : (NODE_SIZE + r)),
						p.y + (i + 1) * NODE_SIZE / (node->portCount + 1)
					};
					Ellipse(hDC, c.x  - r, c.y - r, c.x + r, c.y + r);
				}

				SelectObject(hDC, hInBrush);
				for (int i = 0; i < MAX_LINK_COUNT; i++) {
					TLink* link = &links[i];
					if (link->id <= 0 || link->from_id != id)
						continue;

					TNode* to = findNode(link->to_id);
					POINT c = {
						to->x + (!to->isFlip ? - r : (NODE_SIZE + r)),
						to->y + NODE_SIZE / 2
					};
					Ellipse(hDC, c.x  - r, c.y - r, c.x + r, c.y + r);

					// Cross
					MoveToEx(hDC, c.x - r/2, c.y - r/2, NULL);
					LineTo(hDC, c.x + r/2 + 1, c.y + r/2 + 1);
					MoveToEx(hDC, c.x - r/2, c.y + r/2, NULL);
					LineTo(hDC, c.x + r/2 + 1, c.y - r/2 - 1);

				}

				DeleteObject(hPen);
				DeleteObject(hInBrush);
				DeleteObject(hOutBrush);
			}

			// Node captions
			TNode *node, *tmp;
			HASH_ITER(hh, nodes, node, tmp) {
				RECT rcText = {node->x - NODE_SIZE, node->y + NODE_SIZE + 5, node->x + 2 * NODE_SIZE, node->y + 2 * NODE_SIZE};
				DrawText(hDC, node->name, _tcslen(node->name), &rcText, DT_CENTER | DT_TOP | DT_WORDBREAK);
			}

			SelectObject(hDC, hOldFont);
			EndPaint(hWnd, &ps);

		}
		break;

		case WMU_WORKFLOW_LOAD: {
			int id = wParam;
			bool rc = false;

			SetProp(hWnd, TEXT("CURRENT-NODE"), 0);

			if (nodes) {
				TNode *node, *tmp;
				HASH_ITER(hh, nodes, node, tmp) {
					HASH_DEL(nodes, node);
					DestroyWindow(node->hWnd);
					free(node);
				}
				nodes = NULL;

				for (int i = 0; i < MAX_LINK_COUNT; i++)
					links[i].id = 0;
			}

			sqlite3_stmt *stmt;
			rc = SQLITE_OK == sqlite3_prepare_v2(db, "select name from workflows where id = ?1 limit 1", -1, &stmt, 0);
			if (rc) {
				sqlite3_bind_int(stmt, 1, id);
				rc = SQLITE_ROW == sqlite3_step(stmt);
				if (rc) {
					TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
					SetWindowText(hWnd, name16);
					free(name16);
				} else {
					SetWindowText(hWnd, TEXT("No workflow is selected"));
					id = 0;
				}
			}
			sqlite3_finalize(stmt);

			rc = rc && (SQLITE_OK == sqlite3_prepare_v2(db, "select id, type, iif(type == 3, json_array_length(coalesce(data, '[]')), 1) port_count, x, y, name from workflow_nodes where workflow_id = ?1", -1, &stmt, 0));
			if (rc) {
				sqlite3_bind_int(stmt, 1, id);
				while (SQLITE_ROW == sqlite3_step(stmt)) {
					TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 5));
					createNode(
						sqlite3_column_int(stmt, 0),
						sqlite3_column_int(stmt, 1),
						sqlite3_column_int(stmt, 2),
						sqlite3_column_int(stmt, 3),
						sqlite3_column_int(stmt, 4),
						name16);
					free(name16);
				}
			}
			rc && sqlite3_finalize(stmt);

			memset(links, 0, MAX_LINK_COUNT * sizeof(TLink));
			rc = rc && (SQLITE_OK == sqlite3_prepare_v2(db,
				"select id, from_id, to_id, port_no from workflow_links where workflow_id = ?1", -1, &stmt, 0));
			if (rc) {
				sqlite3_bind_int(stmt, 1, id);
				int linkNo = 0;
				while (SQLITE_ROW == sqlite3_step(stmt)) {
					TLink* link = &links[linkNo];
					link->id = sqlite3_column_int(stmt, 0);
					link->from_id = sqlite3_column_int(stmt, 1);
					link->to_id = sqlite3_column_int(stmt, 2);
					link->port_no = sqlite3_column_int(stmt, 3);
					linkNo++;
				}
			}
			rc && sqlite3_finalize(stmt);

			if ((rc && id) || !id) {
				prefs::set("wf-last-workflow-id", id);
				SetWindowLong(hWnd, GWL_USERDATA, id);
			}

			if (!rc && id)
				showDbError();

			int range [] = {IDM_EDIT, IDM_DELETE, IDM_RUN, IDM_JOURNAL,
				IDM_ADD_SQL, IDM_ADD_VAR, IDM_ADD_IF, IDM_ADD_SUB, };
			for (int idm : range) {
				TBBUTTONINFO tbi{0};
				tbi.cbSize = sizeof(TBBUTTONINFO);
				tbi.dwMask = TBIF_STATE;
				tbi.fsState = rc && id ? TBSTATE_ENABLED : TBSTATE_INDETERMINATE;
				SendMessage(hToolbarWnd, TB_SETBUTTONINFO, idm, (LPARAM)&tbi);
			}

			SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 1, 0);
			SetFocus(hWnd);
		}
		break;

		case WMU_WORKFLOW_REPAINT: {
			if (wParam && nodes) {
				TNode *node, *tmp;
				HASH_ITER(hh, nodes, node, tmp) {
					node->isFlip = false;
				}

				for (int i = 0; i < MAX_LINK_COUNT; i++) {
					TLink* link = &links[i];
					if (link->id < 1)
						continue;

					TNode* from = findNode(link->from_id);
					TNode* to = findNode(link->to_id);
					if (to && from)
						to->isFlip = to->isFlip || (to->type != NODE_IF && to->x < from->x);
				}
			}

			SendMessage(hWnd, WM_PAINT, 0, 0);
		}
		break;

		case WMU_NODE_DELETE: {
			HWND hNodeWnd = wParam ? (HWND)wParam : (HWND)GetProp(hWnd, TEXT("CURRENT-NODE"));
			sqlite3_stmt *stmt;
			if (SQLITE_OK == sqlite3_prepare_v2(db, "delete from workflow_nodes where id = ?1;", -1, &stmt, 0)) {
				int id = LOWORD((HWND)GetWindowLong(hNodeWnd, GWL_USERDATA));
				sqlite3_bind_int(stmt, 1, id);
				if (SQLITE_DONE == sqlite3_step(stmt)) {
					TNode* node = findNode(id);
					HASH_DEL(nodes, node);
					free(node);

					DestroyWindow(hNodeWnd);
					for (int i = 0; i < MAX_LINK_COUNT; i++) {
						TLink* link = &links[i];
						if (link->from_id == id || link->to_id == id)
							link->id = 0;
					}
				} else {
					showDbError();
				}
			}
			sqlite3_finalize(stmt);
			SetProp(hWnd, TEXT("CURRENT-NODE"), 0);
			SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 1, 0);
			return 1;
		}
		break;

		case WMU_LINK_DELETE: {
			int id = wParam;
			sqlite3_stmt *stmt;
			int rc = 0;
			rc = SQLITE_OK == sqlite3_prepare_v2(db, "delete from workflow_links where id = ?1;", -1, &stmt, 0);
			if (rc) {
				sqlite3_bind_int(stmt, 1, id);
				rc = SQLITE_DONE == sqlite3_step(stmt);
				if (rc) {
					int i = 0;
					for (; (i < MAX_LINK_COUNT) && (links[i].id != id); i++);
					if (i != MAX_LINK_COUNT - 1)
						links[i].id = 0;

					SendMessage(hWnd, WMU_WORKFLOW_REPAINT, 1, 0);
				}
			}
			sqlite3_finalize(stmt);

			return rc;
		}
		break;

		default:
			return DefWindowProc (hWnd, message, wParam, lParam);
	}

	return 0;
}

int cli() {
	DWORD written = 0;
	HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (stdOut == NULL || stdOut == INVALID_HANDLE_VALUE)
		return EXIT_FAILURE;

	AttachConsole(ATTACH_PARENT_PROCESS);
	int nArgs = 0;
	TCHAR** args = CommandLineToArgvW(GetCommandLine(), &nArgs);

	TCHAR msg[MAX_TEXT_LENGTH];
	if (nArgs < 3) {
		_stprintf(msg, TEXT("\nUsage: sqlite-wf <workflow name or id> <database path> <arg1> <arg2> ...\n"));
		WriteConsole(stdOut, msg, _tcslen(msg), &written, NULL);
		return EXIT_FAILURE;
	}

	bool rc = false;
	char* wf = utils::utf16to8(args[1]);
	char* dbpath = utils::utf16to8(args[2]);

	sqlite3_stmt *stmt;
	if(SQLITE_OK == sqlite3_prepare_v2(db, "select id from workflows where id = ?1 or lower(name) = lower(?1)", -1, &stmt, 0)) {
		sqlite3_bind_text(stmt, 1, wf, -1, SQLITE_TRANSIENT);
		if (SQLITE_ROW == sqlite3_step(stmt)) {
			char* wargs[nArgs - 3];
			for (int i = 3; i < nArgs; i++)
				wargs[i - 3] = utils::utf16to8(args[i]);

			rc = session::run(sqlite3_column_int(stmt, 0), dbpath, 0, nArgs - 3, (const char**)wargs);
			_stprintf(msg, TEXT("\n%s - %s\n"), args[1], rc ? TEXT("DONE") : TEXT("FAIL"));
			WriteConsole(stdOut, msg, _tcslen(msg), &written, NULL);

			for (int i = 3; i < nArgs; i++)
				free(wargs[i - 3]);
		}
	}
	sqlite3_finalize(stmt);
	free(wf);
	free(dbpath);
	LocalFree(args);

	return rc ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool showDbError() {
	TCHAR* err16 = utils::utf8to16(sqlite3_errmsg(db));
	MessageBox(0, err16, NULL, 0);
	free(err16);
	return true;
}

// A ListView with one column has broken sort. So, the first column is a row number
int setListViewData(HWND hListWnd, sqlite3_stmt *stmt) {
	int colCount = sqlite3_column_count(stmt);
	HWND hHeader = ListView_GetHeader(hListWnd);

	if (hHeader == NULL || Header_GetItemCount(hHeader) == 0) {
		for (int i = 0; i <= colCount; i++) {
			TCHAR* name16 = utils::utf8to16(i > 0 ? sqlite3_column_name(stmt, i - 1) : "rowno");
			LVCOLUMN lvc;
			lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.iSubItem = i;
			lvc.pszText = name16;
			lvc.cchTextMax = _tcslen(name16) + 1;
			lvc.cx = i > 0 ? 100 : 0;
			ListView_InsertColumn(hListWnd, i, &lvc);
			free(name16);
		}
	}

	ListView_DeleteAllItems(hListWnd);

	int rowNo = 0;
	while(sqlite3_step(stmt) == SQLITE_ROW) {
		if (rowNo == 0) {
			for (int i = 1; i <= colCount; i++) {
				int type = sqlite3_column_type(stmt, i - 1);
				LVCOLUMN lvc = {mask: LVCF_FMT};
				lvc.fmt = type == SQLITE_INTEGER || type == SQLITE_FLOAT ? LVCFMT_RIGHT : LVCFMT_LEFT;
				ListView_SetColumn(hListWnd, i, &lvc);
			}
		}

		TCHAR rowNo16[64];
		_stprintf(rowNo16, TEXT("%i"), rowNo + 1);

		LVITEM lvi = {0};
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.iSubItem = 0;
		lvi.iItem = rowNo;
		lvi.pszText = rowNo16;
		lvi.cchTextMax = _tcslen(rowNo16) + 1;
		lvi.lParam = rowNo;
		ListView_InsertItem(hListWnd, &lvi);

		for (int i = 1; i <= colCount; i++) {
			TCHAR* value16 = utils::utf8to16((char *)sqlite3_column_text(stmt, i - 1));

			lvi.iSubItem = i;
			lvi.mask = LVIF_TEXT;
			lvi.pszText = value16;
			lvi.cchTextMax = _tcslen(value16);

			ListView_SetItem(hListWnd, &lvi);
			free(value16);
		}

		rowNo++;
	}

	if (SQLITE_DONE != sqlite3_errcode(db))
		showDbError();

	ListView_SetExtendedListViewStyle(hListWnd, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_AUTOSIZECOLUMNS | LVS_EX_LABELTIP);
	for (int i = 1; i <= colCount; i++) {
		ListView_SetColumnWidth(hListWnd, i, LVSCW_AUTOSIZE_USEHEADER);
		if (ListView_GetColumnWidth(hListWnd, i) > 200)
			ListView_SetColumnWidth(hListWnd, i, 200);
	}

	return rowNo;
}

