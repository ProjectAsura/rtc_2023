//-----------------------------------------------------------------------------
// File : rtcDevice.h
// Desc : Device 
// Copyright(c) Project Asura. All right reserved
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <rtcTypedef.h>
#include <atomic>
#include <list>
#include <vector>
#include <mutex>
#include <string>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")


namespace rtc {

class CommandList;
class CommandQueue;
class DescriptorHeap;

template<typename T>
using RefPtr = Microsoft::WRL::ComPtr<T> ;

///////////////////////////////////////////////////////////////////////////////
// DescriptorHandle structure
///////////////////////////////////////////////////////////////////////////////
struct DescriptorHandle
{
    uint32_t    Index      : 24;
    uint32_t    HeapId     : 8;
};

///////////////////////////////////////////////////////////////////////////////
// DeviceDesc structure
///////////////////////////////////////////////////////////////////////////////
struct DeviceDesc
{
    uint32_t    MaxShaderResourceCount  = 8192;     //!< 最大シェーダリソース数です.
    uint32_t    MaxSamplerCount         = 128;      //!< 最大サンプラー数です.
    uint32_t    MaxColorTargetCount     = 256;      //!< 最大カラーターゲット数です.
    uint32_t    MaxDepthTargetCount     = 256;      //!< 最大深度ターゲット数です.
    bool        EnableDebug             = false;    //!< デバッグモードを有効にします.
    bool        EnableDRED              = true;     //!< DREDを有効にします
    bool        EnableCapture           = false;    //!< PIXキャプチャーを有効にします.
    bool        EnableBreakOnWarning    = false;    //!< 警告時にブレークするなら true.
    bool        EnableBreakOnError      = true;     //!< エラー時にブレークするなら true.
};

///////////////////////////////////////////////////////////////////////////////
// RayTracingPipelineStateDesc structure
///////////////////////////////////////////////////////////////////////////////
struct RayTracingPipelineStateDesc
{
    ID3D12RootSignature*    pGlobalRootSignature;
    ID3D12RootSignature*    pLocalRootSignature;
    D3D12_SHADER_BYTECODE   DXILLibrary;
    uint32_t                ExportCount;
    D3D12_EXPORT_DESC*      pExports;
    uint32_t                HitGroupCount;
    D3D12_HIT_GROUP_DESC*   pHitGroups;
    uint32_t                MaxPayloadSize;
    uint32_t                MaxAttributeSize;
    uint32_t                MaxTraceRecursionDepth;
};

///////////////////////////////////////////////////////////////////////////////
// Device class
///////////////////////////////////////////////////////////////////////////////
class Device
{
public:
    static bool Init(const DeviceDesc& desc);
    static void Term();
    static Device* Instance() { return s_pInstance; }
    ID3D12Device8* GetD3D12Device  () const { return m_pDevice.Get(); }
    CommandQueue*  GetGraphicsQueue() const { return m_pGraphicsQueue; }
    CommandQueue*  GetComputeQueue () const { return m_pComputeQueue; }
    CommandQueue*  GetCopyQueue    () const { return m_pCopyQueue; }

    DescriptorHandle AllocDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE type);
    void FreeDescriptorHandle(DescriptorHandle& handle);

    D3D12_CPU_DESCRIPTOR_HANDLE GetHandleCPU(const DescriptorHandle& handle) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU(const DescriptorHandle& handle) const;

    void SetDescriptorHeaps(ID3D12GraphicsCommandList* pCommandList);
    void WaitIdle();

private:
    static Device* s_pInstance;

    RefPtr<IDXGIFactory7>   m_pFactory              = nullptr;
    RefPtr<IDXGIAdapter1>   m_pAdapter              = nullptr;
    RefPtr<IDXGIOutput6>    m_pOutput               = nullptr;
    RefPtr<ID3D12Device8>   m_pDevice               = nullptr;

    CommandQueue*           m_pGraphicsQueue        = nullptr;
    CommandQueue*           m_pComputeQueue         = nullptr;
    CommandQueue*           m_pCopyQueue            = nullptr;
    DescriptorHeap*         m_pDescriptorHeap[4]    = {};

    Device () = default;
    ~Device() = default;

    bool OnInit(const DeviceDesc& desc);
    void OnTerm();

    Device             (const Device&) = delete;
    Device& operator = (const Device&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// Fence class
///////////////////////////////////////////////////////////////////////////////
class Fence final
{
public:
    static constexpr uint32_t kIgnore   = 0;
    static constexpr uint32_t kInfinite = 0xFFFFFFFF;

    Fence();
    ~Fence();
    bool Init(ID3D12Device* pDevice);
    void Term();
    void Wait(UINT64 fenceValue, uint32_t msec = kInfinite);
    ID3D12Fence* GetPtr() const;

private:
    RefPtr<ID3D12Fence> m_Fence;        //!< フェンスです.
    HANDLE              m_Handle;       //!< イベントハンドルです.
};

///////////////////////////////////////////////////////////////////////////////
// WaitPoint class
///////////////////////////////////////////////////////////////////////////////
class WaitPoint final
{
    friend class CommandQueue;

public:
    WaitPoint();
    ~WaitPoint();
    WaitPoint& operator = (const WaitPoint& value);
    bool IsValid() const;

private:
    UINT64          m_FenceValue = 0;
    ID3D12Fence*    m_pFence     = nullptr;
};


///////////////////////////////////////////////////////////////////////////////
// CommandQueue class
///////////////////////////////////////////////////////////////////////////////
class CommandQueue
{
public:
    static bool Create(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, CommandQueue** ppResult);
    void AddRef();
    void Release();
    uint32_t GetCount() const;
    void Execute(uint32_t count, ID3D12CommandList** ppList);
    WaitPoint Signal();
    bool Wait(const WaitPoint& value);
    void Sync(const WaitPoint& value, uint32_t msec = Fence::kInfinite);
    ID3D12CommandQueue* GetD3D12Queue() const;

private:
    Fence                       m_Fence;        //!< フェンスです.
    RefPtr<ID3D12CommandQueue>  m_Queue;        //!< キューです.
    std::atomic<uint32_t>       m_Counter;      //!< 参照カウンターです.
    std::atomic<bool>           m_IsExecuted;   //!< 実行されたかどうか?
    UINT64                      m_FenceValue;   //!< フェンス値です.

    CommandQueue ();
    ~CommandQueue();

    bool Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
    void Term();
};

///////////////////////////////////////////////////////////////////////////////
// CommandList class
///////////////////////////////////////////////////////////////////////////////
class CommandList
{
public:
    CommandList();
    ~CommandList();
    bool Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type);
    void Term();
    ID3D12GraphicsCommandList6* Reset();
    ID3D12GraphicsCommandList6* GetCommandList() const;

private:
    RefPtr<ID3D12CommandAllocator>      m_Allocator[2];         //!< アロケータです.
    RefPtr<ID3D12GraphicsCommandList6>  m_CmdList;              //!< コマンドリストです.
    uint8_t                             m_Index = 0;            //!< バッファ番号です.
};

///////////////////////////////////////////////////////////////////////////////
// DescriptorHeap class
///////////////////////////////////////////////////////////////////////////////
class DescriptorHeap
{
public:
    DescriptorHeap () = default;
    ~DescriptorHeap() = default;
    bool Init(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC* pDesc);
    void Term();
    DescriptorHandle Alloc();
    void Free(DescriptorHandle& handle);

    D3D12_CPU_DESCRIPTOR_HANDLE GetHandleCPU(const DescriptorHandle& handle) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetHandleGPU(const DescriptorHandle& handle) const;
    ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const;

private:
    std::mutex                      m_Mutex;
    std::list<uint32_t>             m_FreeList;
    RefPtr<ID3D12DescriptorHeap>    m_pHeap;
    uint32_t                        m_IncrementSize = 0;
};

///////////////////////////////////////////////////////////////////////////////
// Blas class
///////////////////////////////////////////////////////////////////////////////
class Blas
{
public:
    struct Desc
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>         Geometries;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags;
    };

    Blas() = default;
    ~Blas();
    bool Init(ID3D12Device6* pDevice, const Desc& desc);
    void Term();
    size_t GetScratchBufferSize() const;
    void Build(ID3D12GraphicsCommandList6* pCmd, D3D12_GPU_VIRTUAL_ADDRESS scratchAddress);
    uint32_t GetGeometryCount() const;
    const D3D12_RAYTRACING_GEOMETRY_DESC& GetGeometry(uint32_t index) const;
    void SetGeometry(uint32_t index, const D3D12_RAYTRACING_GEOMETRY_DESC& desc);
    ID3D12Resource* GetResource() const;

private:
    RefPtr<ID3D12Resource>                              m_Structure;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>         m_GeometryDesc;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC  m_BuildDesc         = {};
    size_t                                              m_ScratchBufferSize = 0;
};

///////////////////////////////////////////////////////////////////////////////
// Tlas class
///////////////////////////////////////////////////////////////////////////////
class Tlas
{
public:
    struct Desc
    {
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC>         Instances;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags;
    };

    Tlas() = default;
    ~Tlas();
    bool Init(ID3D12Device6* pDevice, const Desc& desc);
    void Term();
    size_t GetScratchBufferSize() const;
    void Build(ID3D12GraphicsCommandList6* pCmd, D3D12_GPU_VIRTUAL_ADDRESS scratchAddress);
    D3D12_RAYTRACING_INSTANCE_DESC* Map();
    void Unmap();
    ID3D12Resource* GetResource() const;

private:
    RefPtr<ID3D12Resource>                              m_Structure;
    RefPtr<ID3D12Resource>                              m_Instances;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC  m_BuildDesc         = {};
    size_t                                              m_ScratchBufferSize = 0;
};


///////////////////////////////////////////////////////////////////////////////
// RayTracingPipelineState class
///////////////////////////////////////////////////////////////////////////////
class RayTracingPipelineState
{
public:
    RayTracingPipelineState ();
    ~RayTracingPipelineState();
    bool Init(ID3D12Device5* pDevice, const RayTracingPipelineStateDesc& desc);
    void Term();
    void* GetShaderIdentifier(const wchar_t* exportName) const;
    UINT64 GetShaderStackSize(const wchar_t* exportName) const;
    ID3D12StateObject* GetStateObject() const;

private:
    RefPtr<ID3D12StateObject>           m_pObject;
    RefPtr<ID3D12StateObjectProperties> m_pProps;
};

} // namespace rtc
