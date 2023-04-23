//-----------------------------------------------------------------------------
// File : Common.hlsli
// Desc : Common Parameter and Methods.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <BRDF.hlsli>
#include <Samplers.hlsli>
#include <SceneParameters.hlsli>

#define INVALID_ID          (-1)
#define STANDARD_RAY_INDEX  (0)
#define SHADOW_RAY_INDEX    (1)

#define RTC_DEBUG   (0)
#define RTC_RELEASE (1)

typedef BuiltInTriangleIntersectionAttributes   HitArgs;
typedef RaytracingAccelerationStructure         RayTracingAS;

///////////////////////////////////////////////////////////////////////////////
// Payload structure
///////////////////////////////////////////////////////////////////////////////
struct Payload
{
    uint    InstanceId;
    uint    PrimitiveId;
    float2  Barycentrics;

    bool HasHit()
    { return InstanceId != INVALID_ID; }
};

///////////////////////////////////////////////////////////////////////////////
// ShadowPayload structure
///////////////////////////////////////////////////////////////////////////////
struct ShadowPayload
{
    bool    Visible;
};

///////////////////////////////////////////////////////////////////////////////
// Instance structure
///////////////////////////////////////////////////////////////////////////////
struct Instance
{
    uint    VertexId;   // 頂点番号.
    uint    IndexId;    // 頂点インデックス番号.
    uint    MaterialId; // マテリアル番号.
};


//-----------------------------------------------------------------------------
//      Permuted Congruential Generator (PCG)
//-----------------------------------------------------------------------------
uint4 PCG(uint4 v)
{
    v = v * 1664525u + 101390422u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    v = v ^ (v >> 16u);
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    return v;
}

//-----------------------------------------------------------------------------
//      floatに変換します.
//-----------------------------------------------------------------------------
float ToFloat(uint x)
{ return asfloat(0x3f800000 | (x >> 9)) - 1.0f; }

//-----------------------------------------------------------------------------
//      乱数のシード値を設定します..
//-----------------------------------------------------------------------------
uint4 SetSeed(uint2 pixelCoords, uint frameIndex)
{ return uint4(pixelCoords.xy, frameIndex, 0); }

//-----------------------------------------------------------------------------
//      疑似乱数を取得します.
//-----------------------------------------------------------------------------
float Random(uint4 seed)
{
    seed.w++;
    return ToFloat(PCG(seed).x);
}

//-----------------------------------------------------------------------------
//      レイのオフセット値を取得します.
//-----------------------------------------------------------------------------
float3 OffsetRay(const float3 p, const float3 n)
{
    // Ray Tracing Gems, Chapter 6.
    static const float origin       = 1.0f / 32.0f;
    static const float float_scale  = 1.0f / 65536.0f;
    static const float int_scale    = 256.0f;

    int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    float3 p_i = float3(
        asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
        asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
        asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return float3(
        abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
        abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
        abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

//-----------------------------------------------------------------------------
//      輝度値を求めます.
//-----------------------------------------------------------------------------
float Luminance(float3 rgb)
{ return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

//-----------------------------------------------------------------------------
//      Diffuseをサンプルするかどうかの確率を求めます.
//-----------------------------------------------------------------------------
float ProbabilityToSampleDiffuse(float3 diffuseColor, float3 specularColor)
{
    // DirectX Raytracing, Tutorial 14.
    // http://cwyman.org/code/dxrTutors/tutors/Tutor14/tutorial14.md.html
    float lumDiffuse  = max(0.01f, Luminance(diffuseColor));
    float lumSpecular = max(0.01f, Luminance(specularColor));
    return lumDiffuse / (lumDiffuse + lumSpecular);
}

#endif//COMMON_HLSLI
