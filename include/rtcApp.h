//-----------------------------------------------------------------------------
// File : rtcApp.h
// Desc : Application.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <rtcTypedef.h>
#include <rtcTimer.h>
#include <rtcDevice.h>



namespace rtc {

///////////////////////////////////////////////////////////////////////////////
// Config structure
///////////////////////////////////////////////////////////////////////////////
struct Config
{
    uint32_t    Width       = 1920;     //!< 横幅.
    uint32_t    Height      = 1080;     //!< 縦幅.
    double      AnimFPS     = 60.0;     //!< アニメーションのFrame Per Second.
    double      RenderTime  = 256.0;    //!< 制限時間(sec).
};

///////////////////////////////////////////////////////////////////////////////
// App class
///////////////////////////////////////////////////////////////////////////////
class App
{
public:
    App () = default;
    ~App() = default;
    void Run(const Config& config);

private:
    Config      m_Config   = {};
    Timer       m_Timer    = {};
    bool        m_IsLoop   = true;

    bool Init();
    void Term();
    void MainLoop();

    bool OnLoad  ();
    void OnUnload();
    void OnRender();
};

} // namespace rtc
