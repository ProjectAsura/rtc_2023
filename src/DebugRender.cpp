//-----------------------------------------------------------------------------
// File : DebugRender.cpp
// Desc : Debug Rendering Function.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Macro.h>
#if RTC_TARGET == RTC_DEVELOP
#include <App.h>

#if ASDX_ENABLE_IMGUI
#include <imgui.h>
#endif//ASDX_ENABLE_IMGUI


namespace {

///////////////////////////////////////////////////////////////////////////////
// DEBUG_TEXTURE_TYPE enum
///////////////////////////////////////////////////////////////////////////////
enum DEBUG_TEXTURE_TYPE
{
    DEBUG_TEXTURE_RENDERED = 0,
    DEBUG_TEXTURE_ALBEDO,
    DEBUG_TEXTURE_NORMAL,
    DEBUG_TEXTURE_ROUGHNESS,
    DEBUG_TEXTURE_VELOCITY,
};

///////////////////////////////////////////////////////////////////////////////
// SAMPLING_TYPE enum
///////////////////////////////////////////////////////////////////////////////
enum SAMPLING_TYPE
{
    SAMPLING_TYPE_RGBA      = 0,
    SAMPLING_TYPE_RGB       = 1,
    SAMPLING_TYPE_R         = 2,
    SAMPLING_TYEP_G         = 3,
    SAMPLING_TYPE_B         = 4,
    SAMPLING_TYPE_NORMAL    = 5,
    SAMPLING_TYPE_VELOCITY  = 6,
    SAMPLING_TYPE_HEAT_MAP  = 7,
};

//-----------------------------------------------------------------------------
// Constants.
//-----------------------------------------------------------------------------
static const char* kDebugTextureItems[] = {
    u8"描画結果",
    u8"アルベド",
    u8"法線",
    u8"ラフネス",
    u8"速度",
};

} // namespace

//-----------------------------------------------------------------------------
//      デバッグ用2D描画を行います.
//-----------------------------------------------------------------------------
void App::Draw2D(ID3D12GraphicsCommandList6* pCmd)
{
    const asdx::IShaderResourceView* pDebugSRV = m_Radiance.GetSRV();
    uint32_t samplingType = SAMPLING_TYPE_RGBA;

    switch(m_DebugTextureType)
    {
    case DEBUG_TEXTURE_RENDERED:
        {
            pDebugSRV    = m_Radiance.GetSRV();
            samplingType = SAMPLING_TYPE_RGB;
        }
        break;

    case DEBUG_TEXTURE_ALBEDO:
        {
            pDebugSRV    = m_Albedo.GetSRV();
            samplingType = SAMPLING_TYPE_RGBA;
        }
        break;

    case DEBUG_TEXTURE_NORMAL:
        {
            pDebugSRV    = m_Normal.GetSRV();
            samplingType = SAMPLING_TYPE_NORMAL;
        }
        break;

    case DEBUG_TEXTURE_ROUGHNESS:
        {
            //pDebugSRV    = m_Roughness.GetSRV();
            samplingType = SAMPLING_TYPE_R;
        }
        break;

    case DEBUG_TEXTURE_VELOCITY:
        {
            pDebugSRV    = m_Velocity.GetSRV();
            samplingType = SAMPLING_TYPE_VELOCITY;
        }
        break;
    }
    assert(pDebugSRV != nullptr);

    // 描画結果を表示.
    {
        pCmd->SetGraphicsRootSignature(m_DebugRootSignature.GetPtr());
        m_DebugPipelineState.SetState(pCmd);
        pCmd->SetGraphicsRoot32BitConstant(0, samplingType, 0);
        pCmd->SetGraphicsRootDescriptorTable(1, pDebugSRV->GetHandleGPU());
        asdx::DrawQuad(pCmd);
    }

    // レイを描画
    DrawRay(pCmd);

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

        ImGui::SetNextWindowPos(ImVec2(20, 100), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(250, 0));
        if (ImGui::Begin(u8"デバッグ設定", &m_OpenDebugSetting))
        {
            int count = _countof(kDebugTextureItems);
            ImGui::Combo(u8"バッファ", &m_DebugTextureType, kDebugTextureItems, count);

            // フリーズカメラ.
            // レイトレーシングに用いるカメラ更新をデバッグの為に停止します.
            if (!m_FreezeCamera) 
            {
                if (ImGui::Button(u8"Freeze Camera"))
                { m_FreezeCamera = true; }
            }
            else
            {
                if (ImGui::Button(u8"Unfreeze Camera"))
                { m_FreezeCamera = false; }
            }

            if (ImGui::CollapsingHeader(u8"レイデバッグ"))
            {
                int index[2] = { m_DebugRayIndexOfX, m_DebugRayIndexOfY };
                int mini = -1;
                int maxi = asdx::Max<int>(int(m_Width), int(m_Height));
                if (ImGui::DragInt2(u8"レイ番号", index, 1.0f, mini, maxi))
                {
                    m_DebugRayIndexOfX = index[0];
                    m_DebugRayIndexOfY = index[1];
                }
            }

            if (ImGui::CollapsingHeader(u8"カメラ情報"))
            {
                auto pos = m_AppCamera.GetPosition();
                auto at  = m_AppCamera.GetTarget();
                auto up  = m_AppCamera.GetUpward();

                ImGui::Text(u8"位置   : %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
                ImGui::Text(u8"注視点 : %.2f, %.2f, %.2f", at.x, at.y, at.z);
                ImGui::Text(u8"上向き : %.2f, %.2f, %.2f", up.x, up.y, up.z);

                if (ImGui::Button(u8"カメラ情報出力"))
                {
                    auto& param = m_AppCamera.GetParam();
                    printf_s("// Camera Parameter\n");
                    printf_s("asdx::Camera::Param param;\n");
                    printf_s("param.Position = asdx::Vector3(%f, %f, %f);\n", param.Position.x, param.Position.y, param.Position.z);
                    printf_s("param.Target   = asdx::Vector3(%f, %f, %f);\n", param.Target.x, param.Target.y, param.Target.z);
                    printf_s("param.Upward   = asdx::Vector3(%f, %f, %f);\n", param.Upward.x, param.Upward.y, param.Upward.z);
                    printf_s("param.Rotate   = asdx::Vector2(%f, %f);\n", param.Rotate.x, param.Rotate.y);
                    printf_s("param.PanTilt  = asdx::Vector2(%f, %f);\n", param.PanTilt.x, param.PanTilt.y);
                    printf_s("param.Twist    = %f;\n", param.Twist);
                    printf_s("param.MinDist  = %f;\n", param.MinDist);
                    printf_s("param.MaxDist  = %f;\n", param.MaxDist);
                    printf_s("\n");
                }
            }
        }
        ImGui::End();
    }
    asdx::GuiMgr::Instance().Draw(pCmd);
    #endif//ASDX_ENABLE_IMGUI
}


//-----------------------------------------------------------------------------
//      レイをデバッグ描画します.
//-----------------------------------------------------------------------------
void App::DrawRay(ID3D12GraphicsCommandList6* pCmd)
{
    pCmd->SetGraphicsRootSignature(m_DebugRootSignature.GetPtr());
    m_LinePipelineState.SetState(pCmd);
    pCmd->SetGraphicsRootDescriptorTable(1, m_RayPoints.GetView()->GetHandleGPU());
    pCmd->SetGraphicsRootConstantBufferView(2, m_SceneParam.GetResource()->GetGPUVirtualAddress());
    pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    pCmd->ExecuteIndirect(m_DrawCommandSig.GetPtr(), 1, m_DrawArgs.GetResource(), 0, nullptr, 0);
}
#endif//RTC_TARGET == RTC_DEVELOP
