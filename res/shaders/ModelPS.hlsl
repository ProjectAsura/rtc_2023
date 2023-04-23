//-----------------------------------------------------------------------------
// File : ModelPS.hlsl
// Desc : Pixel Shader For Model Rendering.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <Samplers.hlsli>


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
// PSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct PSOutput
{
    float4 Albedo    : SV_TARGET0;
    float2 Normal    : SV_TARGET1;
    float  Roughness : SV_TARGET2;
    float2 Velocity  : SV_TARGET3;
};

///////////////////////////////////////////////////////////////////////////////
// ObjectParameters structure
///////////////////////////////////////////////////////////////////////////////
struct ObjectParameters
{
    uint  InstanceId;
    uint3 Reserved0;
};

///////////////////////////////////////////////////////////////////////////////
// Instance structure
///////////////////////////////////////////////////////////////////////////////
struct Instance
{
    uint VertexId;
    uint IndexId;
    uint MaterialId;
};

///////////////////////////////////////////////////////////////////////////////
// ModelMaterial structure
///////////////////////////////////////////////////////////////////////////////
struct ModelMaterial
{
    uint4   TextureIndex;
};


//-----------------------------------------------------------------------------
// Resources.
//-----------------------------------------------------------------------------
ConstantBuffer<ObjectParameters> ObjectParam : register(b1);
StructuredBuffer<ModelMaterial>  Materials   : register(t0);
ByteAddressBuffer                Instances   : register(t1);

//-----------------------------------------------------------------------------
//      BC5で圧縮された法線ベクトルをデコードします.
//-----------------------------------------------------------------------------
float3 DecodeBC5Normal(float2 encodedNormal)
{
    float2 xy = encodedNormal * 2.0f - 1.0f;
    float  z  = sqrt(abs(1.0f - dot(xy, xy)));
    return float3(xy, z);
}

//-----------------------------------------------------------------------------
//      エントリーポイントです.
//-----------------------------------------------------------------------------
PSOutput main(const VSOutput input)
{
    PSOutput output = (PSOutput)0;

    // マテリアル取得.
    uint materialId = Instances.Load(ObjectParam.InstanceId * sizeof(Instance) + 8);
    ModelMaterial material = Materials[materialId];

    // テクスチャ取得.
    Texture2D<float4> albedoMap = ResourceDescriptorHeap[material.TextureIndex.x];
    Texture2D<float2> normalMap = ResourceDescriptorHeap[material.TextureIndex.y];
    Texture2D<float4> ormMap    = ResourceDescriptorHeap[material.TextureIndex.z];

    // テクスチャサンプリング.
    float4 albedo = albedoMap.Sample(LinearWrap, input.TexCoord);
    float3 normal = DecodeBC5Normal(normalMap.Sample(LinearWrap, input.TexCoord));
    float3 orm    = ormMap.Sample(LinearWrap, input.TexCoord);

    // 接線空間からワールド空間に変換.
    float3 bitangent = normalize(cross(input.Tangent, input.Normal));
    float3 normalWS  = FromTangentSpaceToWorld(normal, input.Tangent, bitangent, input.Normal);

    // 速度ベクトル算出.
    float2 currPosCS = input.CurrProjPos.xy / input.CurrProjPos.w;
    float2 prevPosCS = input.PrevProjPos.xy / input.PrevProjPos.w;
    float2 velocity = currPosCS - prevPosCS;

    // 出力値設定.
    output.Albedo    = albedo;
    output.Normal    = PackNormal(normalWS);
    output.Roughness = orm.y;
    output.Velocity  = velocity;

    return output;
}