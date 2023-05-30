//-----------------------------------------------------------------------------
// File : App.cpp
// Desc : Renderer Application.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <App.h>
#include <fnd/asdxStopWatch.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxMisc.h>
#include <gfx/asdxShaderCompiler.h>
#include <fpng.h>
#include <process.h>


// For Agility SDK
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 610;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const size_t     REQUEST_BIT_INDEX   = 0;
static const size_t     RELOADED_BIT_INDEX  = 1;
static const uint32_t   MAX_ITERATION       = 16;


//-----------------------------------------------------------------------------
// Shaders
//-----------------------------------------------------------------------------
#include "../res/shaders/Compiled/PathTracing.inc"
#include "../res/shaders/Compiled/ModelVS.inc"
#include "../res/shaders/Compiled/ModelPS.inc"
#include "../res/shaders/Compiled/TonemapCS.inc"
#if RTC_TARGET == RTC_DEVELOP
#include "../asdx12/res/shaders/Compiled/FullScreenVS.inc"
#include "../res/shaders/Compiled/DebugPS.inc"
#include "../res/shaders/Compiled/LineVS.inc"
#include "../res/shaders/Compiled/LinePS.inc"
#include "../res/shaders/Compiled/CopyDepthPS.inc"
#endif


///////////////////////////////////////////////////////////////////////////////
// SceneParams structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParams
{
    asdx::Matrix    View;
    asdx::Matrix    Proj;
    asdx::Matrix    InvView;
    asdx::Matrix    InvProj;
    asdx::Matrix    InvViewProj;

    asdx::Matrix    PrevView;
    asdx::Matrix    PrevProj;
    asdx::Matrix    PrevInvView;
    asdx::Matrix    PrevInvProj;
    asdx::Matrix    PrevInvViewProj;

    asdx::Vector4   ScreenSize;
    asdx::Vector3   CameraDir;
    uint32_t        MaxIteration;

    uint32_t        FrameIndex;
    float           AnimationTimeSec;
    uint32_t        EnableAccumulation;
    uint32_t        AccumulatedFrames;

    int32_t         DebugRayIndexOfX;
    int32_t         DebugRayIndexOfY;
    uint32_t        Reserved[2];
};

///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    uint32_t        InstanceId;
    uint32_t        PrimitiveId;
    asdx::Vector2   Barycentrics;
};

//-----------------------------------------------------------------------------
//      描画結果を画像に出力します.
//-----------------------------------------------------------------------------
unsigned ExportRenderedImage(void* args)
{
    auto image = reinterpret_cast<ExportImage*>(args);
    if (image == nullptr)
    { return -1; }

    // コピーコマンドの完了を待機.
    if (image->WaitPoint.IsValid())
    { image->pQueue->Sync(image->WaitPoint); }

    uint8_t* ptr = nullptr;

    auto hr = image->pReadBackTexture->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
    if (SUCCEEDED(hr))
    {
        char path[256] = {};
        sprintf_s(path, "output_%03ld.png", image->FrameIndex);

        // ファイルに出力.
        if (fpng::fpng_encode_image_to_memory(
            ptr,
            image->Width,
            image->Height,
            4,
            image->Converted))
        {
            FILE* pFile = nullptr;
            auto err = fopen_s(&pFile, path, "wb");
            if (err == 0)
            {
                fwrite(image->Converted.data(), 1, image->Converted.size(), pFile);
                fclose(pFile);
            }
        }
    }
    image->pReadBackTexture->Unmap(0, nullptr);

    return 0;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// App::RayTracingPipeline structure
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理.
//-----------------------------------------------------------------------------
bool App::RayTracingPipeline::Init
(
    ID3D12RootSignature*    pRootSignature,
    const void*             pBinary,
    size_t                  binarySize
)
{
    auto pDevice = asdx::GetD3D12Device();

    // レイトレ用パイプラインステート生成.
    {
        D3D12_EXPORT_DESC exports[] = {
            { L"OnGenerateRay"      , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnClosestHit"       , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowAnyHit"     , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnMiss"             , nullptr, D3D12_EXPORT_FLAG_NONE },
            { L"OnShadowMiss"       , nullptr, D3D12_EXPORT_FLAG_NONE },
        };

        D3D12_HIT_GROUP_DESC groups[2] = {};
        groups[0].ClosestHitShaderImport    = L"OnClosestHit";
        groups[0].HitGroupExport            = L"StandardHit";
        groups[0].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        groups[1].AnyHitShaderImport        = L"OnShadowAnyHit";
        groups[1].HitGroupExport            = L"ShadowHit";
        groups[1].Type                      = D3D12_HIT_GROUP_TYPE_TRIANGLES;

        asdx::RayTracingPipelineStateDesc desc = {};
        desc.pGlobalRootSignature       = pRootSignature;
        desc.DXILLibrary                = { pBinary, binarySize };
        desc.ExportCount                = _countof(exports);
        desc.pExports                   = exports;
        desc.HitGroupCount              = _countof(groups);
        desc.pHitGroups                 = groups;
        desc.MaxPayloadSize             = sizeof(Payload);
        desc.MaxAttributeSize           = sizeof(asdx::Vector2);
        desc.MaxTraceRecursionDepth     = MAX_ITERATION;

        if (!PipelineState.Init(pDevice, desc))
        {
            ELOGA("Error : RayTracing PSO Failed.");
            return false;
        }
    }

    // レイ生成テーブル.
    {
        asdx::ShaderRecord record = {};
        record.ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnGenerateRay");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount    = 1;
        desc.pRecords       = &record;

        if (!RayGen.Init(pDevice, &desc))
        {
            ELOGA("Error : RayGenTable Init Failed.");
            return false;
        }
    }

    // ミステーブル.
    {
        asdx::ShaderRecord record[2] = {};
        record[0].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnMiss");
        record[1].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"OnShadowMiss");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!Miss.Init(pDevice, &desc))
        {
            ELOGA("Error : MissTable Init Failed.");
            return false;
        }
    }

    // ヒットグループ.
    {
        asdx::ShaderRecord record[2];
        record[0].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"StandardHit");
        record[1].ShaderIdentifier = PipelineState.GetShaderIdentifier(L"ShadowHit");

        asdx::ShaderTable::Desc desc = {};
        desc.RecordCount = 2;
        desc.pRecords    = record;

        if (!HitGroup.Init(pDevice, &desc))
        {
            ELOGA("Error : HitGroupTable Init Failed.");
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理.
//-----------------------------------------------------------------------------
void App::RayTracingPipeline::Term()
{
    HitGroup     .Term();
    Miss         .Term();
    RayGen       .Term();
    PipelineState.Term();
}

//-----------------------------------------------------------------------------
//      レイトレーシングパイプラインを起動します.
//-----------------------------------------------------------------------------
void App::RayTracingPipeline::DispatchRays(ID3D12GraphicsCommandList6* pCmd, uint32_t w, uint32_t h)
{
    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.HitGroupTable              = HitGroup.GetTableView();
    desc.MissShaderTable            = Miss.GetTableView();
    desc.RayGenerationShaderRecord  = RayGen.GetRecordView();
    desc.Width                      = w;
    desc.Height                     = h;
    desc.Depth                      = 1;

    pCmd->SetPipelineState1(PipelineState.GetStateObject());
    pCmd->DispatchRays(&desc);
}

///////////////////////////////////////////////////////////////////////////////
// App class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
App::App(const RenderDesc& desc)
: asdx::Application(L"rtc alpha 0.0", desc.Width, desc.Height, nullptr, nullptr, nullptr)
, m_RenderDesc(desc)
{
#if RTC_TARGET == RTC_DEVELOP
    // 開発版.
    m_CreateWindow = true;

    m_DeviceDesc.EnableBreakOnError     = true;
    m_DeviceDesc.EnableBreakOnWarning   = false;
    m_DeviceDesc.EnableDRED             = true;
    m_DeviceDesc.EnableCapture          = true;
    m_DeviceDesc.EnableDebug            = true;

    m_SwapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#else
    // 提出版.
    m_CreateWindow = false;

    m_DeviceDesc.EnableBreakOnError     = false;
    m_DeviceDesc.EnableBreakOnWarning   = false;
    m_DeviceDesc.EnableDRED             = false;
    m_DeviceDesc.EnableCapture          = false;
    m_DeviceDesc.EnableDebug            = false;
#endif
}

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
App::~App()
{
}

//-----------------------------------------------------------------------------
//      初期化処理です.
//-----------------------------------------------------------------------------
bool App::OnInit()
{
    asdx::StopWatch timer;
    timer.Start();

    auto pDevice = asdx::GetD3D12Device();

    // DXRが使用可能かどうかチェック.
    if (!asdx::IsSupportDXR(pDevice))
    {
        ELOGA("Error : DirectX Ray Tracing is not supported.");
        return false;
    }

    // Shader Model 6.6 以降対応かどうかチェック.
    {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
        auto hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
        if (FAILED(hr) || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6))
        {
            ELOG("Error : Shader Model 6.6 is not supported.");
            return false;
        }
    }

    // PNGライブラリ初期化.
    fpng::fpng_init();

    // コマンドリストリセット.
    m_GfxCmdList.Reset();
    auto pCmd = m_GfxCmdList.GetCommandList();

    #if ASDX_ENABLE_IMGUI
    // ImGui初期化.
    {
        const auto path = "../res/fonts/07やさしさゴシック.ttf";
        if (!asdx::GuiMgr::Instance().Init(pCmd, m_hWnd, m_Width, m_Height, m_SwapChainFormat, path))
        {
            ELOGA("Error : GuiMgr::Init() Failed.");
            return false;
        }
    }
    #endif

    // リードバックテクスチャ生成.
    {
    #if RTC_TARGET == RTC_DEVELOP
        const wchar_t* debugTag[EXPORT_COUNT] = {
            L"ReadBackTexture0",
            L"ReadBackTexture1",
        };
    #endif

        for(auto i=0u; i<EXPORT_COUNT; ++i)
        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            desc.Width              = UINT64(m_RenderDesc.Width * m_RenderDesc.Height * 4);
            desc.Height             = 1;
            desc.DepthOrArraySize   = 1;
            desc.MipLevels          = 1;
            desc.Format             = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

            D3D12_HEAP_PROPERTIES props = {};
            props.Type = D3D12_HEAP_TYPE_READBACK;

            auto hr = pDevice->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(m_ReadBackTexture[i].GetAddress()));

            if (FAILED(hr))
            {
                ELOGA("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
                return false;
            }

            RTC_DEBUG_CODE(m_ReadBackTexture[i]->SetName(debugTag[i]));
        }

        UINT   rowCount     = 0;
        UINT64 pitchSize    = 0;
        UINT64 resSize      = 0;

        D3D12_RESOURCE_DESC dstDesc = {};
        dstDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dstDesc.Alignment           = 0;
        dstDesc.Width               = m_RenderDesc.Width;
        dstDesc.Height              = m_RenderDesc.Height;
        dstDesc.DepthOrArraySize    = 1;
        dstDesc.MipLevels           = 1;
        dstDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
        dstDesc.SampleDesc.Count    = 1;
        dstDesc.SampleDesc.Quality  = 0;
        dstDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        dstDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        pDevice->GetCopyableFootprints(
            &dstDesc,
            0,
            1,
            0,
            nullptr,
            &rowCount,
            &pitchSize,
            &resSize);

        m_ReadBackPitch = static_cast<uint32_t>((pitchSize + 255) & ~0xFFu);
    }

    // エクスポートデータ生成.
    {
        for(auto i=0u; i<EXPORT_COUNT; ++i)
        {
            m_ExportImages[i].pQueue            = asdx::GetCopyQueue();
            m_ExportImages[i].pReadBackTexture  = m_ReadBackTexture[i].GetPtr();
            m_ExportImages[i].Width             = m_RenderDesc.Width;
            m_ExportImages[i].Height            = m_RenderDesc.Height;
            m_ExportImages[i].FrameIndex        = 0;
        }
    }

    // 定数バッファ.
    {
        auto size = asdx::RoundUp(sizeof(SceneParams), 256);
        if (!m_SceneParam.Init(size))
        {
            ELOGA("Error : SceneParam Init Failed.");
            return false;
        }
    }

    // G-Bufferパス初期化.
    if (!InitGBufferPass())
    {
        ELOG("Error : G-Buffer Pass Init Failed.");
        return false;
    }

    // レイトレーシングパス初期化.
    if (!InitRayTracingPass())
    {
        ELOG("Error : RayTracing Pass Init Failed.");
        return false;
    }

    // トーンマップパス初期化.
    if (!InitTonemapPass())
    {
        ELOG("Error : Tonemap Pass Init Failed.");
        return false;
    }

    // TemporalAAパス初期化.
    if (!InitTemporalAntiAliasPass())
    {
        ELOG("Error : TemporalAA Pass Init Failed.");
        return false;
    }

    #if RTC_TARGET == RTC_DEVELOP
    if (!InitForTest(pCmd))
    {
        ELOG("Error : InitForTest() Failed.");
        return false;
    }

    if (!InitDebugPass())
    {
        ELOG("Error : DebugPass Init Failed.");
        return false;
    }

    // カメラ初期化.
    {
        //auto pos    = asdx::Vector3(0.0f, 0.0f, 300.5f);
        auto pos    = asdx::Vector3(0.0f, 0.0f, -2.0f);
        auto target = asdx::Vector3(0.0f, 0.0f, 0.0f);
        auto upward = asdx::Vector3(0.0f, 1.0f, 0.0f);
        m_AppCamera.Init(pos, target, upward, 1.0f, 1000.0f);

        auto fovY   = asdx::ToRadian(37.5f);
        auto aspect = float(m_RenderDesc.Width) / float(m_RenderDesc.Height);

        auto view = m_AppCamera.GetView();
        auto proj = asdx::Matrix::CreatePerspectiveFieldOfView(
            fovY,
            aspect,
            m_AppCamera.GetNearClip(),
            m_AppCamera.GetFarClip());

        auto invView = asdx::Matrix::Invert(view);
        auto invProj = asdx::Matrix::Invert(proj);

        m_CurrView    = view;
        m_CurrProj    = proj;
        m_CurrInvView = invView;
        m_CurrInvProj = invProj;

        m_PrevView    = view;
        m_PrevProj    = proj;
        m_PrevInvView = invView;
        m_PrevInvProj = invProj;
    }
    #endif

    // セットアップコマンド実行.
    {
        pCmd->Close();

        ID3D12CommandList* pCmds[] = {
            pCmd
        };

        auto pQueue = asdx::GetGraphicsQueue();

        // コマンドを実行.
        pQueue->Execute(_countof(pCmds), pCmds);

        // 待機点を発行.
        m_WaitPoint = pQueue->Signal();

        // 完了を待機.
        pQueue->Sync(m_WaitPoint);
    }

    timer.End();
    printf_s("Initialize End. %lf[msec]\n", timer.GetElapsedMsec());

    // 標準出力をフラッシュ.
    std::fflush(stdout);
    return true;
}

//-----------------------------------------------------------------------------
//      G-Bufferパスの初期化を行います.
//-----------------------------------------------------------------------------
bool App::InitGBufferPass()
{
    auto pDevice = asdx::GetD3D12Device();

    // G-Buffer ルートシグニチャ.
    {
        auto vs = D3D12_SHADER_VISIBILITY_VERTEX;
        auto ps = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE ranges[5] = {};
        asdx::InitRangeAsSRV(ranges[0], 0);
        asdx::InitRangeAsSRV(ranges[1], 1);
        asdx::InitRangeAsSRV(ranges[2], 0);
        asdx::InitRangeAsSRV(ranges[3], 1);
        asdx::InitRangeAsSRV(ranges[4], 2);

        D3D12_ROOT_PARAMETER params[6] = {};
        asdx::InitAsCBV(params[0], 0, vs);                    // SceneParams.
        asdx::InitAsTable(params[1], 1, &ranges[0], vs);      // Transforms.
        asdx::InitAsTable(params[2], 1, &ranges[1], vs);      // Vertices.
        asdx::InitAsTable(params[3], 1, &ranges[2], ps);      // Albedo.
        asdx::InitAsTable(params[4], 1, &ranges[3], ps);      // Normal.
        asdx::InitAsTable(params[5], 1, &ranges[4], ps);      // Roughness.

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters      = _countof(params);
        desc.pParameters        = params;
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.pStaticSamplers    = asdx::GetStaticSamplers();

        if (!asdx::InitRootSignature(pDevice, &desc, m_GBufferRootSig.GetAddress()))
        {
            ELOG("Error : GBuffer RootSignature Init Failed.");
            return false;
        }
    }

    //// G-Bufferパイプライン.
    //{
    //    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    //    desc.pRootSignature                     = m_GBufferRootSig.GetPtr();
    //    desc.VS                                 = { ModelVS, sizeof(ModelVS) };
    //    desc.PS                                 = { ModelPS, sizeof(ModelPS) };
    //    desc.BlendState                         = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
    //    desc.DepthStencilState                  = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
    //    desc.RasterizerState                    = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
    //    desc.SampleMask                         = D3D12_DEFAULT_SAMPLE_MASK;
    //    desc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    //    desc.NumRenderTargets                   = 4;
    //    desc.RTVFormats[0]                      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    //    desc.RTVFormats[1]                      = DXGI_FORMAT_R16G16_FLOAT;
    //    desc.RTVFormats[2]                      = DXGI_FORMAT_R8_UNORM;
    //    desc.RTVFormats[3]                      = DXGI_FORMAT_R16G16_FLOAT;
    //    desc.DSVFormat                          = DXGI_FORMAT_D32_FLOAT;
    //    desc.InputLayout.NumElements            = 0;
    //    desc.InputLayout.pInputElementDescs     = nullptr;
    //    desc.SampleDesc.Count                   = 1;
    //    desc.SampleDesc.Quality                 = 0;

    //    if (!m_GBufferPipelineState.Init(pDevice, &desc))
    //    {
    //        ELOG("Error : G-Buffer Pipeline Init Failed.");
    //        return false;
    //    }
    //}

    // アルベド用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 1.0f;

        if (!m_Albedo.Init(&desc))
        {
            ELOG("Error : Albedo Init Failed.");
            return false;
        }

        m_Albedo.SetName(L"AlbedoBuffer");
    }

    // 法線用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Normal.Init(&desc))
        {
            ELOG("Error : Normal Init Failed.");
            return false;
        }

        m_Normal.SetName(L"NormalBuffer");
    }

    // ラフネス用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Roughness.Init(&desc))
        {
            ELOG("Error : Roughness Init Failed.");
            return false;
        }

        m_Roughness.SetName(L"RoughnessBuffer");
    }

    // 速度用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_COMMON;
        desc.ClearColor[0]      = 0.0f;
        desc.ClearColor[1]      = 0.0f;
        desc.ClearColor[2]      = 0.0f;
        desc.ClearColor[3]      = 0.0f;

        if (!m_Velocity.Init(&desc))
        {
            ELOG("Error : Velocity Init Failed.");
            return false;
        }

        m_Velocity.SetName(L"VelocityBuffer");
    }

    // 深度用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.MipLevels          = 1;
        desc.Format             = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        desc.ClearDepth         = 1.0f;
        desc.ClearStencil       = 0;

        if (!m_Depth.Init(&desc))
        {
            ELOG("Error : Depth Init Failed.");
            return false;
        }

        m_Depth.SetName(L"DepthBuffer");
    }

    return true;
}

//-----------------------------------------------------------------------------
//      レイトレーシングパスの初期化を行います.
//-----------------------------------------------------------------------------
bool App::InitRayTracingPass()
{
    auto pDevice = asdx::GetD3D12Device();

    // レイトレーシングパス.
    {
        D3D12_SHADER_VISIBILITY cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srvRange[1] = {};
        asdx::InitRangeAsSRV(srvRange[0], 5);

        D3D12_DESCRIPTOR_RANGE uavRange[3] = {};
        asdx::InitRangeAsUAV(uavRange[0], 0);
        asdx::InitRangeAsUAV(uavRange[1], 1);
        asdx::InitRangeAsUAV(uavRange[2], 2);

        D3D12_ROOT_PARAMETER params[8] = {};
        asdx::InitAsCBV  (params[0], 0, cs);
        asdx::InitAsSRV  (params[1], 0, cs);
        asdx::InitAsSRV  (params[2], 1, cs);
        asdx::InitAsSRV  (params[3], 2, cs);
        asdx::InitAsTable(params[4], 1, &srvRange[0], cs);
        asdx::InitAsTable(params[5], 1, &uavRange[0], cs);
        asdx::InitAsTable(params[6], 1, &uavRange[1], cs);
        asdx::InitAsTable(params[7], 1, &uavRange[2], cs);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters       = _countof(params);
        desc.pParameters         = params;
        desc.NumStaticSamplers   = asdx::GetStaticSamplerCounts();
        desc.pStaticSamplers     = asdx::GetStaticSamplers();

        if (!asdx::InitRootSignature(pDevice, &desc, m_RayTracingRootSig.GetAddress()))
        {
            ELOG("Error : RayTracing RootSignature Init Failed.");
            return false;
        }
    }

    // パイプラインステート.
    {
        if (!m_RayTracingPipeline.Init(m_RayTracingRootSig.GetPtr(), PathTracing, sizeof(PathTracing)))
        {
            ELOG("Error : PathTracing Pipeline Init Failed.");
            return false;
        }
    }

    // レイトレ用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_Radiance.Init(&desc))
        {
            ELOG("Error : Radiance Init Failed.");
            return false;
        }

        m_Radiance.SetName(L"Radiance");
    }

    return true;
}

//-----------------------------------------------------------------------------
//      トーンマップパスの初期化を行います.
//-----------------------------------------------------------------------------
bool App::InitTonemapPass()
{
    auto pDevice = asdx::GetD3D12Device();

    // ポストプロセス用ルートシグニチャ.
    {
        auto cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE uavRanges[1] = {};
        asdx::InitRangeAsUAV(uavRanges[0], 0);

        D3D12_DESCRIPTOR_RANGE srvRanges[2] = {};
        asdx::InitRangeAsSRV(srvRanges[0], 0);
        asdx::InitRangeAsSRV(srvRanges[1], 1);

        D3D12_ROOT_PARAMETER params[4] = {};
        asdx::InitAsCBV  (params[0], 0, cs);
        asdx::InitAsTable(params[1], 1, &uavRanges[0], cs);
        asdx::InitAsTable(params[2], 1, &srvRanges[0], cs);
        asdx::InitAsTable(params[3], 1, &srvRanges[1], cs);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();

        if (!asdx::InitRootSignature(pDevice, &desc, m_PostProcessRootSig.GetAddress()))
        {
            ELOG("Error : PostProcessRootSig Init Failed.");
            return false;
        }
    }

    // トーンマップパイプライン.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_PostProcessRootSig.GetPtr();
        desc.CS             = { TonemapCS, sizeof(TonemapCS) };

        if (!m_TonemapPipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : Tonemap Pipeline Init Failed.");
            return false;
        }
    }

    // トーンマップ用ターゲット.
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (!m_Tonemaped.Init(&desc))
        {
            ELOG("Error : Tonemapped Init Failed.");
            return false;
        }

        m_Tonemaped.SetName(L"TonemapBuffer");
    }

    return true;
}

//-----------------------------------------------------------------------------
//      テンポラルAAパスの初期化を行います.
//-----------------------------------------------------------------------------
bool App::InitTemporalAntiAliasPass()
{
    // パイプラインステート.
    {
    }

    // カラーヒストリーバッファ
    {
        asdx::TargetDesc desc;
        desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width              = m_RenderDesc.Width;
        desc.Height             = m_RenderDesc.Height;
        desc.DepthOrArraySize   = 1;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MipLevels          = 1;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.InitState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        for(auto i=0u; i<2; ++i)
        {
            if (!m_ColorHistory[i].Init(&desc))
            {
                ELOG("Error : ColorHistory[%u] Init Failed.", i);
                return false;
            }
        }

        m_CurrHistoryIndex = 0;
        m_PrevHistoryIndex = 1;

        m_ColorHistory[0].SetName(L"ColorHistory0");
        m_ColorHistory[1].SetName(L"ColorHistory1");
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理です.
//-----------------------------------------------------------------------------
void App::OnTerm()
{
    asdx::StopWatch timer;
    timer.Start();

#if ASDX_ENABLE_IMGUI
    asdx::GuiMgr::Instance().Term();
#endif

    m_SceneParam.Term();

    m_Radiance  .Term();
    m_Albedo    .Term();
    m_Normal    .Term();
    m_Roughness .Term();
    m_Velocity  .Term();
    m_Depth     .Term();
    m_Denoised  .Term();
    m_Tonemaped .Term();

    for(auto i=0u; i<2; ++i)
    { m_ColorHistory[i].Term(); }

    for(auto i=0u; i<EXPORT_COUNT; ++i)
    {
        m_Capture[i].Term();
        m_ReadBackTexture[i].Reset();
    }

    m_GBufferPipelineState.Term();
    m_TonemapPipelineState.Term();
    m_DenoisePipelineState.Term();
    m_TaaPipelineState    .Term();
    m_RayTracingPipeline  .Term();

    m_GBufferRootSig    .Reset();
    m_PostProcessRootSig.Reset();
    m_RayTracingRootSig .Reset();

    RTC_DEBUG_CODE(m_DevRayTracingPipeline.Term());
    RTC_DEBUG_CODE(m_DebugRootSignature.Reset());
    RTC_DEBUG_CODE(m_DebugPipelineState.Term());
    RTC_DEBUG_CODE(m_RayPoints.Term());
    RTC_DEBUG_CODE(m_DrawArgs.Term());
    RTC_DEBUG_CODE(m_LinePipelineState.Term());
    RTC_DEBUG_CODE(m_CopyDepthPipelineState.Term());
    RTC_DEBUG_CODE(m_DrawCommandSig.Reset());

#if 1
    m_VB.Reset();
    m_IB.Reset();
    m_VertexSRV.Reset();
    m_IndexSRV.Reset();
    m_BLAS.Term();
    m_TLAS.Term();
#endif

    timer.End();
    printf_s("Terminate Process : %lf[msec]\n", timer.GetElapsedMsec());
}

//-----------------------------------------------------------------------------
//      フレーム遷移処理.
//-----------------------------------------------------------------------------
void App::OnFrameMove(asdx::FrameEventArgs& args)
{
#if RTC_TARGET == RTC_RELEASE
    // 制限時間を超えたら自動終了.
    if (args.Time >= m_RenderDesc.RenderTimeSec)
    {
        PostQuitMessage(0);
        m_RequestTerminate = true;
        return;
    }
#endif

    m_PrevView    = m_CurrView;
    m_PrevProj    = m_CurrProj;
    m_PrevInvView = m_CurrInvView;
    m_PrevInvProj = m_CurrInvProj;

#if RTC_TARGET == RTC_DEVELOP
    // カメラ更新.
    {
        auto aspectRatio = float(m_RenderDesc.Width) / float(m_RenderDesc.Height);

        m_CurrView = m_AppCamera.GetView();
        m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
            asdx::ToRadian(37.5f),
            aspectRatio,
            m_AppCamera.GetNearClip(),
            m_AppCamera.GetFarClip());
        m_CameraDir = m_AppCamera.GetAxisZ();
    }
#else
    // カメラ更新.
    {
    }
#endif

    m_CurrInvView = asdx::Matrix::Invert(m_CurrView);
    m_CurrInvProj = asdx::Matrix::Invert(m_CurrProj);

    // 定数バッファ更新.
    {
        auto enableAccumulation = true;

        auto changed = false;
        changed |= (memcmp(&m_CurrView, &m_PrevView, sizeof(asdx::Matrix)) != 0);
        changed |= (memcmp(&m_CurrProj, &m_PrevProj, sizeof(asdx::Matrix)) != 0);

    #if RTC_TARGET == RTC_DEVELOP
        if (m_DirtyShader)
        {
            changed       = true;
            m_DirtyShader = false;
        }

        // カメラフリーズされていないときは更新.
        if (!m_FreezeCamera)
        {
            m_FreezeCurrView    = m_CurrView;
            m_FreezeCurrProj    = m_CurrProj;
            m_FreezeCurrInvView = m_CurrInvView;
            m_FreezeCurrInvProj = m_CurrInvProj;

            m_FreezePrevView    = m_PrevView;
            m_FreezePrevProj    = m_PrevProj;
            m_FreezePrevInvView = m_PrevInvView;
            m_FreezePrevInvProj = m_PrevInvProj;

            m_FreezeCameraDir = m_CameraDir;
        }
    #endif

        // カメラ変更があった場合はアキュームレーションを無効化.
        if (changed)
        {
            enableAccumulation  = false;
            m_AccumulatedFrames = 0;
        }

        m_AccumulatedFrames++;

        SceneParams params = {};
        params.View                 = m_CurrView;
        params.Proj                 = m_CurrProj;
        params.InvView              = m_CurrInvView;
        params.InvProj              = m_CurrInvProj;
        params.InvViewProj          = m_CurrInvProj * m_CurrInvView;
        params.PrevView             = m_PrevView;
        params.PrevProj             = m_PrevProj;
        params.PrevInvView          = m_PrevInvView;
        params.PrevInvProj          = m_PrevInvProj;
        params.PrevInvViewProj      = m_PrevInvProj * m_PrevInvView;
        params.ScreenSize.x         = float(m_RenderDesc.Width);
        params.ScreenSize.y         = float(m_RenderDesc.Height);
        params.ScreenSize.z         = 1.0f / params.ScreenSize.x;
        params.ScreenSize.w         = 1.0f / params.ScreenSize.y;
        params.CameraDir            = m_CameraDir;
        params.FrameIndex           = GetFrameCount();
        params.MaxIteration         = MAX_ITERATION;
        params.EnableAccumulation   = (enableAccumulation) ? 1 : 0;
        params.AccumulatedFrames    = m_AccumulatedFrames;
        params.DebugRayIndexOfX     = -1;
        params.DebugRayIndexOfY     = -1;

    #if RTC_TARGET == RTC_DEVELOP
        // レイデバッグ番号を設定.
        params.DebugRayIndexOfX     = m_DebugRayIndexOfX;
        params.DebugRayIndexOfY     = m_DebugRayIndexOfY;

        // カメラフリーズの場合は上書き.
        if (m_FreezeCamera)
        {
            params.View             = m_FreezeCurrView;
            params.Proj             = m_FreezeCurrProj;
            params.InvView          = m_FreezeCurrInvView;
            params.InvProj          = m_FreezeCurrInvProj;
            params.InvViewProj      = m_FreezeCurrInvProj * m_FreezeCurrInvProj;
            params.PrevView         = m_FreezePrevView;
            params.PrevProj         = m_FreezePrevProj;
            params.PrevInvView      = m_FreezePrevInvView;
            params.PrevInvProj      = m_FreezePrevInvProj;
            params.PrevInvViewProj  = m_FreezePrevInvProj * m_FreezePrevInvView;
            params.CameraDir        = m_FreezeCameraDir;
        }
    #endif

        m_SceneParam.SwapBuffer();
        m_SceneParam.Update(&params, sizeof(params));
    }

    // アニメーション関連更新.
    {
    }
}

//-----------------------------------------------------------------------------
//      フレーム描画処理.
//-----------------------------------------------------------------------------
void App::OnFrameRender(asdx::FrameEventArgs& args)
{
    if (m_RequestTerminate)
    { return; }

    m_GfxCmdList.Reset();

    auto pCmd = m_GfxCmdList.GetCommandList();

    Render(pCmd);

#if RTC_TARGET == RTC_DEVELOP
    // スワップチェインに描画.
    {
        auto idx = GetCurrentBackBufferIndex();
        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
            m_ColorTarget[idx].GetRTV()->GetHandleCPU()
        };

        auto dsv = m_DepthTarget.GetDSV()->GetHandleCPU();

        pCmd->OMSetRenderTargets(_countof(rtvs), rtvs, FALSE, &dsv);
        pCmd->ClearRenderTargetView(rtvs[0], m_ClearColor, 0, nullptr);
        pCmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        pCmd->RSSetViewports(1, &m_Viewport);
        pCmd->RSSetScissorRects(1, &m_ScissorRect);

        // 深度コピー.
        pCmd->SetGraphicsRootSignature(m_DebugRootSignature.GetPtr());
        m_CopyDepthPipelineState.SetState(pCmd);
        pCmd->SetGraphicsRootDescriptorTable(1, m_Depth.GetSRV()->GetHandleGPU());
        asdx::DrawQuad(pCmd);

        Draw2D(pCmd);

        m_ColorTarget[idx].Transition(pCmd, D3D12_RESOURCE_STATE_PRESENT);
    }
#endif

    // コマンド記録終了.
    pCmd->Close();

    auto pQueue = asdx::GetGraphicsQueue();

    ID3D12CommandList* pCmds[] = {
        pCmd
    };

    // 前フレームの描画の完了を待機.
    if (m_WaitPoint.IsValid())
    { pQueue->Sync(m_WaitPoint); }

    // コマンドを実行.
    pQueue->Execute(_countof(pCmds), pCmds);

    // 待機点を発行.
    m_WaitPoint = pQueue->Signal();

    // 画面に表示.
    Present(1);

    // フレーム同期.
    asdx::FrameSync();

    // シェーダリロード処理.
    RTC_DEBUG_CODE(ReloadShader());
}

//-----------------------------------------------------------------------------
//      リサイズ処理.
//-----------------------------------------------------------------------------
void App::OnResize(const asdx::ResizeEventArgs& args)
{
}

//-----------------------------------------------------------------------------
//      キー処理.
//-----------------------------------------------------------------------------
void App::OnKey(const asdx::KeyEventArgs& args)
{
    #if ASDX_ENABLE_IMGUI
    {
        asdx::GuiMgr::Instance().OnKey(
            args.IsKeyDown,
            args.IsKeyDown,
            args.KeyCode);
    }
    #endif

    #if RTC_TARGET == RTC_DEVELOP
    {
        m_AppCamera.OnKey(args.KeyCode, args.IsKeyDown, args.IsAltDown);

        if (args.IsKeyDown)
        {
            switch(args.KeyCode)
            {
            // シェーダリロード.
            case VK_F7:
                {
                    m_RayTracingReloadFlags.Set(REQUEST_BIT_INDEX, true);
                    m_GBufferReloadFlags   .Set(REQUEST_BIT_INDEX, true);
                    m_TonemapReloadFlags   .Set(REQUEST_BIT_INDEX, true);
                }
                break;
            }
        }
    }
    #endif
}

//-----------------------------------------------------------------------------
//      マウス処理.
//-----------------------------------------------------------------------------
void App::OnMouse(const asdx::MouseEventArgs& args)
{
    auto isAltDown = !!(GetAsyncKeyState(VK_MENU) & 0x8000);
    RTC_UNUSED(isAltDown);

    #if ASDX_ENABLE_IMGUI
    {
        if (!isAltDown) {
            asdx::GuiMgr::Instance().OnMouse(
                args.X,
                args.Y,
                args.WheelDelta,
                args.IsLeftButtonDown,
                args.IsMiddleButtonDown,
                args.IsRightButtonDown);
        }
    }
    #endif

    #if RTC_TARGET == RTC_DEVELOP
    if (isAltDown)
    {
        m_AppCamera.OnMouse(
            args.X,
            args.Y,
            args.WheelDelta,
            args.IsLeftButtonDown,
            args.IsRightButtonDown,
            args.IsMiddleButtonDown,
            args.IsSideButton1Down,
            args.IsSideButton2Down);
    }
    #endif
}

//-----------------------------------------------------------------------------
//      タイピング処理.
//-----------------------------------------------------------------------------
void App::OnTyping(uint32_t keyCode)
{
    #if ASDX_ENABLE_IMGUI
    {
        asdx::GuiMgr::Instance().OnTyping(keyCode);
    }
    #endif
}

//-----------------------------------------------------------------------------
//      リソースをキャプチャーします.
//-----------------------------------------------------------------------------
void App::CaptureResource(ID3D12Resource* pResource)
{
    if (pResource == nullptr)
    { return; }

    // ※リソースは事前にTransitionBarrierで変更済みとします.
    //   (コピーキューではTransitionBarrierが実行できないため).

    auto pQueue = asdx::GetCopyQueue();
    if (pQueue == nullptr)
    { return; }

    m_CopyCmdList.Reset();
    auto pCmd = m_CopyCmdList.GetCommandList();

    auto exportImage = &m_ExportImages[m_ExportIndex];

    // リードバック実行.
    {
        auto pReadBackTexture = m_ReadBackTexture[m_ExportIndex].GetPtr();

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.Type                                = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource                           = pReadBackTexture;
        dst.PlacedFootprint.Footprint.Width     = static_cast<UINT>(m_RenderDesc.Width);
        dst.PlacedFootprint.Footprint.Height    = m_RenderDesc.Height;
        dst.PlacedFootprint.Footprint.Depth     = 1;
        dst.PlacedFootprint.Footprint.RowPitch  = m_ReadBackPitch;
        dst.PlacedFootprint.Footprint.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.Type                = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource           = pResource;
        src.SubresourceIndex    = 0;

        D3D12_BOX box = {};
        box.left    = 0;
        box.right   = m_RenderDesc.Width;
        box.top     = 0;
        box.bottom  = m_RenderDesc.Height;
        box.front   = 0;
        box.back    = 1;

        // コピーコマンド.
        pCmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        // コマンドの記録を終了.
        pCmd->Close();

        ID3D12CommandList* pCmds[] = {
            pCmd
        };

        // コマンドを実行.
        pQueue->Execute(_countof(pCmds), pCmds);

        exportImage->WaitPoint          = pQueue->Signal();
        exportImage->pQueue             = pQueue;
        exportImage->FrameIndex         = m_CaptureIndex;
        exportImage->pReadBackTexture   = pReadBackTexture;
    }

    // スレッド実行.
    _beginthreadex(nullptr, 0, ExportRenderedImage, exportImage, 0, nullptr);

    m_CaptureIndex++;
    m_ExportIndex = (m_ExportIndex + 1) % EXPORT_COUNT;
}

//-----------------------------------------------------------------------------
//      レイトレーシングパイプラインを起動します.
//-----------------------------------------------------------------------------
void App::DispatchRay(ID3D12GraphicsCommandList6* pCmd)
{
#if RTC_TARGET == RTC_DEVELOP
    if (m_RayTracingReloadFlags.Get(RELOADED_BIT_INDEX))
    {
        m_DevRayTracingPipeline.DispatchRays(
            pCmd, m_RenderDesc.Width, m_RenderDesc.Height);
        return;
    }
#endif

    m_RayTracingPipeline.DispatchRays(
        pCmd, m_RenderDesc.Width, m_RenderDesc.Height);
}

#if RTC_TARGET == RTC_DEVELOP
//-----------------------------------------------------------------------------
//      更新ファイル対象があればフラグを立てます.
//-----------------------------------------------------------------------------
void CheckModify
(
    const char*         relativePath,
    asdx::BitFlags8&    flags,
    const char*         paths[],
    uint32_t            countPaths
)
{
    bool detect = false;

    // 更新対象に入っているかチェック.
    for(auto i=0u; i<countPaths; ++i)
    { detect |= (_stricmp(paths[i], relativePath) == 0); }

    // 対象ファイルを検出したらリロードフラグを立てる.
    if (detect)
    { flags.Set(REQUEST_BIT_INDEX, true); }
}

//-----------------------------------------------------------------------------
//      シェーダをコンパイルします.
//-----------------------------------------------------------------------------
bool CompileShader
(
    const wchar_t*  path,
    const char*     entryPoint,
    const char*     profile,
    asdx::IBlob**   ppBlob
)
{
    std::wstring resolvePath;

    if (!asdx::SearchFilePathW(path, resolvePath))
    {
        ELOGA("Error : File Not Found. path = %ls", path);
        return false;
    }

    std::vector<std::wstring> includeDirs;
    includeDirs.push_back(asdx::ToFullPathW(L"../external/asdx12/res/shaders"));
    includeDirs.push_back(asdx::ToFullPathW(L"../res/shaders"));

    if (!asdx::CompileFromFile(resolvePath.c_str(), includeDirs, entryPoint, profile, ppBlob))
    {
        ELOGA("Error : Compile Shader Failed. path = %ls", resolvePath.c_str());
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      シェーダのリロード.
//-----------------------------------------------------------------------------
void App::ReloadShader()
{
    auto successCount = 0;

    // レイトレーシング.
    if (m_RayTracingReloadFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> shader;

        // シェーダコンパイル.
        if (CompileShader(L"../res/shaders/PathTracing.hlsl", "", "lib_6_6", shader.GetAddress()))
        { 
            // 解放.
            m_DevRayTracingPipeline.Term();

            // リロードしたシェーダから生成.
            if (m_DevRayTracingPipeline.Init(
                m_RayTracingRootSig.GetPtr(),
                shader->GetBufferPointer(),
                shader->GetBufferSize()))
            {
                m_RayTracingReloadFlags.Set(RELOADED_BIT_INDEX, true);
                successCount++;
            }
        }

        m_RayTracingReloadFlags.Set(REQUEST_BIT_INDEX, false);
    }

    // トーンマッピング.
    if (m_TonemapReloadFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> shader;
        if (CompileShader(L"../res/shaders/TonemapCS.hlsl", "main", "cs_6_6", shader.GetAddress()))
        {
            m_TonemapPipelineState.ReplaceShader(
                asdx::SHADER_TYPE_CS,
                shader->GetBufferPointer(),
                shader->GetBufferSize());
            m_TonemapPipelineState.Rebuild();
            successCount++;
        }

        m_TonemapReloadFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (successCount > 0)
    {
        // リロード完了時刻を取得.
        tm local_time = {};
        auto t   = time(nullptr);
        auto err = localtime_s( &local_time, &t );

        // 成功ログを出力.
        ILOGA("Info : Shader Reload Successs!! [%04d/%02d/%02d %02d:%02d:%02d], successCount = %u",
            local_time.tm_year + 1900,
            local_time.tm_mon + 1,
            local_time.tm_mday,
            local_time.tm_hour,
            local_time.tm_min,
            local_time.tm_sec,
            successCount);

        m_DirtyShader = true;
    }
}

//-----------------------------------------------------------------------------
//      デバッグパスを初期化します.
//-----------------------------------------------------------------------------
bool App::InitDebugPass()
{
    auto pDevice = asdx::GetD3D12Device();

    // ルートシグニチャ.
    {
        auto ps = D3D12_SHADER_VISIBILITY_PIXEL;
        auto vs_ps = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srvRange[1] = {};
        asdx::InitRangeAsSRV(srvRange[0], 0);

        D3D12_ROOT_PARAMETER params[3] = {};
        asdx::InitAsConstants(params[0], 0, 1, ps);
        asdx::InitAsTable(params[1], 1, &srvRange[0], vs_ps);
        asdx::InitAsCBV(params[2], 1, vs_ps);

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.pParameters        = params;
        desc.NumParameters      = _countof(params);
        desc.pStaticSamplers    = asdx::GetStaticSamplers();
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.Flags              = flags;

        if (!asdx::InitRootSignature(pDevice, &desc, m_DebugRootSignature.GetAddress()))
        {
            ELOG("Error : DebugRootSignature Init Failed.");
            return false;
        }
    }

    // パイプラインステート.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = m_DebugRootSignature.GetPtr();
        desc.VS                     = { FullScreenVS, sizeof(FullScreenVS) };
        desc.PS                     = { DebugPS, sizeof(DebugPS) };
        desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_NONE);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat              = DXGI_FORMAT_UNKNOWN;
        desc.InputLayout            = asdx::GetQuadLayout();
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;
            
        if (!m_DebugPipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : DebugPipelineState Init Failed.");
            return false;
        }
    }

    // レイバッファ.
    {
        auto size = sizeof(asdx::Vector4) * (MAX_ITERATION + 1);

        if (!m_RayPoints.Init(size, D3D12_RESOURCE_STATE_COMMON))
        {
            ELOG("Error : Ray Point Init Failed.");
            return false;
        }       
    }

    // 描画引数バッファ.
    {
        uint32_t stride = sizeof(D3D12_DRAW_ARGUMENTS);

        if (!m_DrawArgs.Init(1, stride, D3D12_RESOURCE_STATE_COMMON))
        {
            ELOG("Error : Draw Args Init Failed.");
            return false;
        }
    }

    // ライン描画用パイプラインステート.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = m_DebugRootSignature.GetPtr();
        desc.VS                     = { LineVS, sizeof(LineVS) };
        desc.PS                     = { LinePS, sizeof(LinePS) };
        desc.BlendState             = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_READ_ONLY);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat              = DXGI_FORMAT_D32_FLOAT;
        desc.InputLayout            = asdx::GetQuadLayout();
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;

        if (!m_LinePipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : Line Pipeline Init Failed.");
            return false;
        }
    }

    // コマンドシグニチャ
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride         = sizeof(D3D12_DRAW_ARGUMENTS);
        desc.NumArgumentDescs   = 1;
        desc.pArgumentDescs     = &argDesc;
        desc.NodeMask           = 0;

        auto hr = pDevice->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(m_DrawCommandSig.ReleaseAndGetAddress()));
        if (FAILED(hr))
        {
            ELOG("Error : DrawCommandSig Init Failed.");
            return false;
        }
    }

    // 深度コピー用パイプラインステート
    {
        auto blendState = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        blendState.RenderTarget[0].RenderTargetWriteMask = 0;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature         = m_DebugRootSignature.GetPtr();
        desc.VS                     = { FullScreenVS, sizeof(FullScreenVS) };
        desc.PS                     = { CopyDepthPS, sizeof(CopyDepthPS) };
        desc.BlendState             = blendState;
        desc.DepthStencilState      = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_READ_ONLY);
        desc.RasterizerState        = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask             = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType  = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets       = 1;
        desc.RTVFormats[0]          = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat              = DXGI_FORMAT_D32_FLOAT;
        desc.InputLayout            = asdx::GetQuadLayout();
        desc.SampleDesc.Count       = 1;
        desc.SampleDesc.Quality     = 0;

        if (!m_CopyDepthPipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : CopyDepth Pipeline Init Failed.");
            return false;
        }
    }

    return true;
}

bool App::InitForTest(ID3D12GraphicsCommandList6* pCmd)
{
    auto pDevice = asdx::GetD3D12Device();

    struct Vertex
    {
        asdx::Vector3 Position;
        asdx::Vector2 TexCoord;
        asdx::Vector4 Color;
    };

    {
        const Vertex vertices[] = {
            { asdx::Vector3( 0.0f,  0.7f, 1.0f), asdx::Vector2(0.0f, 1.0f), asdx::Vector4(1.0f, 0.0f, 0.0f, 1.0f) },
            { asdx::Vector3(-0.7f, -0.7f, 1.0f), asdx::Vector2(0.0f, 0.0f), asdx::Vector4(0.0f, 1.0f, 0.0f, 1.0f) },
            { asdx::Vector3( 0.7f, -0.7f, 1.0f), asdx::Vector2(1.0f, 0.0f), asdx::Vector4(0.0f, 0.0f, 1.0f, 1.0f) },
        };

        auto size = sizeof(vertices);
        if (!asdx::CreateUploadBuffer(pDevice, size, m_VB.GetAddress()))
        {
            ELOGA("Error : CreateUploadBuffer() Failed.");
            return false;
        }

        void* ptr = nullptr;
        auto hr = m_VB->Map(0, nullptr, &ptr);
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }

        memcpy(ptr, vertices, sizeof(vertices));
        m_VB->Unmap(0, nullptr);

        if (!asdx::CreateBufferSRV(pDevice, m_VB.GetPtr(), sizeof(vertices) / 4, 0, m_VertexSRV.GetAddress()))
        {
            ELOGA("Error : CreateBufferSRV() Failed.");
            return false;
        }
    }

    // インデックスデータ生成.
    {
        uint32_t indices[] = { 0, 1, 2 };

        if (!asdx::CreateUploadBuffer(pDevice, sizeof(indices), m_IB.GetAddress()))
        {
            ELOGA("Error : CreateUploadBuffer() Failed.");
            return false;
        }

        void* ptr = nullptr;
        auto hr = m_IB->Map(0, nullptr, &ptr);
        if (FAILED(hr))
        {
            ELOGA("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }

        memcpy(ptr, indices, sizeof(indices));
        m_IB->Unmap(0, nullptr);

        if (!asdx::CreateBufferSRV(pDevice, m_IB.GetPtr(), 3, 0, m_IndexSRV.GetAddress()))
        {
            ELOGA("Error : CreateBufferSRV() Failed.");
            return false;
        }
    }

    // 下位レベル高速化機構の生成.
    {
        asdx::DXR_GEOMETRY_DESC desc = {};
        desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        desc.Triangles.VertexCount                  = 3;
        desc.Triangles.VertexBuffer.StartAddress    = m_VB->GetGPUVirtualAddress();
        desc.Triangles.VertexBuffer.StrideInBytes   = sizeof(Vertex);
        desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
        desc.Triangles.IndexCount                   = 3;
        desc.Triangles.IndexBuffer                  = m_IB->GetGPUVirtualAddress();
        desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
        desc.Triangles.Transform3x4                 = 0;

        if (!m_BLAS.Init(pDevice, 1, &desc, asdx::DXR_BUILD_FLAG_PREFER_FAST_TRACE))
        {
            ELOGA("Error : Blas::Init() Failed.");
            return false;
        }
    }

    // 上位レベル高速化機構の生成.
    {
        auto matrix = asdx::Transform3x4();

        asdx::DXR_INSTANCE_DESC desc = {};
        memcpy(desc.Transform, &matrix, sizeof(matrix));
        desc.InstanceMask           = 0x1;
        desc.AccelerationStructure  = m_BLAS.GetResource()->GetGPUVirtualAddress();

        if (!m_TLAS.Init(pDevice, 1, &desc, asdx::DXR_BUILD_FLAG_PREFER_FAST_TRACE))
        {
            ELOGA("Error : Tlas::Init() Failed.");
            return false;
        }
    }

    // 高速化機構のビルド.
    {
        m_BLAS.Build(pCmd);
        m_TLAS.Build(pCmd);
    }

    // IBLの読み込み.
    {
    }

    return true;
}
#endif//RTC_TARGET == RTC_DEVELOP


