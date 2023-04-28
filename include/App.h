//-----------------------------------------------------------------------------
// File : App.h
// Desc : Renderer Application.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Macro.h>
#include <fw/asdxApp.h>
#include <fnd/asdxBitFlags.h>
#include <gfx/asdxRayTracing.h>
#include <gfx/asdxBuffer.h>
#include <gfx/asdxTarget.h>
#include <gfx/asdxPipelineState.h>
#include <gfx/asdxCommandQueue.h>

#if defined(DEBUG) || defined(_DEBUG)
#define ASDX_ENABLE_IMGUI   (1)
#include <edit/asdxGuiMgr.h>
#endif

#if RTC_TARGET == RTC_DEVELOP
#include <fw/asdxAppCamera.h>
#endif

#define EXPORT_COUNT    (2)

///////////////////////////////////////////////////////////////////////////////
// RenderDesc structure
///////////////////////////////////////////////////////////////////////////////
struct RenderDesc
{
    double      RenderTimeSec;      // 最大描画時間[sec].
    uint32_t    Width;              // 横幅[pixel]
    uint32_t    Height;             // 縦幅[pixel]
    double      FPS;                // Frame Per Second.
    double      AnimationTimeSec;   // 総アニメーション時間[sec]
};

///////////////////////////////////////////////////////////////////////////////
// ExportImage structure
///////////////////////////////////////////////////////////////////////////////
struct ExportImage
{
    std::vector<uint8_t>    Converted;
    uint32_t                Width;
    uint32_t                Height;
    uint32_t                FrameIndex;
    asdx::WaitPoint         WaitPoint;
    asdx::CommandQueue*     pQueue;
    ID3D12Resource*         pReadBackTexture;
};

///////////////////////////////////////////////////////////////////////////////
// App class
///////////////////////////////////////////////////////////////////////////////
class App : public asdx::Application
{
public:
    App(const RenderDesc& desc);
    ~App();

private:
    struct RayTracingPipeline
    {
        asdx::RayTracingPipelineState   PipelineState;
        asdx::ShaderTable               RayGen;
        asdx::ShaderTable               Miss;
        asdx::ShaderTable               HitGroup;

        bool Init(
            ID3D12RootSignature*    pRootSignature,
            const void*             pBinary,
            size_t                  binarySize);

        void Term();

        void DispatchRays(ID3D12GraphicsCommandList6* pCmd, uint32_t w, uint32_t h);
    };

    RenderDesc                      m_RenderDesc;
    asdx::WaitPoint                 m_WaitPoint;
    asdx::ConstantBuffer            m_SceneParam;

    asdx::ComputeTarget             m_Radiance;
    asdx::ColorTarget               m_Albedo;
    asdx::ColorTarget               m_Normal;
    asdx::ColorTarget               m_Velocity;
    asdx::DepthTarget               m_Depth;
    asdx::ComputeTarget             m_Denoised;
    asdx::ComputeTarget             m_Tonemaped;
    asdx::ComputeTarget             m_ColorHistory[2];
    asdx::ComputeTarget             m_Capture[EXPORT_COUNT];
    asdx::RefPtr<ID3D12Resource>    m_ReadBackTexture[EXPORT_COUNT];

    uint8_t                         m_CurrHistoryIndex = 0;
    uint8_t                         m_PrevHistoryIndex = 1;

    asdx::PipelineState             m_GBufferPipelineState;
    asdx::PipelineState             m_TonemapPipelineState;
    asdx::PipelineState             m_DenoisePipelineState;
    asdx::PipelineState             m_TemporalAntiAliasPipelineState;
    RayTracingPipeline              m_RayTracingPipeline;

    asdx::RefPtr<ID3D12RootSignature>   m_GBufferRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_TonemapRootSig;
    asdx::RefPtr<ID3D12RootSignature>   m_RayTracingRootSig;

    uint32_t        m_ReadBackPitch     = 0;
    uint64_t        m_AppFrameCount     = 0;
    uint32_t        m_CaptureIndex      = 0;
    uint8_t         m_ExportIndex       = 0;
    bool            m_RequestTerminate  = false;
    uint32_t        m_AccumulatedFrames = 0;

    ExportImage     m_ExportImages[EXPORT_COUNT];

    asdx::Matrix    m_CurrView;
    asdx::Matrix    m_CurrProj;
    asdx::Matrix    m_CurrInvView;
    asdx::Matrix    m_CurrInvProj;
    asdx::Matrix    m_PrevView;
    asdx::Matrix    m_PrevProj;
    asdx::Matrix    m_PrevInvView;
    asdx::Matrix    m_PrevInvProj;
    asdx::Vector3   m_CameraDir;

#if 1 // For Test.
    asdx::RefPtr<ID3D12Resource>    m_VB;
    asdx::RefPtr<ID3D12Resource>    m_IB;
    asdx::RefPtr<asdx::IShaderResourceView> m_VertexSRV;
    asdx::RefPtr<asdx::IShaderResourceView> m_IndexSRV;
    asdx::Blas                              m_BLAS;
    asdx::Tlas                              m_TLAS;
    bool InitForTest(ID3D12GraphicsCommandList6* pCmd);
#endif

#if RTC_TARGET == RTC_DEVELOP
    //+++++++++++++++++++
    //      開発用.
    //+++++++++++++++++++
    asdx::BitFlags8                 m_RayTracingReloadFlags;
    asdx::BitFlags8                 m_GBufferReloadFlags;
    asdx::BitFlags8                 m_TonemapReloadFlags;
    RayTracingPipeline              m_DevRayTracingPipeline;
    asdx::AppCamera                 m_AppCamera;
    bool                            m_DirtyShader;

    bool                                 m_OpenDebugSetting;
    int                                  m_DebugTextureType;
    asdx::RefPtr<ID3D12RootSignature>    m_DebugRootSignature;
    asdx::PipelineState                  m_DebugPipelineState;

    void Draw2D(ID3D12GraphicsCommandList6* pCmd);
    void ReloadShader();
    bool InitDebugPass();
#endif

    bool OnInit() override;
    void OnTerm() override;
    void OnFrameMove(asdx::FrameEventArgs& args) override;
    void OnFrameRender(asdx::FrameEventArgs& args) override;
    void OnResize(const asdx::ResizeEventArgs& args) override;
    void OnKey(const asdx::KeyEventArgs& args) override;
    void OnMouse(const asdx::MouseEventArgs& args) override;
    void OnTyping(uint32_t keyCode) override;

    void Render(ID3D12GraphicsCommandList6* pCmdList);
    void CaptureResource(ID3D12Resource* pResource);
    void DispatchRay(ID3D12GraphicsCommandList6* pCmdList);

    bool InitGBufferPass();
    bool InitRayTracingPass();
    bool InitTonemapPass();
    bool InitTemporalAntiAliasPass();
};