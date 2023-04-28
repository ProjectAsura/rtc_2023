//-----------------------------------------------------------------------------
// File : DebugPS.hlsl
// Desc : Pixel Shader For Debug.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <Math.hlsli>
#include <Samplers.hlsli>

#define TYPE_RGBA       (0)
#define TYPE_RGB        (1)
#define TYPE_R_ONLY     (2)
#define TYPE_G_ONLY     (3)
#define TYPE_B_ONLY     (4)
#define TYPE_NORMAL     (5)
#define TYPE_VELOCITY   (6)
#define TYPE_HEAT_MAP   (7)

///////////////////////////////////////////////////////////////////////////////
// VSOutput structure
///////////////////////////////////////////////////////////////////////////////
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0; 
};

//-----------------------------------------------------------------------------
// Resources
//-----------------------------------------------------------------------------
cbuffer CbParam : register(b0)
{
    uint    Type;
    uint3   Reserved;
};
Texture2D ColorMap : register(t0);

//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
float4 main(const VSOutput input) : SV_TARGET0
{
    float4 output = float4(0.0f, 0.0f, 0.0f, 1.0f);

    switch(Type)
    {
    case TYPE_RGBA:
        {
            output = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0);
        }
        break;

    case TYPE_RGB:
        {
            output.rgb = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).rgb;
        }
        break;

    case TYPE_R_ONLY:
        {
            output.rgb = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).rrr;
        }
        break;

    case TYPE_G_ONLY:
        {
            output.rgb = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).ggg;
        }
        break;

    case TYPE_B_ONLY:
        {
            output.rgb = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).bbb;
        }
        break;

    case TYPE_NORMAL:
        {
            float3 normal = UnpackNormal(ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).xy);
            output.rgb = normal * 0.5f + 0.5f;
        }
        break;

    case TYPE_VELOCITY:
        {
            float2 velocity = ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).xy;
            output.rg = velocity * 0.5f + 0.5f;
        }
        break;

    case TYPE_HEAT_MAP:
        {
            output.rgb = HeatMap(ColorMap.SampleLevel(LinearClamp, input.TexCoord, 0).x);
        }
        break;
    }

    return output;
}