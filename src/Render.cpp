//-----------------------------------------------------------------------------
// File : Render.cpp
// Desc : Rendering Function.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Macro.h>
#include <App.h>

#if ASDX_ENABLE_IMGUI
#include <imgui.h>
#endif//ASDX_ENABLE_IMGUI


//-----------------------------------------------------------------------------
//      描画処理です.
//-----------------------------------------------------------------------------
void App::Render(ID3D12GraphicsCommandList6* pCmd)
{
    // デノイズ用 G-Bufferパス.
    {
    }

    // Path-Tracing.
    {
    }

    // デノイズ.
    {
    }

    // Tonemapping.
    {
    }

    // ポストエフェクト.
    {
    }
}


#if RTC_TARGET == RTC_DEVELOP
//-----------------------------------------------------------------------------
//      デバッグ用2D描画を行います.
//-----------------------------------------------------------------------------
void App::Draw2D(ID3D12GraphicsCommandList6* pCmd)
{
    #if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Update(m_Width, m_Height);
    {
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(120, 0));
        if (ImGui::Begin(u8"フレーム情報", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text(u8"FPS   : %.3lf", GetFPS());
            ImGui::Text(u8"Frame : %ld", GetFrameCount());
            ImGui::Text(u8"Accum : %ld", m_AccumulatedFrames);
        }
        ImGui::End();

    }
    asdx::GuiMgr::Instance().Draw(pCmd);
    #endif//ASDX_ENABLE_IMGUI
}
#endif//RTC_TARGET == RTC_DEVELOP
