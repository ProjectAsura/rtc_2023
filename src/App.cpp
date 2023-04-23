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
};

///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    asdx::Vector3   Position;
    asdx::Vector3   Normal;
    asdx::Vector3   Tangent;
    asdx::Vector2   TexCoord;
    uint32_t        MaterialId;
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

void InitRangeAsSRV(D3D12_DESCRIPTOR_RANGE& range, UINT registerIndex, UINT count = 1)
{
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = count;
    range.BaseShaderRegister                = registerIndex;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
}

void InitRangeAsUAV(D3D12_DESCRIPTOR_RANGE& range, UINT registerIndex, UINT count = 1)
{
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors                    = count;
    range.BaseShaderRegister                = registerIndex;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
}

void InitAsConstans(D3D12_ROOT_PARAMETER& param, UINT registerIndex, UINT count, D3D12_SHADER_VISIBILITY visiblity)
{
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.Num32BitValues  = count;
    param.Constants.ShaderRegister  = registerIndex;
    param.Constants.RegisterSpace   = 0;
    param.ShaderVisibility          = visiblity;
}

void InitAsCBV(D3D12_ROOT_PARAMETER& param, UINT registerIndex, D3D12_SHADER_VISIBILITY visiblity)
{
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = registerIndex;
    param.Descriptor.RegisterSpace  = 0;
    param.ShaderVisibility          = visiblity;
}

void InitAsTable(
    D3D12_ROOT_PARAMETER&           param,
    UINT                            count,
    const D3D12_DESCRIPTOR_RANGE*   range,
    D3D12_SHADER_VISIBILITY         visiblity
)
{
    param.ParameterType                         = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges   = 1;
    param.DescriptorTable.pDescriptorRanges     = range;
    param.ShaderVisibility                      = visiblity;
}

bool InitRootSignature
(
    ID3D12Device*                       pDevice,
    const D3D12_ROOT_SIGNATURE_DESC*    pDesc,
    ID3D12RootSignature**               ppRootSig
)
{
    asdx::RefPtr<ID3DBlob> blob;
    asdx::RefPtr<ID3DBlob> errorBlob;
    auto hr = D3D12SerializeRootSignature(
        pDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.GetAddress(), errorBlob.GetAddress());
    if (FAILED(hr))
    {
        ELOG("Error : D3D12SerializeRootSignature() Failed. errcode = 0x%x, msg = %s",
            hr, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    hr = pDevice->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(ppRootSig));
    if (FAILED(hr))
    {
        ELOG("Error : ID3D12Device::CreateRootSignature() Failed. errcode = 0x%x", hr);
        return false;
    }

    return true;
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

///////////////////////////////////////////////////////////////////////////////
// App class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
App::App(const RenderDesc& desc)
: asdx::Application(L"rtc_2023", desc.Width, desc.Height, nullptr, nullptr, nullptr)
, m_RenderDesc(desc)
{
#if RTC_TARGET == RTC_DEVELOP
    // 開発版.
    m_CreateWindow = true;

    m_DeviceDesc.EnableBreakOnError     = true;
    m_DeviceDesc.EnableBreakOnWarning   = false;
    m_DeviceDesc.EnableDRED             = true;
    m_DeviceDesc.EnableCapture          = true;
    m_DeviceDesc.EnableDebug            = false;
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

    // レイトレーシングパス.
    {
        D3D12_SHADER_VISIBILITY cs = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[5] = {};
        InitRangeAsSRV(ranges[0], 0);
        InitRangeAsSRV(ranges[1], 1);
        InitRangeAsSRV(ranges[2], 2);
        InitRangeAsSRV(ranges[3], 3);
        InitRangeAsUAV(ranges[4], 0);

        D3D12_ROOT_PARAMETER params[6] = {};
        InitAsCBV  (params[0], 0, cs);
        InitAsTable(params[1], 1, &ranges[0], cs);
        InitAsTable(params[2], 1, &ranges[1], cs);
        InitAsTable(params[3], 1, &ranges[2], cs);
        InitAsTable(params[4], 1, &ranges[3], cs);
        InitAsTable(params[5], 1, &ranges[4], cs);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters       = _countof(params);
        desc.pParameters         = params;
        desc.NumStaticSamplers   = asdx::GetStaticSamplerCounts();
        desc.pStaticSamplers     = asdx::GetStaticSamplers();

        if (!InitRootSignature(pDevice, &desc, m_RayTracingRootSig.GetAddress()))
        {
            ELOG("Error : RayTracing RootSignature Init Failed.");
            return false;
        }
    }

    // G-Buffer ルートシグニチャ.
    {
        auto vs = D3D12_SHADER_VISIBILITY_VERTEX;
        auto ps = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE ranges[5] = {};
        InitRangeAsSRV(ranges[0], 0);
        InitRangeAsSRV(ranges[1], 1);
        InitRangeAsSRV(ranges[2], 0);
        InitRangeAsSRV(ranges[3], 1);
        InitRangeAsSRV(ranges[4], 2);

        D3D12_ROOT_PARAMETER params[6] = {};
        InitAsCBV(params[0], 0, vs);                    // SceneParams.
        InitAsTable(params[1], 1, &ranges[0], vs);      // Transforms.
        InitAsTable(params[2], 1, &ranges[1], vs);      // Vertices.
        InitAsTable(params[3], 1, &ranges[2], ps);      // Albedo.
        InitAsTable(params[4], 1, &ranges[3], ps);      // Normal.
        InitAsTable(params[5], 1, &ranges[4], ps);      // Roughness.

        auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters      = _countof(params);
        desc.pParameters        = params;
        desc.NumStaticSamplers  = asdx::GetStaticSamplerCounts();
        desc.pStaticSamplers    = asdx::GetStaticSamplers();

        if (!InitRootSignature(pDevice, &desc, m_GBufferRootSig.GetAddress()))
        {
            ELOG("Error : GBuffer RootSignature Init Failed.");
            return false;
        }
    }

    // G-Bufferパイプライン.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature                     = m_GBufferRootSig.GetPtr();
        desc.VS                                 = { ModelVS, sizeof(ModelVS) };
        desc.PS                                 = { ModelPS, sizeof(ModelPS) };
        desc.BlendState                         = asdx::BLEND_DESC(asdx::BLEND_STATE_OPAQUE);
        desc.DepthStencilState                  = asdx::DEPTH_STENCIL_DESC(asdx::DEPTH_STATE_DEFAULT);
        desc.RasterizerState                    = asdx::RASTERIZER_DESC(asdx::RASTERIZER_STATE_CULL_NONE);
        desc.SampleMask                         = D3D12_DEFAULT_SAMPLE_MASK;
        desc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets                   = 4;
        desc.RTVFormats[0]                      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.RTVFormats[1]                      = DXGI_FORMAT_R16G16_FLOAT;
        desc.RTVFormats[2]                      = DXGI_FORMAT_R8_UNORM;
        desc.RTVFormats[3]                      = DXGI_FORMAT_R16G16_FLOAT;
        desc.DSVFormat                          = DXGI_FORMAT_D32_FLOAT;
        desc.InputLayout.NumElements            = 0;
        desc.InputLayout.pInputElementDescs     = nullptr;
        desc.SampleDesc.Count                   = 1;
        desc.SampleDesc.Quality                 = 0;

        if (!m_GBufferPipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : G-Buffer Pipeline Init Failed.");
            return false;
        }
    }

    // トーンマップルートシグニチャ.
    {
    }

    // トーンマップパイプライン.
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};

        if (!m_TonemapPipelineState.Init(pDevice, &desc))
        {
            ELOG("Error : Tonemap Pipeline Init Failed.");
            return false;
        }
    }

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

        if (!m_Canvas.Init(&desc))
        {
            ELOG("Error : Canvas Init Failed.");
            return false;
        }

        m_Canvas.SetName(L"Canvas");
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

    #if RTC_TARGET == RTC_DEVELOP
    // ファイル監視設定.
    {
        auto dirPath = asdx::ToFullPathA("../res/shaders");

        asdx::FileWatcher::Desc desc = {};
        desc.DirectoryPath = dirPath.c_str();
        desc.BufferSize    = 4096;
        desc.WaitTimeMsec  = 16;
        desc.pListener     = this;

        if (!m_Watcher.Init(desc))
        {
            ELOG("Error : File Watcher Initialize Failed.");
            return false;
        }
    }

    // カメラ初期化.
    {
        auto pos    = asdx::Vector3(0.0f, 0.0f, 300.5f);
        auto target = asdx::Vector3(0.0f, 0.0f, 0.0f);
        auto upward = asdx::Vector3(0.0f, 1.0f, 0.0f);
        m_AppCamera.Init(pos, target, upward, 0.1f, 10000.0f);

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

    m_Canvas    .Term();
    m_Albedo    .Term();
    m_Normal    .Term();
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
    m_TemporalAAPipelineState.Term();

    m_RayTracingRootSig.Reset();

    m_PathTracePipeline.Term();
    RTC_DEBUG_CODE(m_DebugTracePipeline.Term());

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

        pCmd->OMSetRenderTargets(_countof(rtvs), rtvs, FALSE, nullptr);
        pCmd->ClearRenderTargetView(rtvs[0], m_ClearColor, 0, nullptr);
        pCmd->RSSetViewports(1, &m_Viewport);
        pCmd->RSSetScissorRects(1, &m_ScissorRect);

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
//      レイトレーシングパイプラインを取得します.
//-----------------------------------------------------------------------------
App::RayTracingPipeline* App::GetRayTracingPipline()
{
#if RTC_TARGET == RTC_DEVELOP
    if (m_RayTracingReloadFlags.Get(RELOADED_BIT_INDEX))
    { return &m_DebugTracePipeline; }
#endif

    return &m_PathTracePipeline;
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
//      ファイル更新時の処理.
//-----------------------------------------------------------------------------
void App::OnUpdate
(
    asdx::ACTION_TYPE   type,
    const char*         directoryPath,
    const char*         relativePath
)
{
    if (type != asdx::ACTION_MODIFIED)
    { return; }

    // RayTracingシェーダ.
    {
        // "../res/shaders" ディレクトリからの相対パス.
        const char* shader_paths[] = {
            "Common.hlsli"
            "PathTracing.hlsl"
        };

        CheckModify(
            relativePath,
            m_RayTracingReloadFlags,
            shader_paths,
            _countof(shader_paths));
    }

    // Tonemapシェーダ.
    {
        //const char* shader_paths[] = {
        //    "Tonemap.hlsl"
        //};
        //CheckModify(relativePath, m_TonemapReloadFlags, shader_paths, _countof(shader_paths));
    }
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
    if (m_RayTracingReloadFlags.Get(REQUEST_BIT_INDEX))
    {
        asdx::RefPtr<asdx::IBlob> shader;

        if (CompileShader(L"../res/shaders/RayTracing.hlsl", "", "lib_6_6", shader.GetAddress()))
        { 
            // 解放.
            m_DebugTracePipeline.Term();

            // リロードしたシェーダから製紙画.
            if (m_DebugTracePipeline.Init(
                m_RayTracingRootSig.GetPtr(),
                shader->GetBufferPointer(),
                shader->GetBufferSize()))
            {
                m_RayTracingReloadFlags.Set(RELOADED_BIT_INDEX, true);
            }
        }

        m_RayTracingReloadFlags.Set(REQUEST_BIT_INDEX, false);
    }

    if (m_TonemapReloadFlags.Get(REQUEST_BIT_INDEX))
    {
        //asdx::RefPtr<asdx::IBlob> shader;
        //if (CompileShader(L"../res/shaders/Tonemap.hlsl", "main", "ps_6_6", shader.GetAddress()))
        //{
        //    m_TonemapPipelineState.ReplaceShader(
        //        asdx::SHADER_TYPE_PS,
        //        shader->GetBufferPointer(),
        //        shader->GetBufferSize());
        //    m_TonemapPipelineState.Rebuild();
        //}

        m_TonemapReloadFlags.Set(REQUEST_BIT_INDEX, false);
    }
}
#endif//RTC_TARGET == RTC_DEVELOP


