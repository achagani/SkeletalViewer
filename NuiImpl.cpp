//------------------------------------------------------------------------------
// <copyright file="NuiImpl.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

// Implementation of CSkeletalViewerApp methods dealing with NUI processing

#include "stdafx.h"
#include "SkeletalViewer.h"
#include "resource.h"
#include <mmsystem.h>
#include <assert.h>
#include <strsafe.h>

//lookups for color tinting based on player index
static const int g_IntensityShiftByPlayerR[] = { 1, 2, 0, 2, 0, 0, 2, 0 };
static const int g_IntensityShiftByPlayerG[] = { 1, 2, 2, 0, 2, 0, 0, 1 };
static const int g_IntensityShiftByPlayerB[] = { 1, 0, 2, 2, 0, 2, 0, 2 };

static const float g_JointThickness = 3.0f;
static const float g_TrackedBoneThickness = 6.0f;
static const float g_InferredBoneThickness = 1.0f;

const int g_BytesPerPixel = 4;

const int g_ScreenWidth = 320;
const int g_ScreenHeight = 240;


enum _SV_TRACKED_SKELETONS
{
    SV_TRACKED_SKELETONS_DEFAULT = 0,
    SV_TRACKED_SKELETONS_NEAREST1,
    SV_TRACKED_SKELETONS_NEAREST2,
    SV_TRACKED_SKELETONS_STICKY1,
    SV_TRACKED_SKELETONS_STICKY2
} SV_TRACKED_SKELETONS;

enum _SV_TRACKING_MODE
{
    SV_TRACKING_MODE_DEFAULT = 0,
    SV_TRACKING_MODE_SEATED
} SV_TRACKING_MODE;

enum _SV_RANGE
{
    SV_RANGE_DEFAULT = 0,
    SV_RANGE_NEAR,
} SV_RANGE;
/// <summary>
/// Zero out member variables
/// </summary>
void CSkeletalViewerApp::Nui_Zero()
{
    SafeRelease( m_pNuiSensor );

    m_pRenderTarget = NULL;
    m_pBrushJointTracked = NULL;
    m_pBrushJointInferred = NULL;
    m_pBrushBoneTracked = NULL;
    m_pBrushBoneInferred = NULL;
    ZeroMemory(m_Points,sizeof(m_Points));

    m_hNextDepthFrameEvent = NULL;
    m_hNextColorFrameEvent = NULL;
    m_hNextSkeletonEvent = NULL;
    m_pDepthStreamHandle = NULL;
    m_pVideoStreamHandle = NULL;
    m_hThNuiProcess = NULL;
    m_hEvNuiProcessStop = NULL;
    m_LastSkeletonFoundTime = 0;
    m_bScreenBlanked = false;
    m_DepthFramesTotal = 0;
    m_LastDepthFPStime = 0;
    m_LastDepthFramesTotal = 0;
    m_pDrawDepth = NULL;
    m_pDrawColor = NULL;
    m_TrackedSkeletons = 0;
    m_SkeletonTrackingFlags = NUI_SKELETON_TRACKING_FLAG_ENABLE_IN_NEAR_RANGE;
    m_DepthStreamFlags = 0;
    ZeroMemory(m_StickySkeletonIds,sizeof(m_StickySkeletonIds));
}

/// <summary>
/// Callback to handle Kinect status changes, redirects to the class callback handler
/// </summary>
/// <param name="hrStatus">current status</param>
/// <param name="instanceName">instance name of Kinect the status change is for</param>
/// <param name="uniqueDeviceName">unique device name of Kinect the status change is for</param>
/// <param name="pUserData">additional data</param>
void CALLBACK CSkeletalViewerApp::Nui_StatusProcThunk( HRESULT hrStatus, const OLECHAR* instanceName, const OLECHAR* uniqueDeviceName, void * pUserData )
{
    reinterpret_cast<CSkeletalViewerApp *>(pUserData)->Nui_StatusProc( hrStatus, instanceName, uniqueDeviceName );
}

/// <summary>
/// Callback to handle Kinect status changes
/// </summary>
/// <param name="hrStatus">current status</param>
/// <param name="instanceName">instance name of Kinect the status change is for</param>
/// <param name="uniqueDeviceName">unique device name of Kinect the status change is for</param>
void CALLBACK CSkeletalViewerApp::Nui_StatusProc( HRESULT hrStatus, const OLECHAR* instanceName, const OLECHAR* uniqueDeviceName )
{
    // Update UI
    PostMessageW( m_hWnd, WM_USER_UPDATE_COMBO, 0, 0 );

    if( SUCCEEDED(hrStatus) )
    {
        if ( S_OK == hrStatus )
        {
            if ( m_instanceId && 0 == wcscmp(instanceName, m_instanceId) )
            {
                Nui_Init(m_instanceId);
            }
            else if ( !m_pNuiSensor )
            {
                Nui_Init();
            }
        }
    }
    else
    {
        if ( m_instanceId && 0 == wcscmp(instanceName, m_instanceId) )
        {
            Nui_UnInit();
            Nui_Zero();
        }
    }
}

/// <summary>
/// Initialize Kinect by instance name
/// </summary>
/// <param name="instanceName">instance name of Kinect to initialize</param>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CSkeletalViewerApp::Nui_Init( OLECHAR *instanceName )
{
    // Generic creation failure
    if ( NULL == instanceName )
    {
        MessageBoxResource( IDS_ERROR_NUICREATE, MB_OK | MB_ICONHAND );
        return E_FAIL;
    }

    HRESULT hr = NuiCreateSensorById( instanceName, &m_pNuiSensor );
    
    // Generic creation failure
    if ( FAILED(hr) )
    {
        MessageBoxResource( IDS_ERROR_NUICREATE, MB_OK | MB_ICONHAND );
        return hr;
    }

    SysFreeString(m_instanceId);

    m_instanceId = m_pNuiSensor->NuiDeviceConnectionId();

    return Nui_Init();
}

/// <summary>
/// Initialize Kinect
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CSkeletalViewerApp::Nui_Init( )
{
    HRESULT  hr;
    bool     result;

    if ( !m_pNuiSensor )
    {
        hr = NuiCreateSensorByIndex(0, &m_pNuiSensor);

        if ( FAILED(hr) )
        {
            return hr;
        }

        SysFreeString(m_instanceId);

        m_instanceId = m_pNuiSensor->NuiDeviceConnectionId();
    }

    m_hNextDepthFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
    m_hNextColorFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
    m_hNextSkeletonEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

    // reset the tracked skeletons, range, and tracking mode
    SendDlgItemMessage(m_hWnd, IDC_TRACKEDSKELETONS, CB_SETCURSEL, 0, 0);
    SendDlgItemMessage(m_hWnd, IDC_TRACKINGMODE, CB_SETCURSEL, 0, 0);
    SendDlgItemMessage(m_hWnd, IDC_RANGE, CB_SETCURSEL, 0, 0);

    EnsureDirect2DResources();

    m_pDrawDepth = new DrawDevice( );
    result = m_pDrawDepth->Initialize( GetDlgItem( m_hWnd, IDC_DEPTHVIEWER ), m_pD2DFactory, 320, 240, 320 * 4 );
    if ( !result )
    {
        MessageBoxResource( IDS_ERROR_DRAWDEVICE, MB_OK | MB_ICONHAND );
        return E_FAIL;
    }

    m_pDrawColor = new DrawDevice( );
    result = m_pDrawColor->Initialize( GetDlgItem( m_hWnd, IDC_VIDEOVIEW ), m_pD2DFactory, 640, 480, 640 * 4 );
    if ( !result )
    {
        MessageBoxResource( IDS_ERROR_DRAWDEVICE, MB_OK | MB_ICONHAND );
        return E_FAIL;
    }
    
    DWORD nuiFlags = NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_SKELETON |  NUI_INITIALIZE_FLAG_USES_COLOR;

    hr = m_pNuiSensor->NuiInitialize(nuiFlags);
    if ( E_NUI_SKELETAL_ENGINE_BUSY == hr )
    {
        nuiFlags = NUI_INITIALIZE_FLAG_USES_DEPTH |  NUI_INITIALIZE_FLAG_USES_COLOR;
        hr = m_pNuiSensor->NuiInitialize( nuiFlags) ;
    }
  
    if ( FAILED( hr ) )
    {
        if ( E_NUI_DEVICE_IN_USE == hr )
        {
            MessageBoxResource( IDS_ERROR_IN_USE, MB_OK | MB_ICONHAND );
        }
        else
        {
            MessageBoxResource( IDS_ERROR_NUIINIT, MB_OK | MB_ICONHAND );
        }
        return hr;
    }

    if ( HasSkeletalEngine( m_pNuiSensor ) )
    {
        hr = m_pNuiSensor->NuiSkeletonTrackingEnable( m_hNextSkeletonEvent, m_SkeletonTrackingFlags );
        if( FAILED( hr ) )
        {
            MessageBoxResource( IDS_ERROR_SKELETONTRACKING, MB_OK | MB_ICONHAND );
            return hr;
        }
    }

    hr = m_pNuiSensor->NuiImageStreamOpen(
        NUI_IMAGE_TYPE_COLOR,
        NUI_IMAGE_RESOLUTION_640x480,
        0,
        2,
        m_hNextColorFrameEvent,
        &m_pVideoStreamHandle );

    if ( FAILED( hr ) )
    {
        MessageBoxResource( IDS_ERROR_VIDEOSTREAM, MB_OK | MB_ICONHAND );
        return hr;
    }

    hr = m_pNuiSensor->NuiImageStreamOpen(
        HasSkeletalEngine(m_pNuiSensor) ? NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX : NUI_IMAGE_TYPE_DEPTH,
        NUI_IMAGE_RESOLUTION_320x240,
        m_DepthStreamFlags,
        2,
        m_hNextDepthFrameEvent,
        &m_pDepthStreamHandle );

    if ( FAILED( hr ) )
    {
        MessageBoxResource(IDS_ERROR_DEPTHSTREAM, MB_OK | MB_ICONHAND);
        return hr;
    }

    // Start the Nui processing thread
    m_hEvNuiProcessStop = CreateEvent( NULL, FALSE, FALSE, NULL );
    m_hThNuiProcess = CreateThread( NULL, 0, Nui_ProcessThread, this, 0, NULL );

    return hr;
}

/// <summary>
/// Uninitialize Kinect
/// </summary>
void CSkeletalViewerApp::Nui_UnInit( )
{
    // Stop the Nui processing thread
    if ( NULL != m_hEvNuiProcessStop )
    {
        // Signal the thread
        SetEvent(m_hEvNuiProcessStop);

        // Wait for thread to stop
        if ( NULL != m_hThNuiProcess )
        {
            WaitForSingleObject( m_hThNuiProcess, INFINITE );
            CloseHandle( m_hThNuiProcess );
        }
        CloseHandle( m_hEvNuiProcessStop );
    }

    if ( m_pNuiSensor )
    {
        m_pNuiSensor->NuiShutdown( );
    }
    if ( m_hNextSkeletonEvent && ( m_hNextSkeletonEvent != INVALID_HANDLE_VALUE ) )
    {
        CloseHandle( m_hNextSkeletonEvent );
        m_hNextSkeletonEvent = NULL;
    }
    if ( m_hNextDepthFrameEvent && ( m_hNextDepthFrameEvent != INVALID_HANDLE_VALUE ) )
    {
        CloseHandle( m_hNextDepthFrameEvent );
        m_hNextDepthFrameEvent = NULL;
    }
    if ( m_hNextColorFrameEvent && ( m_hNextColorFrameEvent != INVALID_HANDLE_VALUE ) )
    {
        CloseHandle( m_hNextColorFrameEvent );
        m_hNextColorFrameEvent = NULL;
    }

    SafeRelease( m_pNuiSensor );
    
    // clean up Direct2D graphics
    delete m_pDrawDepth;
    m_pDrawDepth = NULL;

    delete m_pDrawColor;
    m_pDrawColor = NULL;

    DiscardDirect2DResources();
}

/// <summary>
/// Thread to handle Kinect processing, calls class instance thread processor
/// </summary>
/// <param name="pParam">instance pointer</param>
/// <returns>always 0</returns>
DWORD WINAPI CSkeletalViewerApp::Nui_ProcessThread( LPVOID pParam )
{
    CSkeletalViewerApp *pthis = (CSkeletalViewerApp *)pParam;
    return pthis->Nui_ProcessThread( );
}

/// <summary>
/// Thread to handle Kinect processing
/// </summary>
/// <returns>always 0</returns>
DWORD WINAPI CSkeletalViewerApp::Nui_ProcessThread( )
{
    const int numEvents = 4;
    HANDLE hEvents[numEvents] = { m_hEvNuiProcessStop, m_hNextDepthFrameEvent, m_hNextColorFrameEvent, m_hNextSkeletonEvent };
    int    nEventIdx;
    DWORD  t;

    m_LastDepthFPStime = timeGetTime( );

    // Blank the skeleton display on startup
    m_LastSkeletonFoundTime = 0;

    // Main thread loop
    bool continueProcessing = true;
    while ( continueProcessing )
    {
        // Wait for any of the events to be signalled
        nEventIdx = WaitForMultipleObjects( numEvents, hEvents, FALSE, 100 );

        // Timed out, continue
        if ( nEventIdx == WAIT_TIMEOUT )
        {
            continue;
        }

        // stop event was signalled 
        if ( WAIT_OBJECT_0 == nEventIdx )
        {
            continueProcessing = false;
            break;
        }

        // Wait for each object individually with a 0 timeout to make sure to
        // process all signalled objects if multiple objects were signalled
        // this loop iteration

        // In situations where perfect correspondance between color/depth/skeleton
        // is essential, a priority queue should be used to service the item
        // which has been updated the longest ago

        if ( WAIT_OBJECT_0 == WaitForSingleObject( m_hNextDepthFrameEvent, 0 ) )
        {
            //only increment frame count if a frame was successfully drawn
            if ( Nui_GotDepthAlert() )
            {
                ++m_DepthFramesTotal;
            }
        }

        if ( WAIT_OBJECT_0 == WaitForSingleObject( m_hNextColorFrameEvent, 0 ) )
        {
            Nui_GotColorAlert();
        }

        if (  WAIT_OBJECT_0 == WaitForSingleObject( m_hNextSkeletonEvent, 0 ) )
        {
            Nui_GotSkeletonAlert( );
        }

        // Once per second, display the depth FPS
        t = timeGetTime( );
        if ( (t - m_LastDepthFPStime) > 1000 )
        {
            int fps = ((m_DepthFramesTotal - m_LastDepthFramesTotal) * 1000 + 500) / (t - m_LastDepthFPStime);
            PostMessageW( m_hWnd, WM_USER_UPDATE_FPS, IDC_FPS, fps );
            m_LastDepthFramesTotal = m_DepthFramesTotal;
            m_LastDepthFPStime = t;
        }

        // Blank the skeleton panel if we haven't found a skeleton recently
        if ( (t - m_LastSkeletonFoundTime) > 300 )
        {
            if ( !m_bScreenBlanked )
            {
                Nui_BlankSkeletonScreen( );
                m_bScreenBlanked = true;
            }
        }
    }

    return 0;
}

/// <summary>
/// Handle new color data
/// </summary>
/// <returns>true if a frame was processed, false otherwise</returns>
bool CSkeletalViewerApp::Nui_GotColorAlert( )
{
    NUI_IMAGE_FRAME imageFrame;
    bool processedFrame = true;

    HRESULT hr = m_pNuiSensor->NuiImageStreamGetNextFrame( m_pVideoStreamHandle, 0, &imageFrame );

    if ( FAILED( hr ) )
    {
        return false;
    }

    INuiFrameTexture * pTexture = imageFrame.pFrameTexture;
    NUI_LOCKED_RECT LockedRect;
    pTexture->LockRect( 0, &LockedRect, NULL, 0 );
    if ( LockedRect.Pitch != 0 )
    {
        m_pDrawColor->Draw( static_cast<BYTE *>(LockedRect.pBits), LockedRect.size );
    }
    else
    {
        OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
        processedFrame = false;
    }

    pTexture->UnlockRect( 0 );

    m_pNuiSensor->NuiImageStreamReleaseFrame( m_pVideoStreamHandle, &imageFrame );

    return processedFrame;
}

/// <summary>
/// Handle new depth data
/// </summary>
/// <returns>true if a frame was processed, false otherwise</returns>
bool CSkeletalViewerApp::Nui_GotDepthAlert( )
{
    NUI_IMAGE_FRAME imageFrame;
    bool processedFrame = true;

    HRESULT hr = m_pNuiSensor->NuiImageStreamGetNextFrame(
        m_pDepthStreamHandle,
        0,
        &imageFrame );

    if ( FAILED( hr ) )
    {
        return false;
    }

    INuiFrameTexture * pTexture = imageFrame.pFrameTexture;
    NUI_LOCKED_RECT LockedRect;
    pTexture->LockRect( 0, &LockedRect, NULL, 0 );
    if ( 0 != LockedRect.Pitch )
    {
        DWORD frameWidth, frameHeight;
        
        NuiImageResolutionToSize( imageFrame.eResolution, frameWidth, frameHeight );
        
        // draw the bits to the bitmap
        BYTE * rgbrun = m_depthRGBX;
        const USHORT * pBufferRun = (const USHORT *)LockedRect.pBits;

        // end pixel is start + width*height - 1
        const USHORT * pBufferEnd = pBufferRun + (frameWidth * frameHeight);

        assert( frameWidth * frameHeight * g_BytesPerPixel <= ARRAYSIZE(m_depthRGBX) );

        while ( pBufferRun < pBufferEnd )
        {
            USHORT depth     = *pBufferRun;
            USHORT realDepth = NuiDepthPixelToDepth(depth);
            USHORT player    = NuiDepthPixelToPlayerIndex(depth);

            // transform 13-bit depth information into an 8-bit intensity appropriate
            // for display (we disregard information in most significant bit)
            BYTE intensity = static_cast<BYTE>(~(realDepth >> 4));

            // tint the intensity by dividing by per-player values
            *(rgbrun++) = intensity >> g_IntensityShiftByPlayerB[player];
            *(rgbrun++) = intensity >> g_IntensityShiftByPlayerG[player];
            *(rgbrun++) = intensity >> g_IntensityShiftByPlayerR[player];

            // no alpha information, skip the last byte
            ++rgbrun;

            ++pBufferRun;
        }

        m_pDrawDepth->Draw( m_depthRGBX, frameWidth * frameHeight * g_BytesPerPixel );
    }
    else
    {
        processedFrame = false;
        OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
    }

    pTexture->UnlockRect( 0 );

    m_pNuiSensor->NuiImageStreamReleaseFrame( m_pDepthStreamHandle, &imageFrame );

    return processedFrame;
}

/// <summary>
/// Blank the skeleton display
/// </summary>
void CSkeletalViewerApp::Nui_BlankSkeletonScreen( )
{
    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear( );
    m_pRenderTarget->EndDraw( );
}

/// <summary>
/// Draws a line between two bones
/// </summary>
/// <param name="skel">skeleton to draw bones from</param>
/// <param name="bone0">bone to start drawing from</param>
/// <param name="bone1">bone to end drawing at</param>
void CSkeletalViewerApp::Nui_DrawBone( const NUI_SKELETON_DATA & skel, NUI_SKELETON_POSITION_INDEX bone0, NUI_SKELETON_POSITION_INDEX bone1 )
{
    NUI_SKELETON_POSITION_TRACKING_STATE bone0State = skel.eSkeletonPositionTrackingState[bone0];
    NUI_SKELETON_POSITION_TRACKING_STATE bone1State = skel.eSkeletonPositionTrackingState[bone1];

    // If we can't find either of these joints, exit
    if ( bone0State == NUI_SKELETON_POSITION_NOT_TRACKED || bone1State == NUI_SKELETON_POSITION_NOT_TRACKED )
    {
        return;
    }
    
    // Don't draw if both points are inferred
    if ( bone0State == NUI_SKELETON_POSITION_INFERRED && bone1State == NUI_SKELETON_POSITION_INFERRED )
    {
        return;
    }

    // We assume all drawn bones are inferred unless BOTH joints are tracked
    if ( bone0State == NUI_SKELETON_POSITION_TRACKED && bone1State == NUI_SKELETON_POSITION_TRACKED )
    {
        m_pRenderTarget->DrawLine( m_Points[bone0], m_Points[bone1], m_pBrushBoneTracked, g_TrackedBoneThickness );
    }
    else
    {
        m_pRenderTarget->DrawLine( m_Points[bone0], m_Points[bone1], m_pBrushBoneInferred, g_InferredBoneThickness );
    }
}

/// <summary>
/// Draws a skeleton
/// </summary>
/// <param name="skel">skeleton to draw</param>
/// <param name="windowWidth">width (in pixels) of output buffer</param>
/// <param name="windowHeight">height (in pixels) of output buffer</param>
void CSkeletalViewerApp::Nui_DrawSkeleton( const NUI_SKELETON_DATA & skel, int windowWidth, int windowHeight )
{      
    int i;

    for (i = 0; i < NUI_SKELETON_POSITION_COUNT; i++)
    {
        m_Points[i] = SkeletonToScreen( skel.SkeletonPositions[i], windowWidth, windowHeight );
    }

    // Render Torso
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_HEAD, NUI_SKELETON_POSITION_SHOULDER_CENTER );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SHOULDER_RIGHT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SHOULDER_CENTER, NUI_SKELETON_POSITION_SPINE );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SPINE, NUI_SKELETON_POSITION_HIP_CENTER );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_HIP_CENTER, NUI_SKELETON_POSITION_HIP_RIGHT );

    // Left Arm
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SHOULDER_LEFT, NUI_SKELETON_POSITION_ELBOW_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_ELBOW_LEFT, NUI_SKELETON_POSITION_WRIST_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_WRIST_LEFT, NUI_SKELETON_POSITION_HAND_LEFT );

    // Right Arm
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_SHOULDER_RIGHT, NUI_SKELETON_POSITION_ELBOW_RIGHT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_ELBOW_RIGHT, NUI_SKELETON_POSITION_WRIST_RIGHT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_WRIST_RIGHT, NUI_SKELETON_POSITION_HAND_RIGHT );

    // Left Leg
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_HIP_LEFT, NUI_SKELETON_POSITION_KNEE_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_KNEE_LEFT, NUI_SKELETON_POSITION_ANKLE_LEFT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_ANKLE_LEFT, NUI_SKELETON_POSITION_FOOT_LEFT );

    // Right Leg
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_HIP_RIGHT, NUI_SKELETON_POSITION_KNEE_RIGHT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_KNEE_RIGHT, NUI_SKELETON_POSITION_ANKLE_RIGHT );
    Nui_DrawBone( skel, NUI_SKELETON_POSITION_ANKLE_RIGHT, NUI_SKELETON_POSITION_FOOT_RIGHT );
    
    // Draw the joints in a different color
    for ( i = 0; i < NUI_SKELETON_POSITION_COUNT; i++ )
    {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse( m_Points[i], g_JointThickness, g_JointThickness );

        if ( skel.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_INFERRED )
        {
            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointInferred);
        }
        else if ( skel.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_TRACKED )
        {
            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointTracked);
        }
    }
}

/// <summary>
/// Determines which skeletons to track and tracks them
/// </summary>
/// <param name="skel">skeleton frame information</param>
void CSkeletalViewerApp::UpdateTrackedSkeletons( const NUI_SKELETON_FRAME & skel )
{
    DWORD nearestIDs[2] = { 0, 0 };
    USHORT nearestDepths[2] = { NUI_IMAGE_DEPTH_MAXIMUM, NUI_IMAGE_DEPTH_MAXIMUM };

    // Purge old sticky skeleton IDs, if the user has left the frame, etc
    bool stickyID0Found = false;
    bool stickyID1Found = false;
    for ( int i = 0 ; i < NUI_SKELETON_COUNT; i++ )
    {
        NUI_SKELETON_TRACKING_STATE trackingState = skel.SkeletonData[i].eTrackingState;

        if ( trackingState == NUI_SKELETON_TRACKED || trackingState == NUI_SKELETON_POSITION_ONLY )
        {
            if ( skel.SkeletonData[i].dwTrackingID == m_StickySkeletonIds[0] )
            {
                stickyID0Found = true;
            }
            else if ( skel.SkeletonData[i].dwTrackingID == m_StickySkeletonIds[1] )
            {
                stickyID1Found = true;
            }
        }
    }

    if ( !stickyID0Found && stickyID1Found )
    {
        m_StickySkeletonIds[0] = m_StickySkeletonIds[1];
        m_StickySkeletonIds[1] = 0;
    }
    else if ( !stickyID0Found )
    {
        m_StickySkeletonIds[0] = 0;
    }
    else if ( !stickyID1Found )
    {
        m_StickySkeletonIds[1] = 0;
    }

    // Calculate nearest and sticky skeletons
    for ( int i = 0 ; i < NUI_SKELETON_COUNT; i++ )
    {
        NUI_SKELETON_TRACKING_STATE trackingState = skel.SkeletonData[i].eTrackingState;

        if ( trackingState == NUI_SKELETON_TRACKED || trackingState == NUI_SKELETON_POSITION_ONLY )
        {
            // Save SkeletonIds for sticky mode if there's none already saved
            if ( 0 == m_StickySkeletonIds[0] && m_StickySkeletonIds[1] != skel.SkeletonData[i].dwTrackingID )
            {
                m_StickySkeletonIds[0] = skel.SkeletonData[i].dwTrackingID;
            }
            else if ( 0 == m_StickySkeletonIds[1] && m_StickySkeletonIds[0] != skel.SkeletonData[i].dwTrackingID )
            {
                m_StickySkeletonIds[1] = skel.SkeletonData[i].dwTrackingID;
            }

            LONG x, y;
            USHORT depth;

            // calculate the skeleton's position on the screen
            NuiTransformSkeletonToDepthImage( skel.SkeletonData[i].Position, &x, &y, &depth );

            if ( depth < nearestDepths[0] )
            {
                nearestDepths[1] = nearestDepths[0];
                nearestIDs[1] = nearestIDs[0];

                nearestDepths[0] = depth;
                nearestIDs[0] = skel.SkeletonData[i].dwTrackingID;
            }
            else if ( depth < nearestDepths[1] )
            {
                nearestDepths[1] = depth;
                nearestIDs[1] = skel.SkeletonData[i].dwTrackingID;
            }
        }
    }

    if ( SV_TRACKED_SKELETONS_NEAREST1 == m_TrackedSkeletons || SV_TRACKED_SKELETONS_NEAREST2 == m_TrackedSkeletons )
    {
        // Only track the closest single skeleton in nearest 1 mode
        if ( SV_TRACKED_SKELETONS_NEAREST1 == m_TrackedSkeletons )
        {
            nearestIDs[1] = 0;
        }
        m_pNuiSensor->NuiSkeletonSetTrackedSkeletons(nearestIDs);
    }

    if ( SV_TRACKED_SKELETONS_STICKY1 == m_TrackedSkeletons || SV_TRACKED_SKELETONS_STICKY2 == m_TrackedSkeletons )
    {
        DWORD stickyIDs[2] = { m_StickySkeletonIds[0], m_StickySkeletonIds[1] };

        // Only track a single skeleton in sticky 1 mode
        if ( SV_TRACKED_SKELETONS_STICKY1 == m_TrackedSkeletons )
        {
            stickyIDs[1] = 0;
        }
        m_pNuiSensor->NuiSkeletonSetTrackedSkeletons(stickyIDs);
    }
}

/// <summary>
/// Converts a skeleton point to screen space
/// </summary>
/// <param name="skeletonPoint">skeleton point to tranform</param>
/// <param name="width">width (in pixels) of output buffer</param>
/// <param name="height">height (in pixels) of output buffer</param>
/// <returns>point in screen-space</returns>
D2D1_POINT_2F CSkeletalViewerApp::SkeletonToScreen( Vector4 skeletonPoint, int width, int height )
{
    LONG x, y;
    USHORT depth;

    // calculate the skeleton's position on the screen
    // NuiTransformSkeletonToDepthImage returns coordinates in NUI_IMAGE_RESOLUTION_320x240 space
    NuiTransformSkeletonToDepthImage( skeletonPoint, &x, &y, &depth );

    float screenPointX = static_cast<float>(x * width) / g_ScreenWidth;
    float screenPointY = static_cast<float>(y * height) / g_ScreenHeight;

    return D2D1::Point2F(screenPointX, screenPointY);
}

/// <summary>
/// Handle new skeleton data
/// </summary>
bool CSkeletalViewerApp::Nui_GotSkeletonAlert( )
{
    NUI_SKELETON_FRAME SkeletonFrame = {0};

    bool foundSkeleton = false;

    if ( SUCCEEDED(m_pNuiSensor->NuiSkeletonGetNextFrame( 0, &SkeletonFrame )) )
    {
        for ( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ )
        {
            NUI_SKELETON_TRACKING_STATE trackingState = SkeletonFrame.SkeletonData[i].eTrackingState;

            if ( trackingState == NUI_SKELETON_TRACKED || trackingState == NUI_SKELETON_POSITION_ONLY )
            {
                foundSkeleton = true;
            }
        }
    }

    // no skeletons!
    if( !foundSkeleton )
    {
        return true;
    }

    // smooth out the skeleton data
    HRESULT hr = m_pNuiSensor->NuiTransformSmooth(&SkeletonFrame,NULL);
    if ( FAILED(hr) )
    {
        return false;
    }

    // we found a skeleton, re-start the skeletal timer
    m_bScreenBlanked = false;
    m_LastSkeletonFoundTime = timeGetTime( );

    // Endure Direct2D is ready to draw
    hr = EnsureDirect2DResources( );
    if ( FAILED( hr ) )
    {
        return false;
    }

    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear( );
    
    RECT rct;
    GetClientRect( GetDlgItem( m_hWnd, IDC_SKELETALVIEW ), &rct);
    int width = rct.right;
    int height = rct.bottom;

    for ( int i = 0 ; i < NUI_SKELETON_COUNT; i++ )
    {
        NUI_SKELETON_TRACKING_STATE trackingState = SkeletonFrame.SkeletonData[i].eTrackingState;

        if ( trackingState == NUI_SKELETON_TRACKED )
        {
            // We're tracking the skeleton, draw it
            Nui_DrawSkeleton( SkeletonFrame.SkeletonData[i], width, height );
        }
        else if ( trackingState == NUI_SKELETON_POSITION_ONLY )
        {
            // we've only received the center point of the skeleton, draw that
            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                SkeletonToScreen( SkeletonFrame.SkeletonData[i].Position, width, height ),
                g_JointThickness,
                g_JointThickness
                );

            m_pRenderTarget->DrawEllipse(ellipse, m_pBrushJointTracked);
        }
    }

    hr = m_pRenderTarget->EndDraw();

    UpdateTrackedSkeletons( SkeletonFrame );

    // Device lost, need to recreate the render target
    // We'll dispose it now and retry drawing
    if ( hr == D2DERR_RECREATE_TARGET )
    {
        hr = S_OK;
        DiscardDirect2DResources();
        return false;
    }

    return true;
}

/// <summary>
/// Invoked when the user changes the selection of tracked skeletons
/// </summary>
/// <param name="mode">skelton tracking mode to switch to</param>
void CSkeletalViewerApp::UpdateTrackedSkeletonSelection( int mode )
{
    m_TrackedSkeletons = mode;

    UpdateSkeletonTrackingFlag(
        NUI_SKELETON_TRACKING_FLAG_TITLE_SETS_TRACKED_SKELETONS,
        (mode != SV_TRACKED_SKELETONS_DEFAULT));
}

/// <summary>
/// Invoked when the user changes the tracking mode
/// </summary>
/// <param name="mode">tracking mode to switch to</param>
void CSkeletalViewerApp::UpdateTrackingMode( int mode )
{
    UpdateSkeletonTrackingFlag(
        NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT,
        (mode == SV_TRACKING_MODE_SEATED) );
}

/// <summary>
/// Invoked when the user changes the range
/// </summary>
/// <param name="mode">range to switch to</param>
void CSkeletalViewerApp::UpdateRange( int mode )
{
    UpdateDepthStreamFlag(
        NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE,
        (mode != SV_RANGE_DEFAULT) );
}

/// <summary>
/// Sets or clears the specified skeleton tracking flag
/// </summary>
/// <param name="flag">flag to set or clear</param>
/// <param name="value">true to set, false to clear</param>
void CSkeletalViewerApp::UpdateSkeletonTrackingFlag( DWORD flag, bool value )
{
    DWORD newFlags = m_SkeletonTrackingFlags;

    if (value)
    {
        newFlags |= flag;
    }
    else
    {
        newFlags &= ~flag;
    }

    if (NULL != m_pNuiSensor && newFlags != m_SkeletonTrackingFlags)
    {
        if ( !HasSkeletalEngine(m_pNuiSensor) )
        {
            MessageBoxResource(IDS_ERROR_SKELETONTRACKING, MB_OK | MB_ICONHAND);
        }

        m_SkeletonTrackingFlags = newFlags;

        HRESULT hr = m_pNuiSensor->NuiSkeletonTrackingEnable( m_hNextSkeletonEvent, m_SkeletonTrackingFlags );

        if ( FAILED( hr ) )
        {
            MessageBoxResource(IDS_ERROR_SKELETONTRACKING, MB_OK | MB_ICONHAND);
        }
    }
}

/// <summary>
/// Sets or clears the specified depth stream flag
/// </summary>
/// <param name="flag">flag to set or clear</param>
/// <param name="value">true to set, false to clear</param>
void CSkeletalViewerApp::UpdateDepthStreamFlag( DWORD flag, bool value )
{
    DWORD newFlags = m_DepthStreamFlags;

    if (value)
    {
        newFlags |= flag;
    }
    else
    {
        newFlags &= ~flag;
    }

    if (NULL != m_pNuiSensor && newFlags != m_DepthStreamFlags)
    {
        m_DepthStreamFlags = newFlags;
        m_pNuiSensor->NuiImageStreamSetImageFrameFlags( m_pDepthStreamHandle, m_DepthStreamFlags );
    }
}

/// <summary>
/// Ensure necessary Direct2d resources are created
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CSkeletalViewerApp::EnsureDirect2DResources()
{
    HRESULT hr = S_OK;

    if ( !m_pRenderTarget )
    {
        RECT rc;
        GetWindowRect( GetDlgItem( m_hWnd, IDC_SKELETALVIEW ), &rc );  
    
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        D2D1_SIZE_U size = D2D1::SizeU( width, height );
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

        // Create a Hwnd render target, in order to render to the window set in initialize
        hr = m_pD2DFactory->CreateHwndRenderTarget(
            rtProps,
            D2D1::HwndRenderTargetProperties(GetDlgItem( m_hWnd, IDC_SKELETALVIEW), size),
            &m_pRenderTarget
            );
        if ( FAILED( hr ) )
        {
            MessageBoxResource( IDS_ERROR_DRAWDEVICE, MB_OK | MB_ICONHAND );
            return E_FAIL;
        }

        //light green
        m_pRenderTarget->CreateSolidColorBrush( D2D1::ColorF( 68, 192, 68 ), &m_pBrushJointTracked );

        //yellow
        m_pRenderTarget->CreateSolidColorBrush( D2D1::ColorF( 255, 255, 0 ), &m_pBrushJointInferred );

        //green
        m_pRenderTarget->CreateSolidColorBrush( D2D1::ColorF( 0, 128, 0 ), &m_pBrushBoneTracked );

        //gray
        m_pRenderTarget->CreateSolidColorBrush( D2D1::ColorF( 128, 128, 128 ), &m_pBrushBoneInferred );
    }

    return hr;
}

/// <summary>
/// Dispose Direct2d resources 
/// </summary>
void CSkeletalViewerApp::DiscardDirect2DResources( )
{
    SafeRelease(m_pRenderTarget);

    SafeRelease( m_pBrushJointTracked );
    SafeRelease( m_pBrushJointInferred );
    SafeRelease( m_pBrushBoneTracked );
    SafeRelease( m_pBrushBoneInferred );
}
