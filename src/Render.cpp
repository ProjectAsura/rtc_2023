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


//-----------------------------------------------------------------------------
//      UABバリアを発行します.
//-----------------------------------------------------------------------------
void UAVBarrier(ID3D12GraphicsCommandList6* pCmd, ID3D12Resource* pResource)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type            = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags           = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource   = pResource;
    pCmd->ResourceBarrier(1, &barrier);
}

//-----------------------------------------------------------------------------
//      描画処理です.
//-----------------------------------------------------------------------------
void App::Render(ID3D12GraphicsCommandList6* pCmd)
{
    // デノイズ用 G-Bufferパス.
    {
    }

    // レイトレーシング.
    {
        pCmd->SetComputeRootSignature(m_RayTracingRootSig.GetPtr());
        pCmd->SetComputeRootConstantBufferView(0, m_SceneParam.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(1, m_TLAS.GetResource()->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(2, m_VB->GetGPUVirtualAddress());
        pCmd->SetComputeRootShaderResourceView(3, m_IB->GetGPUVirtualAddress());
//        pCmd->SetComputeRootDescriptorTable(4, );
        pCmd->SetComputeRootDescriptorTable(5, m_Radiance.GetUAV()->GetHandleGPU());

        DispatchRay(pCmd);

        UAVBarrier(pCmd, m_Radiance.GetResource());
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
