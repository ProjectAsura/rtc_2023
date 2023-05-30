//-----------------------------------------------------------------------------
// File : PathTracing.hlsl
// Desc : Path Tracing.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Common.hlsli>


#ifndef RTC_TARGET
#define RTC_TARGET  (RTC_DEBUG)
#endif//RTC_TARGET

#define OFFSET_P    (0)     // 位置座標オフセット.
#define OFFSET_N    (12)    // 法線オフセット.
#define OFFSET_T    (24)    // 接線オフセット.
#define OFFSET_U    (36)    // テクスチャ座標オフセット.

#define STRIDE_TRANSFORM    (sizeof(float3x4))
#define STRIDE_VERTEX       (44)
#define STRIDE_INDEX        (sizeof(uint3))
#define STRIDE_INSTANCE     (sizeof(Instance))

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
ConstantBuffer<SceneParameters> SceneParam : register(b0);
RayTracingAS                    SceneAS    : register(t0);
//ByteAddressBuffer               Instances  : register(t1);
//ByteAddressBuffer               Transforms : register(t2);
//Texture2D                       BackGround : register(t3);
RWTexture2D<float4>             Radiance   : register(u0);

#if RTC_TARGET == RTC_DEBUG
RWStructuredBuffer<uint4>   DrawArgs   : register(u1);
RWByteAddressBuffer         RayPoints  : register(u2);
#endif//RTC_TARGET == RTC_DEBUG

ByteAddressBuffer   Vertices : register(t1);
ByteAddressBuffer   Indices  : register(t2);

//-----------------------------------------------------------------------------
// Forward Declarations.
//-----------------------------------------------------------------------------
float3 PathTracing (RayDesc ray);
float3 DebugTracing(RayDesc ray, bool debugRay);


///////////////////////////////////////////////////////////////////////////////
// SurfaceHit structure
///////////////////////////////////////////////////////////////////////////////
struct SurfaceHit
{
    float3  Position;       // 位置座標.
    float3  Normal;         // シェーディング法線.
    float3  Tangent;        // 接線ベクトル.
    float2  TexCoord;       // テクスチャ座標.
    float3  GeometryNormal; // ジオメトリ法線.
};

#if 0
//-----------------------------------------------------------------------------
//      IBLをサンプルします.
//-----------------------------------------------------------------------------
float3 SampleIBL(float3 dir)
{
    float2 uv = ToSphereMapCoord(dir);
    return BackGround.SampleLevel(LinearWrap, uv, 0.0f).rgb;
}

//-----------------------------------------------------------------------------
//      頂点インデックスを取得します.
//-----------------------------------------------------------------------------
uint3 GetIndices(uint indexId, uint triangleIndex)
{
    uint address = triangleIndex * STRIDE_INDEX;
    ByteAddressBuffer indices = ResourceDescriptorHeap[indexId];
    return indices.Load3(address);
}

//-----------------------------------------------------------------------------
//      表面交差情報を取得します.
//-----------------------------------------------------------------------------
SurfaceHit GetSurfaceHit(uint instanceId, uint triangleIndex, float2 barycentrices)
{
    uint2 id = Instances.Load2(instanceId * STRIDE_INSTANCE);

    uint3 indices = GetIndices(id.y, triangleIndex);
    SurfaceHit surfaceHit = (SurfaceHit)0;

    // 重心座標を求める.
    float3 factor = float3(
        1.0f - barycentrices.x - barycentrices.y,
        barycentrices.x,
        barycentrices.y);

    ByteAddressBuffer vertices = ResourceDescriptorHeap[id.x];

    float3 pos[3];

    float4   row0  = asfloat(Transforms.Load4(instanceId * STRIDE_TRANSFORM));
    float4   row1  = asfloat(Transforms.Load4(instanceId * STRIDE_TRANSFORM + 16));
    float4   row2  = asfloat(Transforms.Load4(instanceId * STRIDE_TRANSFORM + 32));
    float3x4 world = float3x4(row0, row1, row2);

    [unroll]
    for(uint i=0; i<3; ++i)
    {
        uint address = indices[i] * STRIDE_VERTEX;

        float3 p = asfloat(vertices.Load3(address));
        pos[i] = mul(world, float4(p, 1.0f)).xyz;

        surfaceHit.Position += p[i] * factor[i];
        surfaceHit.Normal   += asfloat(vertices.Load3(address + OFFSET_N)) * factor[i];
        surfaceHit.Tangent  += asfloat(vertices.Load3(address + OFFSET_T)) * factor[i];
        surfaceHit.TexCoord += asfloat(vertices.Load2(address + OFFSET_U)) * factor[i];
    }

    surfaceHit.Normal  = normalize(mul((float3x3)world, normalize(surfaceHit.Normal)));
    surfaceHit.Tangent = normalize(mul((float3x3)world, normalize(surfaceHit.Tangent)));

    float3 e0 = pos[1] - pos[0];
    float3 e1 = pos[2] - pos[0];
    surfaceHit.GeometryNormal = normalize(cross(e0, e1));

    return surfaceHit;
}
#endif

//-----------------------------------------------------------------------------
//      スクリーン上へのレイを求めます.
//-----------------------------------------------------------------------------
RayDesc GeneratePinholeCameraRay(float2 offset)
{
    float2 pixel = float2(DispatchRaysIndex().xy);
    const float2 resolution = float2(DispatchRaysDimensions().xy);
    pixel += lerp(-0.5f.xx, 0.5f.xx, offset);

    float2 uv = (pixel + 0.5f) / resolution;
    uv.y = 1.0f - uv.y;
    float2 clipPos = uv * 2.0f - 1.0f;

    RayDesc ray;
    ray.Origin      = GetPosition(SceneParam.View);
    ray.Direction   = CalcRayDir(clipPos, SceneParam.View, SceneParam.Proj);
    ray.TMin        = 0.1f;
    ray.TMax        = FLT_MAX;

    return ray;
}

//-----------------------------------------------------------------------------
//      レイを求めます.
//-----------------------------------------------------------------------------
void CalcRay(float2 index, out float3 pos, out float3 dir)
{
    float4 orig   = float4(0.0f, 0.0f, 0.0f, 1.0f);           // カメラの位置.
    float4 screen = float4(-2.0f * index + 1.0f, 0.0f, 1.0f); // スクリーンの位置.

    orig   = mul(SceneParam.InvView,     orig);
    screen = mul(SceneParam.InvViewProj, screen);

    // w = 1 に射影.
    screen.xyz /= screen.w;

    // レイの位置と方向を設定.
    pos = orig.xyz;
    dir = normalize(screen.xyz - orig.xyz);
}

//-----------------------------------------------------------------------------
//      シャドウレイをキャストします.
//-----------------------------------------------------------------------------
bool CastShadowRay(float3 pos, float3 normal, float3 dir, float tmax)
{
    RayDesc ray;
    ray.Origin      = OffsetRay(pos, normal);
    ray.Direction   = dir;
    ray.TMin        = 0.1f;
    ray.TMax        = tmax;

    ShadowPayload payload;
    payload.Visible = true;

    TraceRay(
        SceneAS,
        RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        SHADOW_RAY_INDEX,
        0,
        SHADOW_RAY_INDEX,
        ray,
        payload);

    return payload.Visible;
}

//-----------------------------------------------------------------------------
//      通常描画用レイ生成シェーダです.
//-----------------------------------------------------------------------------
[shader("raygeneration")]
void OnGenerateRay()
{
    const uint2 rayId = DispatchRaysIndex().xy;

    // 乱数初期化.
    uint4 seed = SetSeed(rayId, SceneParam.FrameIndex);
    float2 offset = float2(Random(seed), Random(seed));

    // レイを設定.
    RayDesc ray = GeneratePinholeCameraRay(offset);

    // パストレ.
    #if RTC_TARGET == RTC_RELEASE
        float3 radiance = PathTracing(ray);
    #else
        const bool debugRay = all(rayId == uint2(SceneParam.DebugRayIndex));
        float3 radiance = DebugTracing(ray, debugRay);
    #endif

    // アキュムレーション.
    float3 prevRadiance = Radiance[rayId].rgb;
    radiance = (SceneParam.EnableAccumulation) ? (prevRadiance + radiance) : radiance;

    // 描画結果を格納.
    Radiance[rayId] = float4(radiance, 1.0f);
}

//-----------------------------------------------------------------------------
//      通常描画用近接ヒットシェーダです.
//-----------------------------------------------------------------------------
[shader("closesthit")]
void OnClosestHit(inout Payload payload, in HitArgs args)
{
    payload.InstanceId   = InstanceID();
    payload.PrimitiveId  = PrimitiveIndex();
    payload.Barycentrics = args.barycentrics;
}

//-----------------------------------------------------------------------------
//      通常描画用ミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnMiss(inout Payload payload)
{
    payload.InstanceId  = INVALID_ID;
    payload.PrimitiveId = INVALID_ID;
}

//-----------------------------------------------------------------------------
//      シャドウ用任意ヒットシェーダです.
//-----------------------------------------------------------------------------
[shader("anyhit")]
void OnShadowAnyHit(inout ShadowPayload payload, in HitArgs args)
{
    payload.Visible = true;
    AcceptHitAndEndSearch();
}

//-----------------------------------------------------------------------------
//      シャドウ用ミスシェーダです.
//-----------------------------------------------------------------------------
[shader("miss")]
void OnShadowMiss(inout ShadowPayload payload)
{
    payload.Visible = false;
}

//-----------------------------------------------------------------------------
//      パストレーシング処理.
//-----------------------------------------------------------------------------
float3 PathTracing(RayDesc ray)
{
    Payload payload = (Payload)0;

    float3 W  = 1.0f.xxx;
    float3 Lo = 0.0f.xxx;

    return SaturateFloat(Lo);
}

//-----------------------------------------------------------------------------
//      デバッグトレーシング処理.
//-----------------------------------------------------------------------------
float3 DebugTracing(RayDesc ray, bool debugRay)
{
    uint vertexCount = debugRay ? 1 : 0;
    if (debugRay) {
        RayPoints.Store4(0, asuint(float4(ray.Origin, 0.0f)));
    }

    Payload payload = (Payload)0;
    TraceRay(SceneAS, RAY_FLAG_NONE, ~0, STANDARD_RAY_INDEX, 0, STANDARD_RAY_INDEX, ray, payload);

    float3 color = payload.HasHit() ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 0.0f);

    if (debugRay && payload.HasHit())
    {
        // ヒット時に位置座標を記録.
        //RayPoints.Store4(sizeof(float4) * vertexCount, asuint(float4(hitPos, vertexCount)));

        //// デバッグレイ描画用頂点数をカウントアップ.
        //vertexCount++;

        //// 描画引数作成.
        //DrawArgs[0] = uint4(vertexCount, 1, 0, 0);
    }

    return SaturateFloat(color);
}