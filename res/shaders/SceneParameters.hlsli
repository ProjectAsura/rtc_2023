//-----------------------------------------------------------------------------
// File : SceneParameters.hlsli
// Desc : Scene Parameters.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef SCENE_PARAMETERS_HLSLI
#define SCENE_PARAMETERS_HLSLI

///////////////////////////////////////////////////////////////////////////////
// SceneParam structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParameters
{
    float4x4 View;              // ビュー行列.
    float4x4 Proj;              // 射影行列.
    float4x4 InvView;           // ビュー行列の逆行列.
    float4x4 InvProj;           // 射影行列の逆行列.
    float4x4 InvViewProj;       // ビュー射影行列の逆行列.

    float4x4 PrevView;          // 前フレームのビュー行列.
    float4x4 PrevProj;          // 前フレームの射影行列.
    float4x4 PrevInvView;       // 前フレームのビュー行列の逆行列.
    float4x4 PrevInvProj;       // 前フレームの射影行列の逆行列.
    float4x4 PrevInvViewProj;   // 前フレームのビュー射影行列の逆行列.

    float4  ScreenSize;         // (w, h, 1/w, 1/h).
    float3  CameraDir;          // カメラの方向ベクトル.
    uint    MaxIteration;       // 最大イタレーション回数.

    uint    FrameIndex;         // フレーム番号.
    float   AnimationTime;      // アニメーション時間[sec].
    bool    EnableAccumulation; // アキュームレーション有効フラグ.
    uint    AccumulatedFrames;  // アキュームレーション済みフレーム数.
};

#endif//SCENE_PARAMETERS_HLSLI
