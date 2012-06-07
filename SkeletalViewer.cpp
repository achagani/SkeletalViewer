//------------------------------------------------------------------------------
// <copyright file="SkeletalViewer.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

// This module provides sample code used to demonstrate Kinect NUI processing

// Note: 
//     Platform SDK lib path should be added before the VC lib
//     path, because uuid.lib in VC lib path may be older

#include "stdafx.h"
#include <strsafe.h>
#include "SkeletalViewer.h"
#include "resource.h"

// Global Variables:
CSkeletalViewerApp  g_skeletalViewerApp;  // Application class

#define INSTANCE_MUTEX_NAME L"SkeletalViewerInstanceCheck"

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    MSG       msg;
    WNDCLASS  wc;
        
    // Store the instance handle
    g_skeletalViewerApp.m_hInstance = hInstance;

    // Dialog custom window class
    ZeroMemory(&wc,sizeof(wc));
    wc.style=CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra=DLGWINDOWEXTRA;
    wc.hInstance=hInstance;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hIcon=LoadIcon(hInstance,MAKEINTRESOURCE(IDI_SKELETALVIEWER));
    wc.lpfnWndProc=DefDlgProc;
    wc.lpszClassName=SZ_APPDLG_WINDOW_CLASS;
    if( !RegisterClass(&wc) )
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParam(
        hInstance,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC) CSkeletalViewerApp::MessageRouter, 
        reinterpret_cast<LPARAM>(&g_skeletalViewerApp));

    // unique mutex, if it already exists there is already an instance of this app running
    // in that case we want to show the user an error dialog
    HANDLE hMutex = CreateMutex(NULL, FALSE, INSTANCE_MUTEX_NAME);
    if ( (hMutex != NULL) && (GetLastError() == ERROR_ALREADY_EXISTS) ) 
    {
        //load the app title
        TCHAR szAppTitle[256] = { 0 };
        LoadStringW( hInstance, IDS_APPTITLE, szAppTitle, _countof(szAppTitle) );

        //load the error string
        TCHAR szRes[512] = { 0 };
        LoadStringW( hInstance, IDS_ERROR_APP_INSTANCE, szRes, _countof(szRes) );

        MessageBoxW( NULL, szRes, szAppTitle, MB_OK | MB_ICONHAND );

        CloseHandle(hMutex);
        return -1;
    }

    // Show window
    ShowWindow(hWndApp, nCmdShow);

    // Main message loop:
    while(GetMessage(&msg,NULL,0,0)) 
    {
        // If a dialog message will be taken care of by the dialog proc
        if ( (hWndApp != NULL) && IsDialogMessage(hWndApp, &msg) )
        {
            continue;
        }

        // otherwise do our window processing
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}

/// <summary>
/// Constructor
/// </summary>
CSkeletalViewerApp::CSkeletalViewerApp() : m_hInstance(NULL)
{
    ZeroMemory(m_szAppTitle, sizeof(m_szAppTitle));
    LoadStringW(m_hInstance, IDS_APPTITLE, m_szAppTitle, _countof(m_szAppTitle));

    m_fUpdatingUi = false;
    Nui_Zero();

    // Init Direct2D
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
}

/// <summary>
/// Destructor
/// </summary>
CSkeletalViewerApp::~CSkeletalViewerApp()
{
    // Clean up Direct2D
    SafeRelease(m_pD2DFactory);

    Nui_Zero();
    SysFreeString(m_instanceId);
}

/// <summary>
/// Clears the combo box for selecting active Kinect
/// </summary>
void CSkeletalViewerApp::ClearKinectComboBox()
{
    for (long i = 0; i < SendDlgItemMessage(m_hWnd, IDC_CAMERAS, CB_GETCOUNT, 0, 0); ++i)
    {
        SysFreeString( reinterpret_cast<BSTR>( SendDlgItemMessage(m_hWnd, IDC_CAMERAS, CB_GETITEMDATA, i, 0) ) );
    }
    SendDlgItemMessageW(m_hWnd, IDC_CAMERAS, CB_RESETCONTENT, 0, 0);
}

/// <summary>
/// Updates the combo box that lists Kinects available
/// </summary>
void CSkeletalViewerApp::UpdateKinectComboBox()
{
    m_fUpdatingUi = true;
    ClearKinectComboBox();

    int numDevices = 0;
    HRESULT hr = NuiGetSensorCount(&numDevices);

    if ( FAILED(hr) )
    {
        return;
    }

    long selectedIndex = 0;
    for (int i = 0; i < numDevices; ++i)
    {
        INuiSensor *pNui = NULL;
        HRESULT hr = NuiCreateSensorByIndex(i,  &pNui);
        if (SUCCEEDED(hr))
        {
            HRESULT status = pNui ? pNui->NuiStatus() : E_NUI_NOTCONNECTED;
            if (status == E_NUI_NOTCONNECTED)
            {
                pNui->Release();
                continue;
            }
            
            WCHAR kinectName[MAX_PATH];
            StringCchPrintfW(kinectName, _countof(kinectName), L"Kinect %d", i);
            long index = static_cast<long>( SendDlgItemMessage(m_hWnd, IDC_CAMERAS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kinectName)) );
            SendDlgItemMessageW( m_hWnd, IDC_CAMERAS, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(pNui->NuiUniqueId()) );
            if (m_pNuiSensor && pNui == m_pNuiSensor)
            {
                selectedIndex = index;
            }
            pNui->Release();
        }
    }

    SendDlgItemMessageW(m_hWnd, IDC_CAMERAS, CB_SETCURSEL, selectedIndex, 0);
    m_fUpdatingUi = false;
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSkeletalViewerApp::MessageRouter( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    CSkeletalViewerApp *pThis = NULL;
    
    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CSkeletalViewerApp*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        NuiSetDeviceStatusCallback( &CSkeletalViewerApp::Nui_StatusProcThunk, pThis );
    }
    else
    {
        pThis = reinterpret_cast<CSkeletalViewerApp*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (NULL != pThis)
    {
        return pThis->WndProc( hwnd, uMsg, wParam, lParam );
    }

    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSkeletalViewerApp::WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch(message)
    {
        case WM_INITDIALOG:
        {
            // Clean state the class
            Nui_Zero();

            // Bind application window handle
            m_hWnd = hWnd;

            // Set the font for Frames Per Second display
            LOGFONT lf;
            GetObject( (HFONT)GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf );
            lf.lfHeight *= 4;
            m_hFontFPS = CreateFontIndirect(&lf);
            SendDlgItemMessageW(hWnd, IDC_FPS, WM_SETFONT, (WPARAM)m_hFontFPS, 0);

            UpdateKinectComboBox();
            SendDlgItemMessageW(m_hWnd, IDC_CAMERAS, CB_SETCURSEL, 0, 0);

            TCHAR szComboText[512] = { 0 };

            // Fill combo box options for tracked skeletons

            LoadStringW(m_hInstance, IDS_TRACKEDSKELETONS_DEFAULT, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_TRACKEDSKELETONS_NEAREST1, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_TRACKEDSKELETONS_NEAREST2, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_TRACKEDSKELETONS_STICKY1, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_TRACKEDSKELETONS_STICKY2, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_SETCURSEL, 0, 0);
            // Fill combo box options for tracking mode

            LoadStringW(m_hInstance, IDS_TRACKINGMODE_DEFAULT, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKINGMODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_TRACKINGMODE_SEATED, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKINGMODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_TRACKINGMODE, CB_SETCURSEL, 0, 0);

            // Fill combo box options for range

            LoadStringW(m_hInstance, IDS_RANGE_DEFAULT, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_RANGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            LoadStringW(m_hInstance, IDS_RANGE_NEAR, szComboText, _countof(szComboText));
            SendDlgItemMessageW(m_hWnd, IDC_RANGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(szComboText));

            SendDlgItemMessageW(m_hWnd, IDC_RANGE, CB_SETCURSEL, 0, 0);
        }
        break;

        case WM_SHOWWINDOW:
        {
            // Initialize and start NUI processing
            Nui_Init();
        }
        break;

        case WM_USER_UPDATE_FPS:
        {
            ::SetDlgItemInt( m_hWnd, static_cast<int>(wParam), static_cast<int>(lParam), FALSE );
        }
        break;

        case WM_USER_UPDATE_COMBO:
        {
            UpdateKinectComboBox();
        }
        break;

        case WM_COMMAND:
        {
            if ( HIWORD(wParam) == CBN_SELCHANGE )
            {
                switch (LOWORD(wParam))
                {
                    case IDC_CAMERAS:
                    {
                        LRESULT index = ::SendDlgItemMessageW(m_hWnd, IDC_CAMERAS, CB_GETCURSEL, 0, 0);

                        // Don't reconnect as a result of updating the combo box
                        if ( !m_fUpdatingUi )
                        {
                            Nui_UnInit();
                            Nui_Zero();
                            Nui_Init(reinterpret_cast<BSTR>(::SendDlgItemMessageW(m_hWnd, IDC_CAMERAS, CB_GETITEMDATA, index, 0)));
                        }
                    }
                    break;

                    case IDC_TRACKEDSKELETONS:
                    {
                        LRESULT index = ::SendDlgItemMessageW(m_hWnd, IDC_TRACKEDSKELETONS, CB_GETCURSEL, 0, 0);
                        UpdateTrackedSkeletonSelection( static_cast<int>(index) );
                    }
                    break;

                    case IDC_TRACKINGMODE:
                    {
                        LRESULT index = ::SendDlgItemMessageW(m_hWnd, IDC_TRACKINGMODE, CB_GETCURSEL, 0, 0);
                        UpdateTrackingMode( static_cast<int>(index) );
                    }
                    break;
                    case IDC_RANGE:
                    {
                        LRESULT index = ::SendDlgItemMessageW(m_hWnd, IDC_RANGE, CB_GETCURSEL, 0, 0);
                        UpdateRange( static_cast<int>(index) );
                    }
                    break;
                }
            }
        }
        break;

        // If the titlebar X is clicked destroy app
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            // Uninitialize NUI
            Nui_UnInit();

            // Other cleanup
            ClearKinectComboBox();
            DeleteObject(m_hFontFPS);

            // Quit the main message pump
            PostQuitMessage(0);
            break;
    }

    return FALSE;
}

/// <summary>
/// Display a MessageBox with a string table table loaded string
/// </summary>
/// <param name="nID">id of string resource</param>
/// <param name="nType">type of message box</param>
/// <returns>result of MessageBox call</returns>
int CSkeletalViewerApp::MessageBoxResource( UINT nID, UINT nType )
{
    static TCHAR szRes[512];

    LoadStringW( m_hInstance, nID, szRes, _countof(szRes) );
    return MessageBoxW(m_hWnd, szRes, m_szAppTitle, nType);
}
