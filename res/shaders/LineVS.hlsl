//-----------------------------------------------------------------------------
// File : LineVS.hlsl
// Desc : Vertex Shader For Line Draw.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SceneParameters.hlsli>


///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    uint   HitIndex : HIT_INDEX;
};

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<SceneParameters> SceneParam : register(b1);
ByteAddressBuffer               RayPoints  : register(t0);

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output = (VSOutput)0;

    uint   address  = sizeof(float3) * vertexId;
    float4 worldPos = asfloat(RayPoints.Load4(address));
    float4 viewPos  = mul(SceneParam.View, float4(worldPos.xyz, 1.0f));
    float4 projPos  = mul(SceneParam.Proj, viewPos);

    output.Position = projPos;
    output.HitIndex = uint(worldPos.a);
    return output;
}