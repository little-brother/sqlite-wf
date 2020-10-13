#ifndef __DIALOGS_H__
#define __DIALOGS_H__

#include<windows.h>

namespace dialogs {
	BOOL CALLBACK cbDlgWorkflowAddEdit (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL CALLBACK cbDlgWorkflowOpen (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL CALLBACK cbDlgWorkflowNode (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL CALLBACK cbDlgWorkflowRun (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	BOOL CALLBACK cbDlgJournal (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	LRESULT CALLBACK cbNewWorkflowNodeEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
}

#endif
