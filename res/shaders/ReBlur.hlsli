//-----------------------------------------------------------------------------
// File : ReBlur.hlsli
// Desc : ReBLUR Common Functions
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef REBLUR_HLSLI
#define REBLUR_HLSLI

#include "Math.hlsli"

#define REBLUR_MAX_ACCUM_FRAME_NUM         (63)
#define REBLUR_SPEC_ACCUM_BASE_POWER       (0.5)
#define REBLUR_SPEC_ACCUM_CURVE            (0.66)
//#define REBLUR_ALMOST_ZERO_ACCUM_FRAME_NUM (0.01)
//#define REBLUR_PLANE_DIST_SENSITIVITY      (0.002) // [m]

#define REBLUR_ROUGHNESS_ULP               (1.5 / 255.0)
#define REBLUR_NORMAL_ULP                  (2.0 / 255.0)

// samples = 8, min distance = 0.5, average samples on radius = 2, z = length(xy).
static const float3 kPoisson8[8] = {
    float3( -0.4706069, -0.4427112, +0.6461146 ),
    float3( -0.9057375, +0.3003471, +0.9542373 ),
    float3( -0.3487388, +0.4037880, +0.5335386 ),
    float3( +0.1023042, +0.6439373, +0.6520134 ),
    float3( +0.5699277, +0.3513750, +0.6695386 ),
    float3( +0.2939128, -0.1131226, +0.3149309 ),
    float3( +0.7836658, -0.4208784, +0.8895339 ),
    float3( +0.1564120, -0.8198990, +0.8346850 )
};

float PixelRadiusToWorld(float unproject, float orthoMode, float pixelRadius, float viewZ)
{
    return pixelRadius * unproject * lerp(viewZ, 1.0f, abs(orthoMode));
}

float GetFrustumSize(float minRectDimMulUnproject, float orthoMode, float viewZ)
{
    return minRectDimMulUnproject * lerp(viewZ, 1.0f, abs(orthoMode);
}

float ComputeParallax(float3 X, float3 Xprev, float3 cameraDelta)
{
    float3 V = normalize(X);
    float3 Vprev = normalize(Xprev - cameraDelta);
    float cosa = saturate(dot(V, Vprev));
    float parallax = sqrt(1.0f - cosa * cosa) / max(cosa, 1e-6f);
    parallax *= 60.0f; // Optinoally normalized to 60 FPS.
    return parallax;
}

float GetSpecAccumSpeed(float Amax, float roughness, float NoV, float parallax)
{
    float m = pow2(roughness);
    float acos01sq = 1.0f - NoV; // Approximation of acos^2 in normalized form.
    float a = pow(saturate(acos01sq), SPEC_ACCUM_CURVE);
    float b = 1.1 + m;
    float parallaxSensitivity = (b + a) / (b - a);
    float powerScale = 1.0f + parallax * parallaxSensitivity;
    float f = 1.0f - exp2(-200.0 * m);
    f *= pow(saturate(roughness), REBLUR_SPEC_ACCUM_BASE_POWER * powerScale);
    float A = REBULUR_MAX_ACCUM_FRAME_NUM * f;
    return min(A, Amax);
}

float GetSpecularLobeHalfAngle(float linearRoughness, float percentOfVolume = 0.75)
{
    float m = linearRoughness * linearRoughness;
    return atan(m * percentOfVolume / (1.0f - percentOfVolume));
}

float GetSpecularDominantFactor(float NoV, float roughness)
{
    float a = 0.298475 * log(39.4115 - 39.0029 * roughness);
    float f = pow(saturate(1.0f - NoV), 10.8640) * (1.0f - a ) + a;
    return saturate(f);
}

float3 GetSpecularDominantDirection(float3 N, float3 V, float roughness)
{
    float f = (1.0f - roughness) * (sqrt(1.0f - roughness) + roughness);
    float3 R = reflect(-V, N);
    float3 dir = lerp(N, R, f);
    return normalize(dir);
}

float GetXvirtual(float3 X, float3 V, float NoV, float roughness, float hitDist)
{
    float f = GetSpecularDominantFactor(NoV, roughness);
    return X - V * hitDist * f;
}

float IsInScreen(float2 uv) 
{
    return float(all(saturate(uv) == uv));
}

float2x3 GetKernelBasis(float3 V, float3 N, float NoD, float roughness = 1.0f, float anisoFade = 1.0f)
{
    float3 T, B;
    CalcONB(N, T, B);

    if (NoD < 0.999)
    {
        float3 R = reflect(-D, N);
        T = normalize(cross(N, R));
        B = cross(R, T);

        float skewFactor = lerp(0.5f + 0.5f * roughness, 1.0f, NoD);
        T *= skewFactor;
    }

    return float2x3(T, B);
}

float2 GetGeometryWeightParams(float planeDistSensitivity, float frustumSize, float3 Xv, float3 Nv, float nonLinearAccumSpeed)
{
    float relaxation = lerp(1.0f, 0.25f, nonLinearAccumSpeed);
    float a = relaxation / (planeDistSensitivity * frustumSize);
    float b = -dot(Nv, Xv) * a;
    return float2(a, b);
}

float GetNormalWeightParams(float nonLinearAccumSpeed, float fraction, float roughness = 1.0f)
{
    float angle = GetSpecularLobeHalfAngle(roughness);
    angle *= lerp(saturate(fraction), 1.0f, nonLinearAccumSpeed);
    return 1.0f / max(angle, REBLUR_NORMAL_ULP);
}

float2 GetHitDistanceWeightParams(float hitDist, float nonLinearAccumSpeed, float roughness = 1.0f)
{
    float angle = GetSpecularLobeHalfAngle(roughness, 0.987);
    float almostHalfPi = GetSpecularLobeHalfAngle(1.0f, 0.987);
    float specularMagicCurve = saturate(angle / almostHalfPi);

    float norm = lerp(1e-6f, 1.0f, min(nonLinearAccumSpeed, specularMagicCurve));
    float a = 1.0f / norm;
    float b = hitDist * a;
    return float2(a, -b);
}

float2 GetRoughnessWeightParams(float roughness, float fraction)
{
    float a = rcp(lerp(0.01f, 1.0f, saturate(roughness * fraction)));
    float b = roughness * a;
    return float2(a, -b);
}

float ComputeNonExponentialWeight(float x, float px, float py)
{
    return SmoothStep(0.999, 0.001f, abs(x * px + py));
}

float ComputeExponentalWeight(float x, float px, float py)
{
    float v = -3.0f * abs(x * px + py);
    return rcp(v * v - v + 1.0f);
}

float GetGeometryWeight(float2 params, float3 n0, float3 p)
{
    float d = dot(n0, p);
    return ComputeNonExponentialWeight(d, params.x, params.y);
}

float GetNormalWeight(float param, float3 n0, float3 n)
{
    float cosa = saturate(dot(n0, n));
    float angle = acos(cosa);
    return ComputeNonExponentialWeight(angle, param, 0.0f);
}

float GetHitDistanceWeight(float2 params, float hitDist)
{
    return ComputeExponentialWeight(hitDist, params.x, params.y);
}

float GetGaussianWeight(float r)
{
    return exp(-0.66 * r * r); // assuming r is normalized to 1.
}

#endif//REBLUR_HLSLI

