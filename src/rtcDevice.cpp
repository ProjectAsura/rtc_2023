//-----------------------------------------------------------------------------
// File : rtcDevice.cpp
// Desc : Device.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <rtcDevice.h>
#include <rtcTimer.h>
#include <rtcLog.h>
#include <algorithm>
#if RTC_TARGET == RTC_DEVELOP
#include <ShlObj.h>
#include <strsafe.h>
#endif

namespace {

#if RTC_TARGET == RTC_DEVELOP
//-----------------------------------------------------------------------------
//      PIXキャプチャー用のDLLをロードします.
//-----------------------------------------------------------------------------
void LoadPixGpuCpatureDll()
{
    LPWSTR programFilesPath = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

    wchar_t pixSearchPath[MAX_PATH] = {};
    StringCchCopy(pixSearchPath, MAX_PATH, programFilesPath);
    StringCchCat(pixSearchPath, MAX_PATH, L"\\Microsoft PIX\\*");

    WIN32_FIND_DATA findData;
    bool foundPixInstallation = false;
    wchar_t newestVersionFound[MAX_PATH] = {};

    HANDLE hFind = FindFirstFile(pixSearchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do 
        {
            if (((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) &&
                 (findData.cFileName[0] != '.'))
            {
                if (!foundPixInstallation || wcscmp(newestVersionFound, findData.cFileName) <= 0)
                {
                    foundPixInstallation = true;
                    StringCchCopy(newestVersionFound, _countof(newestVersionFound), findData.cFileName);
                }
            }
        } 
        while (FindNextFile(hFind, &findData) != 0);
    }

    FindClose(hFind);

    if (!foundPixInstallation)
    {
        return;
    }

    wchar_t dllPath[MAX_PATH] = {};
    StringCchCopy(dllPath, wcslen(pixSearchPath), pixSearchPath);
    StringCchCat(dllPath, MAX_PATH, &newestVersionFound[0]);
    StringCchCat(dllPath, MAX_PATH, L"\\WinPixGpuCapturer.dll");

    if (GetModuleHandleW(L"WinPixGpuCapturer.dll") == 0)
    {
        LoadLibraryW(dllPath);
    }
}
#endif

//-------------------------------------------------------------------------------------------------
//      メモリ確保のラッパー関数です.
//-------------------------------------------------------------------------------------------------
void* CustomAlloc(size_t size, size_t alignment, void*)
{ 
    return mi_malloc_aligned(size, alignment); 
}

//-------------------------------------------------------------------------------------------------
//      メモリ解放のラッパー関数です.
//-------------------------------------------------------------------------------------------------
void CustomFree(void* ptr, void*)
{
    return mi_free(ptr);
}

//-----------------------------------------------------------------------------
//      バッファUAVを生成します.
//-----------------------------------------------------------------------------
bool CreateBufferUAV
(
    ID3D12Device*           pDevice,
    UINT64                  bufferSize,
    ID3D12Resource**        ppResource,
    D3D12MA::Allocation**   ppAllocation,
    D3D12_RESOURCE_STATES   initialResourceState
)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment          = 0;
    desc.Width              = bufferSize;
    desc.Height             = 1;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    auto hr = rtc::Device::Instance()->GetAllocator()->CreateResource(
        &allocDesc,
        &desc,
        initialResourceState,
        nullptr,
        ppAllocation,
        IID_PPV_ARGS(ppResource));

    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      アップロードバッファを生成します.
//-----------------------------------------------------------------------------
bool CreateUploadBuffer
(
    ID3D12Device*           pDevice,
    UINT64                  bufferSize,
    ID3D12Resource**        ppResource,
    D3D12MA::Allocation**   ppAllocation
)
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type                  = D3D12_HEAP_TYPE_UPLOAD;
    props.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask      = 1;
    props.VisibleNodeMask       = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment          = 0;
    desc.Width              = bufferSize;
    desc.Height             = 1;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    auto hr = rtc::Device::Instance()->GetAllocator()->CreateResource(
        &allocDesc,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        ppAllocation,
        IID_PPV_ARGS(ppResource));
    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12Device::CreateCommittedResource() Failed. errcode = 0x%x", hr);
        return false;
    }

    return true;
}

} // namespace


namespace rtc {

static_assert(sizeof(DescriptorHandle) == sizeof(uint32_t), "DescriptorHandle Size Not Matched.");

static const uint32_t UINT24_MAX = 0xFFFFFF;

///////////////////////////////////////////////////////////////////////////////
// Device class
///////////////////////////////////////////////////////////////////////////////
Device* Device::s_pInstance = nullptr;

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Device::Init(const DeviceDesc& desc)
{
    if (s_pInstance != nullptr)
    {
        return true;
    }

    s_pInstance = new Device();
    return s_pInstance->OnInit(desc);
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Device::Term()
{
    if (s_pInstance == nullptr)
        return;

    s_pInstance->OnTerm();
    delete s_pInstance;
    s_pInstance = nullptr;
}

//-----------------------------------------------------------------------------
//      初期化時の処理です.
//-----------------------------------------------------------------------------
bool Device::OnInit(const DeviceDesc& deviceDesc)
{
#if RTC_TARGET == RTC_DEVELOP
    if (deviceDesc.EnableCapture)
    {
        // PIXキャプチャー設定.
        LoadPixGpuCpatureDll();
    }

    if (deviceDesc.EnableDebug)
    {
        RefPtr<ID3D12Debug> pDebug;
        auto hr = D3D12GetDebugInterface(IID_PPV_ARGS(pDebug.GetAddressOf()));
        if (SUCCEEDED(hr))
        { pDebug->EnableDebugLayer(); }
    }

    if (deviceDesc.EnableDRED)
    {
        // DRED有効化.
        RefPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDred;
        auto hr = D3D12GetDebugInterface(IID_PPV_ARGS(pDred.GetAddressOf()));
        if (SUCCEEDED(hr))
        {
            pDred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            pDred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            pDred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }
#endif

    // DXGIファクトリを生成.
    {
        UINT flags = 0;
    #if RTC_TARGET == RTC_DEVELOP
        if (deviceDesc.EnableDebug)
        { flags |= DXGI_CREATE_FACTORY_DEBUG; }
    #endif

        RefPtr<IDXGIFactory2> factory;
        auto hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : CreateDXGIFactory() Failed. errcode = 0x%x", hr);
            return false;
        }

        hr = factory->QueryInterface(IID_PPV_ARGS(m_pFactory.GetAddressOf()));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : QueryInterface() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    // DXGIアダプター生成.
    {
        RefPtr<IDXGIAdapter1> pAdapter;
        for(auto adapterId=0;
            DXGI_ERROR_NOT_FOUND != m_pFactory->EnumAdapterByGpuPreference(adapterId, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(pAdapter.ReleaseAndGetAddressOf()));
            adapterId++)
        {
            DXGI_ADAPTER_DESC1 desc;
            auto hr = pAdapter->GetDesc1(&desc);
            if (FAILED(hr))
            { continue; }

            hr = D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
            if (SUCCEEDED(hr))
            {
                if (m_pAdapter.Get() == nullptr)
                { m_pAdapter = pAdapter.Get(); }

                RefPtr<IDXGIOutput> pOutput;
                hr = pAdapter->EnumOutputs(0, pOutput.GetAddressOf());
                if (FAILED(hr))
                { continue; }

                hr = pOutput->QueryInterface(IID_PPV_ARGS(m_pOutput.GetAddressOf()));
                if (SUCCEEDED(hr))
                { break; }
            }
        }
    }

    // デバイス生成.
    {
        RefPtr<ID3D12Device> pDevice;
        auto hr = D3D12CreateDevice(m_pAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(pDevice.GetAddressOf()));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : D3D12CreateDevice() Failed. errcode = 0x%x", hr);
            return false;
        }

        // ID3D12Device8に変換.
        hr = pDevice->QueryInterface(IID_PPV_ARGS(m_pDevice.GetAddressOf()));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : QueryInterface() Failed. errcode = 0x%x", hr);
            return false;
        }

    #if RTC_TARGET == RTC_DEVELOP
        m_pDevice->SetName(L"rtcDevice");

        // ID3D12InfoQueueに変換.
        if (deviceDesc.EnableDebug)
        {
            RefPtr<ID3D12InfoQueue> pInfoQueue;
            hr = m_pDevice->QueryInterface(IID_PPV_ARGS(pInfoQueue.GetAddressOf()));
            if (SUCCEEDED(hr))
            {
                // エラー発生時にブレークさせる.
                if (deviceDesc.EnableBreakOnError)
                { pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE); }

                // 警告発生時にブレークさせる.
                if (deviceDesc.EnableBreakOnWarning)
                { pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE); }

                // 無視するメッセージID.
                D3D12_MESSAGE_ID denyIds[] = {
                    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                    D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                };

                // 無視するメッセージレベル.
                D3D12_MESSAGE_SEVERITY severities[] = {
                    D3D12_MESSAGE_SEVERITY_INFO
                };

                D3D12_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumIDs          = _countof(denyIds);
                filter.DenyList.pIDList         = denyIds;
                filter.DenyList.NumSeverities   = _countof(severities);
                filter.DenyList.pSeverityList   = severities;

                pInfoQueue->PushStorageFilter(&filter);
            }
        }
    #endif
    }

    // アロケータ生成.
    {
        D3D12MA::ALLOCATION_CALLBACKS allocationCallbacks = {};
        allocationCallbacks.pAllocate   = &CustomAlloc;
        allocationCallbacks.pFree       = &CustomFree;
 
        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.pDevice               = m_pDevice.Get();
        allocatorDesc.pAdapter              = m_pAdapter.Get();
        allocatorDesc.pAllocationCallbacks  = &allocationCallbacks;

        auto hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_pAllocator);
        if ( FAILED(hr) )
        {
            RTC_ELOG("Error : D3D12MA::CreateAllocator() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    // DXRのサポートチェック.
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
        auto hr = m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : ID3D12Device::CheckFeatureSupport() Failed. errcode = 0x%x", hr);
            return false;
        }

        if (options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
        {
            RTC_ELOG("Error : DXR Not Supported.");
            return false;
        }
    }

    for(auto i=0; i<4; ++i)
    { m_pDescriptorHeap[i] = new DescriptorHeap(); }
 
    // 定数バッファ・シェーダリソース・アンオーダードアクセスビュー用ディスクリプタヒープ.
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = deviceDesc.MaxShaderResourceCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( !m_pDescriptorHeap[desc.Type]->Init(m_pDevice.Get(), &desc ) )
        {
            RTC_ELOG("Error : DescriptorHeap::Init() Failed.");
            return false;
        }
    }

    // サンプラー用ディスクリプタヒープ.
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = deviceDesc.MaxSamplerCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( !m_pDescriptorHeap[desc.Type]->Init(m_pDevice.Get(), &desc ) )
        {
            RTC_ELOG("Error : DescriptorHeap::Init() Failed");
            return false;
        }
    }

    // レンダーターゲットビュー用ディスクリプタヒープ.
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = deviceDesc.MaxColorTargetCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if ( !m_pDescriptorHeap[desc.Type]->Init(m_pDevice.Get(), &desc ) )
        {
            RTC_ELOG("Error : DescriptorHeap::Init() Failed.");
            return false;
        }
    }

    // 深度ステンシルビュー用ディスクリプタヒープ.
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = deviceDesc.MaxDepthTargetCount;
        desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if ( !m_pDescriptorHeap[desc.Type]->Init(m_pDevice.Get(), &desc ) )
        {
            RTC_ELOG("Error : DescriptorHeap::Init() Failed");
            return false;
        }
    }

    // グラフィックスキュー生成.
    {
        auto ret = CommandQueue::Create(m_pDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, &m_pGraphicsQueue);
        if (!ret)
        {
            RTC_ELOG("Error : CommandQueue::Create() Failed. Type=Graphics");
            return false;
        }
    }

    // コンピュートキュー生成.
    {
        auto ret = CommandQueue::Create(m_pDevice.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE, &m_pComputeQueue);
        if (!ret)
        {
            RTC_ELOG("Error : CommandQueue::Create() Failed. Type=Compute");
            return false;
        }
    }

    // コピーキュー生成.
    {
        auto ret = CommandQueue::Create(m_pDevice.Get(), D3D12_COMMAND_LIST_TYPE_COPY, &m_pCopyQueue);
        if (!ret)
        {
            RTC_ELOG("Error : CommandQueue::Create() Failed. Type=Copy");
            return false;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了時の処理です.
//-----------------------------------------------------------------------------
void Device::OnTerm()
{
    WaitIdle();

    if (m_pCopyQueue)
    {
        m_pCopyQueue->Release();
        m_pCopyQueue = nullptr;
    }

    if (m_pComputeQueue)
    {
        m_pComputeQueue->Release();
        m_pComputeQueue = nullptr;
    }

    if (m_pGraphicsQueue)
    {
        m_pGraphicsQueue->Release();
        m_pGraphicsQueue = nullptr;
    }

    for(auto i=0; i<4; ++i)
    {
        if (m_pDescriptorHeap[i])
        {
            m_pDescriptorHeap[i]->Term();
            delete m_pDescriptorHeap[i];
            m_pDescriptorHeap[i] = nullptr;
        }
    }

    if (m_pAllocator)
    {
        m_pAllocator->Release();
        m_pAllocator = nullptr;
    }

    m_pDevice .Reset();
    m_pOutput .Reset();
    m_pAdapter.Reset();
    m_pFactory.Reset();
}

//-----------------------------------------------------------------------------
//      ディスクリプタハンドルを確保します.
//-----------------------------------------------------------------------------
DescriptorHandle Device::AllocDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    auto ret = m_pDescriptorHeap[type]->Alloc();
    ret.HeapId = uint32_t(type);
    return ret;
}

//-----------------------------------------------------------------------------
//      ディスクリプタハンドルを解放します.
//-----------------------------------------------------------------------------
void Device::FreeDescriptorHandle(DescriptorHandle& handle)
{
    assert(handle.Index != UINT24_MAX);
    auto id = handle.HeapId;
    m_pDescriptorHeap[id]->Free(handle);
}

//-----------------------------------------------------------------------------
//      CPUディスクリプタハンドルを取得します.
//-----------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE Device::GetHandleCPU(const DescriptorHandle& handle) const
{
    assert(handle.Index != UINT24_MAX);
    return m_pDescriptorHeap[handle.HeapId]->GetHandleCPU(handle);
}

//-----------------------------------------------------------------------------
//      GPUディスクリプタハンドルを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE Device::GetHandleGPU(const DescriptorHandle& handle) const
{
    assert(handle.Index != UINT24_MAX);
    return m_pDescriptorHeap[handle.HeapId]->GetHandleGPU(handle);
}

//-----------------------------------------------------------------------------
//      ディスクリプタヒープを設定します.
//-----------------------------------------------------------------------------
void Device::SetDescriptorHeaps(ID3D12GraphicsCommandList* pCommandList)
{
    assert(pCommandList != nullptr);
    ID3D12DescriptorHeap* pHeaps[2] = {
        m_pDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->GetD3D12DescriptorHeap(),
        m_pDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]->GetD3D12DescriptorHeap()
    };
    pCommandList->SetDescriptorHeaps(2, pHeaps);
}

//-----------------------------------------------------------------------------
//      アイドル状態になるまで待機します.
//-----------------------------------------------------------------------------
void Device::WaitIdle()
{
    if (m_pGraphicsQueue != nullptr)
    {
        auto waitPoint = m_pGraphicsQueue->Signal();
        m_pGraphicsQueue->Sync(waitPoint);
    }

    if (m_pComputeQueue != nullptr)
    {
        auto waitPoint = m_pComputeQueue->Signal();
        m_pComputeQueue->Sync(waitPoint);
    }

    if (m_pCopyQueue != nullptr)
    {
        auto waitPoint = m_pCopyQueue->Signal();
        m_pCopyQueue->Sync(waitPoint);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Fence class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
Fence::Fence()
: m_Fence  ()
, m_Handle ( nullptr )
{ /* DO_NOTHING */ }

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
Fence::~Fence()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Fence::Init( ID3D12Device* pDevice )
{
    // 引数チェック.
    if ( pDevice == nullptr )
    {
        RTC_ELOG( "Error : Invalid Error." );
        return false;
    }

    // イベントを生成.
    m_Handle = CreateEventEx( nullptr, FALSE, FALSE, EVENT_ALL_ACCESS );
    if ( m_Handle == nullptr )
    {
        RTC_ELOG( "Error : CreateEventW() Failed." );
        return false;
    }

    // フェンスを生成.
    auto hr = pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( m_Fence.GetAddressOf()) );
    if ( FAILED( hr ) )
    {
        RTC_ELOG( "Error : ID3D12Device::CreateFence() Failed. errcode = 0x%x", hr );
        return false;
    }

    RTC_DEBUG_CODE(m_Fence->SetName(L"asdxFence"));

    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Fence::Term()
{
    // ハンドルを閉じる.
    if ( m_Handle != nullptr )
    {
        CloseHandle( m_Handle );
        m_Handle = nullptr;
    }

    // フェンスオブジェクトを破棄.
    m_Fence.Reset();
}

//-----------------------------------------------------------------------------
//      フェンスが指定された値に達するまで待機します.
//-----------------------------------------------------------------------------
void Fence::Wait(UINT64 fenceValue, uint32_t msec)
{
    if ( m_Fence->GetCompletedValue() < fenceValue )
    {
        auto hr = m_Fence->SetEventOnCompletion( fenceValue, m_Handle );
        if ( FAILED( hr ) )
        {
            RTC_ELOG( "Error : ID3D12Fence::SetEventOnCompletation() Failed." );
            return;
        }

        WaitForSingleObject( m_Handle, msec );
    }
}

//-----------------------------------------------------------------------------
//      フェンスを取得します.
//-----------------------------------------------------------------------------
ID3D12Fence* Fence::GetPtr() const
{ return m_Fence.Get(); }


///////////////////////////////////////////////////////////////////////////////
// WaitPoint class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
WaitPoint::WaitPoint()
: m_FenceValue  (0)
, m_pFence      (nullptr)
{ /* DO_NOTHING */ }

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
WaitPoint::~WaitPoint()
{
    m_FenceValue    = 0;
    m_pFence        = nullptr;
}

//-----------------------------------------------------------------------------
//      代入演算子です.
//-----------------------------------------------------------------------------
WaitPoint& WaitPoint::operator=(const WaitPoint& value)
{
    m_FenceValue = value.m_FenceValue;
    m_pFence     = value.m_pFence;
    return *this;
}

//-----------------------------------------------------------------------------
//      有効かどうかチェックします.
//-----------------------------------------------------------------------------
bool WaitPoint::IsValid() const
{ return (m_FenceValue >= 1) && (m_pFence != nullptr); }


///////////////////////////////////////////////////////////////////////////////
// CommandQueue class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
CommandQueue::CommandQueue()
: m_Fence       ()
, m_Queue       ()
, m_Counter     (1)
, m_FenceValue  (0)
{ /* DO_NOTHING */ }

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
CommandQueue::~CommandQueue()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理です.
//-----------------------------------------------------------------------------
bool CommandQueue::Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type)
{
    if ( pDevice == nullptr )
    {
        RTC_ELOG( "Error : Invalid Arugment." );
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC desc = {
        type,
        0,
        D3D12_COMMAND_QUEUE_FLAG_NONE
    };

    auto hr = pDevice->CreateCommandQueue( &desc, IID_PPV_ARGS(m_Queue.GetAddressOf()) );
    if ( FAILED(hr) )
    {
        RTC_ELOG( "Error : ID3D12Device::CreateCommandQueue() Failed. errcodes = 0x%x", hr );
        return false;
    }

    m_Queue->SetName(L"asdxQueue");

    if ( !m_Fence.Init(pDevice) )
    {
        RTC_ELOG( "Error : Fence::Init() Failed." );
        return false;
    }

    m_IsExecuted = false;
    m_FenceValue = 1;

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void CommandQueue::Term()
{
    m_Queue.Reset();
    m_Fence.Term();
    m_IsExecuted = false;
    m_FenceValue = 0;
}

//-----------------------------------------------------------------------------
//      参照カウントを増やします.
//-----------------------------------------------------------------------------
void CommandQueue::AddRef()
{ m_Counter++; }

//-----------------------------------------------------------------------------
//      解放処理を行います.
//-----------------------------------------------------------------------------
void CommandQueue::Release()
{
    m_Counter--;
    if (m_Counter == 0)
    { delete this; }
}

//-----------------------------------------------------------------------------
//      参照カウントを取得します.
//-----------------------------------------------------------------------------
uint32_t CommandQueue::GetCount() const
{ return m_Counter; }

//-----------------------------------------------------------------------------
//      コマンドを実行します.
//-----------------------------------------------------------------------------
void CommandQueue::Execute(uint32_t count, ID3D12CommandList** ppList)
{
    if(count == 0 || ppList == nullptr)
    { return; }

    m_Queue->ExecuteCommandLists(count, ppList);
    m_IsExecuted = true;
}

//-----------------------------------------------------------------------------
//      フェンスの値を更新します.
//-----------------------------------------------------------------------------
WaitPoint CommandQueue::Signal()
{
    WaitPoint result;

    const auto fence = m_FenceValue;
    auto hr = m_Queue->Signal(m_Fence.GetPtr(), fence);
    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12CommandQueue::Signal() Failed.");
        return result;
    }
    m_FenceValue++;

    result.m_FenceValue = fence;
    result.m_pFence     = m_Fence.GetPtr();

    return result;
}

//-----------------------------------------------------------------------------
//      GPU上での待機点を設定します.
//-----------------------------------------------------------------------------
bool CommandQueue::Wait(const WaitPoint& value)
{
    auto hr = m_Queue->Wait(value.m_pFence, value.m_FenceValue);
    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12CommandQueue::Wait() Failed.");
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      CPU上でコマンドの完了を待機します.
//-----------------------------------------------------------------------------
void CommandQueue::Sync(const WaitPoint& value, uint32_t msec)
{
    if (!m_IsExecuted)
    { return; }

    m_Fence.Wait(value.m_FenceValue, msec);
}

//-----------------------------------------------------------------------------
//      コマンドキューを取得します.
//-----------------------------------------------------------------------------
ID3D12CommandQueue* CommandQueue::GetD3D12Queue() const
{ return m_Queue.Get(); }

//-----------------------------------------------------------------------------
//      生成処理を行います.
//-----------------------------------------------------------------------------
bool CommandQueue::Create
(
    ID3D12Device*           pDevice,
    D3D12_COMMAND_LIST_TYPE type,
    CommandQueue**          ppResult
)
{
    if ( pDevice == nullptr )
    {
        RTC_ELOG( "Error : Invalid Argument." );
        return false;
    }

    auto queue = new (std::nothrow) CommandQueue();
    if (queue == nullptr)
    {
        RTC_ELOG( "Error : Ouf of Memory." );
        return false;
    }

    if (!queue->Init(pDevice, type))
    {
        queue->Release();
        RTC_ELOG( "Error : Queue::Init() Failed." );
        return false;
    }

    *ppResult = queue;

    return true;
}


///////////////////////////////////////////////////////////////////////////////
// GraphicsCommandList class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
CommandList::CommandList()
: m_Allocator()
, m_CmdList  ()
, m_Index    (0)
{ /* DO_NOTHING */ }

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
CommandList::~CommandList()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool CommandList::Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type)
{
    // 引数チェック.
    if (pDevice == nullptr)
    {
        RTC_ELOG("Error : Invalid Argument.");
        return false;
    }

    for(auto i=0; i<2; ++i)
    {
        // コマンドアロケータを生成.
        auto hr = pDevice->CreateCommandAllocator(type, IID_PPV_ARGS( m_Allocator[i].GetAddressOf()));
        if ( FAILED( hr ) )
        {
            RTC_ELOG("Error : ID3D12Device::CreateCommandAllocator() Failed. errcode = 0x%x", hr);
            return false;
        }
    }

    // コマンドリストを生成.
    auto hr = pDevice->CreateCommandList(
        0,
        type,
        m_Allocator[0].Get(),
        nullptr,
        IID_PPV_ARGS(m_CmdList.GetAddressOf()));
    if ( FAILED( hr ) )
    {
        RTC_ELOG("Error : ID3D12Device::CreateCommandList() Failed. errcode = 0x%x", hr);
        return false;
    }

    // 生成直後は開きっぱなしの扱いになっているので閉じておく.
    m_CmdList->Close();

    m_Index = 0;

    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void CommandList::Term()
{
    m_CmdList.Reset();

    for(auto i=0; i<2; ++i)
    { m_Allocator[i].Reset(); }
}

//-----------------------------------------------------------------------------
//      コマンドリストをリセットします.
//-----------------------------------------------------------------------------
ID3D12GraphicsCommandList6* CommandList::Reset()
{
    // ダブルバッファリング.
    m_Index = (m_Index + 1) & 0x1;

    // コマンドアロケータをリセット.
    m_Allocator[m_Index]->Reset();

    // コマンドリストをリセット.
    m_CmdList->Reset(m_Allocator[m_Index].Get(), nullptr);

    // ディスクリプターヒープを設定しおく.
    Device::Instance()->SetDescriptorHeaps(m_CmdList.Get());

    return m_CmdList.Get();
}

//-----------------------------------------------------------------------------
//      グラフィックスコマンドリストを取得します.
//-----------------------------------------------------------------------------
ID3D12GraphicsCommandList6* CommandList::GetCommandList() const
{ return m_CmdList.Get(); }


///////////////////////////////////////////////////////////////////////////////
// DescriptorHeap class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool DescriptorHeap::Init(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC* pDesc)
{
    if (pDevice == nullptr || pDesc == nullptr)
    { return false; }

    if (pDesc->NumDescriptors == 0)
    { return true; }

    auto hr = pDevice->CreateDescriptorHeap(pDesc, IID_PPV_ARGS(&m_pHeap));
    if ( FAILED(hr) )
    {
        RTC_ELOG("Error : ID3D12Device::CreateDescriptorHeap() Failed. errcode = 0x%x", hr);
        return false;
    }

    RTC_DEBUG_CODE(m_pHeap->SetName(L"DescriptorHeap"));

    // インクリメントサイズを取得.
    m_IncrementSize = pDevice->GetDescriptorHandleIncrementSize(pDesc->Type);

    // フリーリスト構築.
    {
        std::lock_guard<std::mutex> locker(m_Mutex);
        if (!m_FreeList.empty())
        { m_FreeList.clear(); }

        for(auto i=0u; i<pDesc->NumDescriptors; ++i)
        { m_FreeList.push_back(i); }
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void DescriptorHeap::Term()
{
    {
        std::lock_guard<std::mutex> locker(m_Mutex);
        m_FreeList.clear();
    }

    m_IncrementSize = 0;
    m_pHeap.Reset();
}

//-----------------------------------------------------------------------------
//      ディスクリプタハンドルを確保します.
//-----------------------------------------------------------------------------
DescriptorHandle DescriptorHeap::Alloc()
{
    DescriptorHandle result = {};
    result.Index  = UINT24_MAX;
    result.HeapId = UINT8_MAX;

    if (m_FreeList.empty())
    { return result; }

    uint32_t index = 0;
    {
        std::lock_guard<std::mutex> locker(m_Mutex);
        index = m_FreeList.front();
        m_FreeList.pop_front();
    }

    result.Index = index;
    return result;
}

//-----------------------------------------------------------------------------
//      ディスクリプタハンドルを解放します.
//-----------------------------------------------------------------------------
void DescriptorHeap::Free(DescriptorHandle& handle)
{
    uint32_t index = handle.Index;
    if (index != UINT24_MAX)
    {
        std::lock_guard<std::mutex> locker(m_Mutex);
        m_FreeList.push_back(index);
    }

    handle = {};
}

//-----------------------------------------------------------------------------
//      CPUディスクリプタハンドルを取得します.
//-----------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetHandleCPU(const DescriptorHandle& handle) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE result = {};
    if (m_pHeap.Get() == nullptr)
    { return result; }

    result = m_pHeap->GetCPUDescriptorHandleForHeapStart();
    result.ptr += (SIZE_T(m_IncrementSize) * handle.Index);
    return result;
}

//-----------------------------------------------------------------------------
//      GPUディスクリプタハンドルを取得します.
//-----------------------------------------------------------------------------
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetHandleGPU(const DescriptorHandle& handle) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE result = {};
    if (m_pHeap.Get() == nullptr)
    { return result; }

    result = m_pHeap->GetGPUDescriptorHandleForHeapStart();
    result.ptr += (UINT64(m_IncrementSize) * handle.Index);
    return result;
}

//-----------------------------------------------------------------------------
//      ディスクリプタヒープを取得します.
//-----------------------------------------------------------------------------
ID3D12DescriptorHeap* DescriptorHeap::GetD3D12DescriptorHeap() const
{ return m_pHeap.Get(); }



///////////////////////////////////////////////////////////////////////////////
// Blas class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
Blas::~Blas()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Blas::Init(ID3D12Device6* pDevice, const Desc& desc)
{
    if (pDevice == nullptr)
    { return false; }

    // 設定をコピっておく.
    m_GeometryDesc = std::move(desc.Geometries);

    // 高速化機構の入力設定.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout      = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags            = desc.BuildFlags;
    inputs.NumDescs         = UINT(m_GeometryDesc.size());
    inputs.pGeometryDescs   = m_GeometryDesc.data();
    inputs.Type             = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    // ビルド前情報を取得.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    if (prebuildInfo.ResultDataMaxSizeInBytes == 0)
    { return false; }

    // スクラッチバッファサイズを取得.
    m_ScratchBufferSize = std::max(prebuildInfo.ScratchDataSizeInBytes, prebuildInfo.UpdateScratchDataSizeInBytes);

    // 高速化機構用バッファ生成.
    if (!CreateBufferUAV(
        pDevice,
        prebuildInfo.ResultDataMaxSizeInBytes,
        m_Structure.GetAddressOf(),
        m_StructureAllocation.GetAddressOf(),
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE))
    {
        RTC_ELOG("Error : CreateUAVBuffer() Failed.");
        return false;
    }
    RTC_DEBUG_CODE(m_Structure->SetName(L"Blas"));

    // ビルド設定.
    memset(&m_BuildDesc, 0, sizeof(m_BuildDesc));
    m_BuildDesc.Inputs                          = inputs;
    m_BuildDesc.DestAccelerationStructureData   = m_Structure->GetGPUVirtualAddress();

    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Blas::Term()
{
    m_GeometryDesc.clear();
    m_Structure.Reset();
    m_StructureAllocation.Reset();
    m_ScratchBufferSize = 0;
}

//-----------------------------------------------------------------------------
//      スクラッチバッファサイズを取得します.
//-----------------------------------------------------------------------------
size_t Blas::GetScratchBufferSize() const
{ return m_ScratchBufferSize; }

//-----------------------------------------------------------------------------
//      ジオメトリ数を取得します.
//-----------------------------------------------------------------------------
uint32_t Blas::GetGeometryCount() const
{ return uint32_t(m_GeometryDesc.size()); }

//-----------------------------------------------------------------------------
//      ジオメトリ構成を取得します.
//-----------------------------------------------------------------------------
const D3D12_RAYTRACING_GEOMETRY_DESC& Blas::GetGeometry(uint32_t index) const
{
    assert(index < uint32_t(m_GeometryDesc.size()));
    return m_GeometryDesc[index];
}

//-----------------------------------------------------------------------------
//      ジオメトリ構成を設定します.
//-----------------------------------------------------------------------------
void Blas::SetGeometry(uint32_t index, const D3D12_RAYTRACING_GEOMETRY_DESC& desc)
{
    assert(index < uint32_t(m_GeometryDesc.size()));
    m_GeometryDesc[index] = desc;
}

//-----------------------------------------------------------------------------
//      リソースを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* Blas::GetResource() const
{ return m_Structure.Get(); }

//-----------------------------------------------------------------------------
//      ビルドします.
//-----------------------------------------------------------------------------
void Blas::Build(ID3D12GraphicsCommandList6* pCmd, D3D12_GPU_VIRTUAL_ADDRESS scratchAddress)
{
    auto desc = m_BuildDesc;
    desc.SourceAccelerationStructureData = scratchAddress;

    // 高速化機構を構築.
    pCmd->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    // バリアを張っておく.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type            = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource   = m_Structure.Get();
    pCmd->ResourceBarrier(1, &barrier);
}


///////////////////////////////////////////////////////////////////////////////
// Tlas class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
Tlas::~Tlas()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool Tlas::Init(ID3D12Device6* pDevice, const Desc& desc)
{
    // アップロードバッファ生成.
    if (!CreateUploadBuffer(
        pDevice,
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * desc.Instances.size(), 
        m_Instances.GetAddressOf(),
        m_InstanceAllocation.GetAddressOf()))
    {
        RTC_ELOG("Error : CreateUploadBuffer() Failed.");
        return false;
    }

    // インスタンス設定をコピー.
    {
        D3D12_RAYTRACING_INSTANCE_DESC* ptr = nullptr;
        auto hr = m_Instances->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        if (FAILED(hr))
        {
            RTC_ELOG("Error : ID3D12Resource::Map() Failed. errcode = 0x%x", hr);
            return false;
        }

        memcpy(ptr, desc.Instances.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * desc.Instances.size());

        m_Instances->Unmap(0, nullptr);
    }

    // 高速化機構の入力設定.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout      = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags            = desc.BuildFlags;
    inputs.NumDescs         = UINT(desc.Instances.size());
    inputs.InstanceDescs    = m_Instances->GetGPUVirtualAddress();
    inputs.Type             = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    // ビルド前情報を取得.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
    if (prebuildInfo.ResultDataMaxSizeInBytes == 0)
    { return false; }

    // スクラッチバッファサイズを取得.
    m_ScratchBufferSize = std::max(prebuildInfo.ScratchDataSizeInBytes, prebuildInfo.UpdateScratchDataSizeInBytes);

    // 高速化機構用バッファ生成.
    if (!CreateBufferUAV(
        pDevice,
        prebuildInfo.ResultDataMaxSizeInBytes,
        m_Structure.GetAddressOf(),
        m_StructureAllocation.GetAddressOf(),
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE))
    {
        RTC_ELOG("Error : CreateUAVBuffer() Failed.");
        return false;
    }
    RTC_DEBUG_CODE(m_Structure->SetName(L"Tlas"));

    // ビルド設定.
    memset(&m_BuildDesc, 0, sizeof(m_BuildDesc));
    m_BuildDesc.Inputs                          = inputs;
    m_BuildDesc.DestAccelerationStructureData   = m_Structure->GetGPUVirtualAddress();

    // 正常終了.
    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Tlas::Term()
{
    m_Instances.Reset();
    m_Structure.Reset();

    m_InstanceAllocation .Reset();
    m_StructureAllocation.Reset();
    m_ScratchBufferSize = 0;
}

//-----------------------------------------------------------------------------
//      スクラッチバッファサイズを取得します.
//-----------------------------------------------------------------------------
size_t Tlas::GetScratchBufferSize() const 
{ return m_ScratchBufferSize; }

//-----------------------------------------------------------------------------
//      メモリマッピングを行います.
//-----------------------------------------------------------------------------
D3D12_RAYTRACING_INSTANCE_DESC* Tlas::Map()
{
    D3D12_RAYTRACING_INSTANCE_DESC* ptr = nullptr;
    auto hr = m_Instances->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
    if (FAILED(hr))
    { return nullptr; }

    return ptr;
}

//-----------------------------------------------------------------------------
//      メモリマッピングを解除します.
//-----------------------------------------------------------------------------
void Tlas::Unmap()
{ m_Instances->Unmap(0, nullptr); }

//-----------------------------------------------------------------------------
//      リソースを取得します.
//-----------------------------------------------------------------------------
ID3D12Resource* Tlas::GetResource() const
{ return m_Structure.Get(); }

//-----------------------------------------------------------------------------
//      ビルドします.
//-----------------------------------------------------------------------------
void Tlas::Build(ID3D12GraphicsCommandList6* pCmd, D3D12_GPU_VIRTUAL_ADDRESS scratchAddress)
{
    auto desc = m_BuildDesc;
    desc.SourceAccelerationStructureData = scratchAddress;

    // 高速化機構を構築.
    pCmd->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    // バリアを張っておく.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type            = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource   = m_Structure.Get();
    pCmd->ResourceBarrier(1, &barrier);
}


///////////////////////////////////////////////////////////////////////////////
// RayTracingPipelineState class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      コンストラクタです.
//-----------------------------------------------------------------------------
RayTracingPipelineState::RayTracingPipelineState()
: m_pObject (nullptr)
, m_pProps  (nullptr)
{ /* DO_NOTHING */ }

//-----------------------------------------------------------------------------
//      デストラクタです.
//-----------------------------------------------------------------------------
RayTracingPipelineState::~RayTracingPipelineState()
{ Term(); }

//-----------------------------------------------------------------------------
//      初期化処理を行います.
//-----------------------------------------------------------------------------
bool RayTracingPipelineState::Init(ID3D12Device5* pDevice, const RayTracingPipelineStateDesc& desc)
{
    uint32_t objCount = 6 + desc.HitGroupCount;

    std::vector<D3D12_STATE_SUBOBJECT> objDesc;
    objDesc.resize(objCount);

    auto index = 0;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature = {};
    globalRootSignature.pGlobalRootSignature = desc.pGlobalRootSignature;

    objDesc[index].Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    objDesc[index].pDesc = &globalRootSignature;
    index++;

    D3D12_LOCAL_ROOT_SIGNATURE localRootSignature = {};
    if (desc.pLocalRootSignature != nullptr)
    {
        localRootSignature.pLocalRootSignature = desc.pLocalRootSignature;

        objDesc[index].Type  = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
        objDesc[index].pDesc = &localRootSignature;
        index++;
    }

    D3D12_DXIL_LIBRARY_DESC libDesc = {};
    libDesc.DXILLibrary = desc.DXILLibrary;
    libDesc.NumExports  = desc.ExportCount;
    libDesc.pExports    = desc.pExports;

    objDesc[index].Type     = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    objDesc[index].pDesc    = &libDesc;
    index++;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxAttributeSizeInBytes = desc.MaxAttributeSize;
    shaderConfig.MaxPayloadSizeInBytes   = desc.MaxPayloadSize;

    objDesc[index].Type     = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    objDesc[index].pDesc    = &shaderConfig;
    index++;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = desc.MaxTraceRecursionDepth;

    objDesc[index].Type     = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    objDesc[index].pDesc    = &pipelineConfig;
    index++;

    for(auto i=0u; i<desc.HitGroupCount; ++i)
    {
        objDesc[index].Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        objDesc[index].pDesc = &desc.pHitGroups[i];
        index++;
    }

    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type            = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects   = index;
    stateObjectDesc.pSubobjects     = objDesc.data();

    auto hr = pDevice->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(m_pObject.GetAddressOf()));

    // メモリ解放.
    objDesc.clear();

    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12Device5::CreateStateObject() Failed. errcode = 0x%x", hr);
        return false;
    }

    hr = m_pObject->QueryInterface(IID_PPV_ARGS(m_pProps.GetAddressOf()));
    if (FAILED(hr))
    {
        RTC_ELOG("Error : ID3D12StateObject::QueryInterface() Failed. errcode = 0x%x", hr);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void RayTracingPipelineState::Term()
{
    m_pObject.Reset();
    m_pProps .Reset();
}

//-----------------------------------------------------------------------------
//      シェーダ識別子を取得します.
//-----------------------------------------------------------------------------
void* RayTracingPipelineState::GetShaderIdentifier(const wchar_t* exportName) const
{ return m_pProps->GetShaderIdentifier(exportName); }

//-----------------------------------------------------------------------------
//      シェーダスタックサイズを取得します.
//-----------------------------------------------------------------------------
UINT64 RayTracingPipelineState::GetShaderStackSize(const wchar_t* exportName) const
{ return m_pProps->GetShaderStackSize(exportName); }

//-----------------------------------------------------------------------------
//      ステートオブジェクトを取得します.
//-----------------------------------------------------------------------------
ID3D12StateObject* RayTracingPipelineState::GetStateObject() const
{ return m_pObject.Get(); }

} // namespace rtc
