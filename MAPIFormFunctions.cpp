// MAPIFormfunctions.cpp : Collection of useful MAPI functions

#include "stdafx.h"
#include "MAPIFormFunctions.h"
#include "MyMAPIFormViewer.h"

// This function creates a new message of class szMessageClass, based in m_lpContainer
// The function will also take care of launching the form

// This function can be used to create a new message using any form.
// Outlook's default IPM.Note and IPM.Post can be created in any folder, so these don't pose a problem.
// Appointment, Contact, StickyNote, and Task can only be created in those folders
// Attempting to create one of those in the Inbox will result in an
// 'Internal Application Error' when you save.
_Check_return_ HRESULT CreateAndDisplayNewMailInFolder(
	_In_ HWND hwndParent,
	_In_ LPMDB lpMDB,
	_In_ LPMAPISESSION lpMAPISession,
	_In_ CContentsTableListCtrl *lpContentsTableListCtrl,
	int iItem,
	_In_ wstring szMessageClass,
	_In_ LPMAPIFOLDER lpFolder)
{
	auto hRes = S_OK;
	LPMAPIFORMMGR lpMAPIFormMgr = nullptr;

	if (!lpFolder || !lpMAPISession) return MAPI_E_INVALID_PARAMETER;

	EC_H_MSG(MAPIOpenFormMgr(lpMAPISession, &lpMAPIFormMgr), IDS_NOFORMMANAGER);

	if (!lpMAPIFormMgr) return hRes;

	LPMAPIFORMINFO lpMAPIFormInfo = nullptr;
	LPPERSISTMESSAGE lpPersistMessage = nullptr;

	EC_H_MSG(lpMAPIFormMgr->ResolveMessageClass(
		wstringTostring(szMessageClass).c_str(), // class
		0, // flags
		lpFolder, // folder to resolve to
		&lpMAPIFormInfo),
		IDS_NOCLASSHANDLER);
	if (lpMAPIFormInfo)
	{
		EC_MAPI(lpMAPIFormMgr->CreateForm(
			reinterpret_cast<ULONG_PTR>(hwndParent), // parent window
			MAPI_DIALOG, // display status window
			lpMAPIFormInfo, // form info
			IID_IPersistMessage, // riid to open
			reinterpret_cast<LPVOID *>(&lpPersistMessage))); // form to open into

		if (lpPersistMessage)
		{
			LPMESSAGE lpMessage = nullptr;
			// Get a message
			EC_MAPI(lpFolder->CreateMessage(
				NULL, // default interface
				0, // flags
				&lpMessage));
			if (lpMessage)
			{
				auto lpMAPIFormViewer = new CMyMAPIFormViewer(
					hwndParent,
					lpMDB,
					lpMAPISession,
					lpFolder,
					lpMessage,
					lpContentsTableListCtrl,
					iItem);

				if (lpMAPIFormViewer)
				{
					// put everything together with the default info
					EC_MAPI(lpPersistMessage->InitNew(
						static_cast<LPMAPIMESSAGESITE>(lpMAPIFormViewer),
						lpMessage));

					LPMAPIFORM lpForm = nullptr;
					EC_MAPI(lpPersistMessage->QueryInterface(IID_IMAPIForm, reinterpret_cast<LPVOID*>(&lpForm)));

					if (lpForm)
					{
						EC_MAPI(lpForm->SetViewContext(
							static_cast<LPMAPIVIEWCONTEXT>(lpMAPIFormViewer)));

						EC_MAPI(lpMAPIFormViewer->CallDoVerb(
							lpForm,
							EXCHIVERB_OPEN,
							NULL)); // Not passing a RECT here so we'll try to use the default for the form
						lpForm->Release();
					}

					lpMAPIFormViewer->Release();
				}

				lpMessage->Release();
			}

			lpPersistMessage->Release();
		}

		lpMAPIFormInfo->Release();
	}

	lpMAPIFormMgr->Release();
	return hRes;
}

_Check_return_ HRESULT OpenMessageNonModal(
	_In_ HWND hwndParent,
	_In_ LPMDB lpMDB,
	_In_ LPMAPISESSION lpMAPISession,
	_In_ LPMAPIFOLDER lpSourceFolder,
	_In_ CContentsTableListCtrl *lpContentsTableListCtrl,
	int iItem,
	_In_ LPMESSAGE lpMessage,
	LONG lVerb,
	_In_opt_ LPCRECT lpRect)
{
	auto hRes = S_OK;
	ULONG cValuesShow = 0;
	LPSPropValue lpspvaShow = nullptr;
	ULONG ulMessageStatus = NULL;
	LPMAPIVIEWCONTEXT lpViewContextTemp = nullptr;

	enum
	{
		FLAGS,
		CLASS,
		EID,
		NUM_COLS
	};
	static const SizedSPropTagArray(NUM_COLS, sptaShowForm) =
	{
	NUM_COLS,
	PR_MESSAGE_FLAGS,
	PR_MESSAGE_CLASS_A,
	PR_ENTRYID
	};

	if (!lpMessage || !lpMAPISession || !lpSourceFolder) return MAPI_E_INVALID_PARAMETER;

	// Get required properties from the message
	EC_H_GETPROPS(lpMessage->GetProps(
		LPSPropTagArray(&sptaShowForm), // property tag array
		fMapiUnicode, // flags
		&cValuesShow, // Count of values returned
		&lpspvaShow)); // Values returned

	if (lpspvaShow)
	{
		EC_MAPI(lpSourceFolder->GetMessageStatus(
			lpspvaShow[EID].Value.bin.cb,
			reinterpret_cast<LPENTRYID>(lpspvaShow[EID].Value.bin.lpb),
			0,
			&ulMessageStatus));

		auto lpMAPIFormViewer = new CMyMAPIFormViewer(
			hwndParent,
			lpMDB,
			lpMAPISession,
			lpSourceFolder,
			lpMessage,
			lpContentsTableListCtrl,
			iItem);

		if (lpMAPIFormViewer)
		{
			LPMAPIFORMMGR lpMAPIFormMgr = nullptr;
			LPMAPIFORM lpForm = nullptr;

			EC_MAPI(lpMAPIFormViewer->GetFormManager(&lpMAPIFormMgr));

			if (lpMAPIFormMgr)
			{
				DebugPrint(DBGFormViewer, L"Calling LoadForm: szMessageClass = %hs, ulMessageStatus = 0x%X, ulMessageFlags = 0x%X\n",
					lpspvaShow[CLASS].Value.lpszA,
					ulMessageStatus,
					lpspvaShow[FLAGS].Value.ul);
				EC_MAPI(lpMAPIFormMgr->LoadForm(
					reinterpret_cast<ULONG_PTR>(hwndParent),
					0, // flags
					lpspvaShow[CLASS].Value.lpszA,
					ulMessageStatus,
					lpspvaShow[FLAGS].Value.ul,
					lpSourceFolder,
					lpMAPIFormViewer,
					lpMessage,
					lpMAPIFormViewer,
					IID_IMAPIForm, // riid
					reinterpret_cast<LPVOID *>(&lpForm)));
				lpMAPIFormMgr->Release();
				lpMAPIFormMgr = nullptr;
			}

			if (lpForm)
			{
				EC_MAPI(lpMAPIFormViewer->CallDoVerb(
					lpForm,
					lVerb,
					lpRect));
				// Fix for unknown typed freedocs.
				WC_MAPI(lpForm->GetViewContext(&lpViewContextTemp));
				if (SUCCEEDED(hRes)) {
					if (lpViewContextTemp) {
						// If we got a pointer back, we'll just release it and continue.
						lpViewContextTemp->Release();
					}
					else {
						// If the pointer came back NULL, then we need to call ShutdownForm but don't release.
						WC_MAPI(lpForm->ShutdownForm(SAVEOPTS_NOSAVE));
					}
				}
				else
				{
					// Not getting a view context isn't a bad thing
					hRes = S_OK;
				}

				lpForm->Release();
			}
			lpMAPIFormViewer->Release();
		}

		MAPIFreeBuffer(lpspvaShow);
	}
	return hRes;
}

_Check_return_ HRESULT OpenMessageModal(_In_ LPMAPIFOLDER lpParentFolder,
	_In_ LPMAPISESSION lpMAPISession,
	_In_ LPMDB lpMDB,
	_In_ LPMESSAGE lpMessage)
{
	auto hRes = S_OK;
	ULONG cValuesShow;
	LPSPropValue lpspvaShow = nullptr;
	ULONG_PTR Token = NULL;
	ULONG ulMessageStatus = NULL;

	enum
	{
		FLAGS,
		CLASS,
		ACCESS,
		EID,
		NUM_COLS
	};
	static const SizedSPropTagArray(NUM_COLS, sptaShowForm) =
	{
	NUM_COLS,
	PR_MESSAGE_FLAGS,
	PR_MESSAGE_CLASS_A,
	PR_ACCESS,
	PR_ENTRYID
	};

	if (!lpMessage || !lpParentFolder || !lpMAPISession || !lpMDB) return MAPI_E_INVALID_PARAMETER;

	// Get required properties from the message
	EC_H_GETPROPS(lpMessage->GetProps(
		LPSPropTagArray(&sptaShowForm), // property tag array
		fMapiUnicode, // flags
		&cValuesShow, // Count of values returned
		&lpspvaShow)); // Values returned

	if (lpspvaShow)
	{
		EC_MAPI(lpParentFolder->GetMessageStatus(
			lpspvaShow[EID].Value.bin.cb,
			reinterpret_cast<LPENTRYID>(lpspvaShow[EID].Value.bin.lpb),
			0,
			&ulMessageStatus));

		// set up the 'display message' form
		EC_MAPI(lpMAPISession->PrepareForm(
			NULL, // default interface
			lpMessage, // message to open
			&Token)); // basically, the pointer to the form

		EC_H_CANCEL(lpMAPISession->ShowForm(
			NULL,
			lpMDB, // message store
			lpParentFolder, // parent folder
			NULL, // default interface
			Token, // token?
			NULL, // reserved
			MAPI_POST_MESSAGE, // flags
			ulMessageStatus, // message status
			lpspvaShow[FLAGS].Value.ul, // message flags
			lpspvaShow[ACCESS].Value.ul, // access
			lpspvaShow[CLASS].Value.lpszA)); // message class
	}

	MAPIFreeBuffer(lpspvaShow);
	return hRes;
}