//-----------------------------------------------------------------------------
// File : ModelVS.hlsl
// Desc : Vertex Shader For Model Rendering.
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
    float4 Position     : SV_POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float2 TexCoord     : TEXCOORD0;
    float4 CurrProjPos  : CURR_PROJ_POS;
    float4 PrevProjPos  : PREV_PROJ_POS;
};

///////////////////////////////////////////////////////////////////////////////
// ModelVertex structure
///////////////////////////////////////////////////////////////////////////////
struct ModelVertex
{
    float3 Position;
    float3 Normal;
    float3 Tangent;
    float2 TexCoord;
};

///////////////////////////////////////////////////////////////////////////////
// ObjectParameters structure
///////////////////////////////////////////////////////////////////////////////
struct ObjectParameters
{
    uint  InstanceId;
    uint3 Reserved0;
};

//-----------------------------------------------------------------------------
// Resources.
//-----------------------------------------------------------------------------
ConstantBuffer<SceneParameters>     SceneParam  : register(b0);
ConstantBuffer<ObjectParameters>    ObjectParam : register(b1);
ByteAddressBuffer                   Transforms  : register(t0);
StructuredBuffer<ModelVertex>       Vertices    : register(t1);

//-----------------------------------------------------------------------------
//      ワールド行列を取得します.
//-----------------------------------------------------------------------------
float3x4 GetWorldMatrix(uint geometryId)
{
    uint index = geometryId * sizeof(float3x4);
    float4 row0 = asfloat(Transforms.Load4(index));
    float4 row1 = asfloat(Transforms.Load4(index + 16));
    float4 row2 = asfloat(Transforms.Load4(index + 32));

    return float3x4(row0, row1, row2);
}

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output = (VSOutput)0;

    // ワールド行列取得.
    float3x4 world = GetWorldMatrix(ObjectParam.InstanceId);

    // 頂点データ取得.
    ModelVertex vertex = Vertices[vertexId];

    float4 localPos = float4(vertex.Position, 1.0f);
    float4 worldPos = float4(mul(world, localPos), 1.0f);
    float4 viewPos  = mul(SceneParam.View, worldPos);
    float4 projPos  = mul(SceneParam.Proj, viewPos);

    float4 prevViewPos = mul(SceneParam.View, worldPos);
    float4 prevProjPos = mul(SceneParam.Proj, prevViewPos);

    float3 worldNormal  = normalize(mul((float3x3)world, vertex.Normal));
    float3 worldTangent = normalize(mul((float3x3)world, vertex.Tangent));

    output.Position     = projPos;
    output.Normal       = worldNormal;
    output.Tangent      = worldTangent;
    output.TexCoord     = vertex.TexCoord;
    output.CurrProjPos  = projPos;
    output.PrevProjPos  = prevProjPos;

    return output;
}
