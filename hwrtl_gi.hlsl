/***************************************************************************
MIT License

Copyright(c) 2023 lvchengTSH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this softwareand associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright noticeand this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***************************************************************************/


// TODO:
//      1. path tracing light grid
//      2. first bounce ray guiding
//      3. SelectiveLightmapOutputCS
//      4. ReduceSHRinging
//      5. https://www.ppsloan.org/publications/StupidSH36.pdf
//      6. SH Div Sample Count
//
//      7. How to encode light map? AHD or Color Luma Directionality?    
//      8. SHBasisFunction yzx?
//      9. we nned to add the shading normal gbuffer? No?
//      10. merge light map?
//
//      11. Light Map GBuffer Generation: https://ndotl.wordpress.com/2018/08/29/baking-artifact-free-lightmaps/
//      12. shadow ray bias optimization
//
//      13. should we add luma to light map?
//      14. light add and scale factor : use 10 1 for now,we should add a post process pass to get the max negetive value
//      15. SHBasisFunction in unreal sh projection factor is 0.28 -0.48 0.48 -0.48? 
//      
//      16. Russian roulette: Unreal's Two Tweak or godot optimization :// <https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer>
//      17. MISWeightRobust
//      18. physical light unit
//
//      19. // "Precision Improvements for Ray / Sphere Intersection" - Ray Tracing Gems (2019) // https://link.springer.com/content/pdf/10.1007%2F978-1-4842-4427-2_7.pdf
//      
//      20. surpport mask material for foliage
//      21. add blend seams pass

//#ifndef INCLUDE_RT_SHADER
//#define INCLUDE_RT_SHADER 1
//#endif
//
//#ifdef RT_DEBUG_OUTPUT
//#define RT_DEBUG_OUTPUT 0
//#endif

SamplerState gSamPointWarp : register(s0, space1000);
SamplerState gSamLinearWarp : register(s4, space1000);
SamplerState gSamLinearClamp : register(s5, space1000);

/***************************************************************************
*   LightMap GBuffer Generation Pass
***************************************************************************/

struct SGeometryApp2VS
{
	float3 m_posistion    : TEXCOORD0;
	float2 m_lightmapuv   : TEXCOORD1;
};

struct SGeometryVS2PS
{
  float4 m_position : SV_POSITION;
  float4 m_worldPosition :TEXCOORD0;
};

cbuffer CGeomConstantBuffer : register(b0)
{
    float4x4 m_worldTM;
    float4   m_lightMapScaleAndBias;
    float padding[44];
};

SGeometryVS2PS LightMapGBufferGenVS(SGeometryApp2VS IN )
{
    SGeometryVS2PS vs2PS = (SGeometryVS2PS) 0;

    float2 lightMapCoord = IN.m_lightmapuv * m_lightMapScaleAndBias.xy + m_lightMapScaleAndBias.zw;

    vs2PS.m_position = float4((lightMapCoord - float2(0.5,0.5)) * float2(2.0,-2.0),0.0,1.0);
    vs2PS.m_worldPosition = mul(m_worldTM, float4(IN.m_posistion,1.0));
    return vs2PS;
}

struct SLightMapGBufferOutput
{
    float4 m_worldPosition :SV_Target0;
    float4 m_worldFaceNormal :SV_Target1;
};

SLightMapGBufferOutput LightMapGBufferGenPS(SGeometryVS2PS IN)
{
    SLightMapGBufferOutput output;

    float3 faceNormal = normalize(cross(ddx(IN.m_worldPosition.xyz), ddy(IN.m_worldPosition.xyz)));
    
    //float3 deltaPosition = max(abs(ddx(IN.m_worldPosition)), abs(ddy(IN.m_worldPosition)));
    //float texelSize = max(deltaPosition.x, max(deltaPosition.y, deltaPosition.z));
    //texelSize *= sqrt(2.0); 

    output.m_worldPosition      = IN.m_worldPosition;
    output.m_worldFaceNormal = float4(-faceNormal, 1.0);
    return output;
}

#if INCLUDE_RT_SHADER
/***************************************************************************
*   LightMap Ray Tracing Pass
***************************************************************************/

#define RT_MAX_SCENE_LIGHT 128
#define POSITIVE_INFINITY (asfloat(0x7F800000))
#define PI (3.14159265358979)

#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_PAYLOAD_FLAG_FRONT_FACE ( 1<< 0)

// eval Material Define
#define SHADING_MODEL_DEFAULT_LIT (1 << 1)
// sample Light Define
#define RT_LIGHT_TYPE_DIRECTIONAL (1 << 0)
#define RT_LIGHT_TYPE_SPHERE (1 << 1)
// ray tacing shader index
#define RT_MATERIAL_SHADER_INDEX 0
#define RT_SHADOW_SHADER_INDEX 1
#define RT_SHADER_NUM 2

static const uint maxBounces = 32;
static const uint debugSample = 0;

struct SRayTracingLight
{
    float3 m_color; // light power
    uint m_isStationary; // stationary or static light

    float3 m_lightDirection; // light direction
    uint m_eLightType; // spjere light / directional light

    float3 m_worldPosition; // spjere light world position
    float m_vAttenuation; // spjere light attenuation

    float m_radius; // spjere light radius
    float3 m_rtLightpadding;
};

struct SMeshInstanceGpuData
{   
    float4x4 m_worldTM;
    uint ibStride;
    uint ibIndex;
    uint vbIndex;
    uint unused;
};

ByteAddressBuffer bindlessByteAddressBuffer[] : register(t0, space1);
StructuredBuffer<SMeshInstanceGpuData> rtSceneInstanceGpuData : register(t0, space2);

struct SRtRenderPassInfo
{
    uint m_renderPassIndex;
    uint rpInfoPadding0;
    uint rpInfoPadding1;
    uint rpInfoPadding2;
};
ConstantBuffer<SRtRenderPassInfo> rtRenderPassInfo : register(b0, space2);

cbuffer CRtGlobalConstantBuffer : register(b0)
{
    uint m_nRtSceneLightCount;
    uint2 m_nAtlasSize;
    uint m_rtSampleIndex;
    float m_rtGlobalCbPadding[60];
};

RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> rtWorldPosition : register(t1);
Texture2D<float4> rtWorldNormal : register(t2);
StructuredBuffer<SRayTracingLight> rtSceneLights : register(t3);

RWTexture2D<float4> irradianceAndValidSampleCount : register(u0);
RWTexture2D<float4> shDirectionality : register(u1);

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

struct SMaterialClosestHitPayload
{
    float3 m_worldPosition;
    float3 m_worldNormal;

#if RT_DEBUG_OUTPUT
    float4 m_debugPayload;
#endif

    float m_vHiTt;
    uint m_eFlag; //e.g. front face flag

    // eval material
    float m_roughness;
    float3 m_baseColor;
    float3 m_diffuseColor;
    float3 m_specColor;
};

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Common function
***************************************************************************/

float Pow2(float inputValue)
{
    return inputValue * inputValue;
}

float Pow5(float inputValue)
{
    float pow2Value = inputValue * inputValue;
    return pow2Value * pow2Value * inputValue;
}

float Luminance(float3 color)
{
    return dot(color,float3(0.3,0.59,0.11));
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Estimate Light
***************************************************************************/

float GetLightFallof(float vSquaredDistance,int nLightIndex)
{
    float invLightAttenuation = 1.0 / rtSceneLights[nLightIndex].m_vAttenuation;
    float normalizedSquaredDistance = vSquaredDistance * invLightAttenuation * invLightAttenuation;
    return saturate(1.0 - normalizedSquaredDistance) * saturate(1.0 - normalizedSquaredDistance);
}

float EstimateDirectionalLight(uint nlightIndex)
{
    return Luminance(rtSceneLights[nlightIndex].m_color);
}

float EstimateSphereLight(uint nlightIndex,float3 worldPosition)
{
    float3 lightDirection = float3(rtSceneLights[nlightIndex].m_worldPosition - worldPosition);
    float squaredLightDistance = dot(lightDirection,lightDirection);
    float lightPower = Luminance(rtSceneLights[nlightIndex].m_color);

    return lightPower * GetLightFallof(squaredLightDistance,nlightIndex) / squaredLightDistance;
}

float EstimateLight(uint nlightIndex, float3 worldPosition,float3 worldNormal)
{
    switch(rtSceneLights[nlightIndex].m_eLightType)
    {
        case RT_LIGHT_TYPE_DIRECTIONAL: return EstimateDirectionalLight(nlightIndex);
        case RT_LIGHT_TYPE_SPHERE: return EstimateSphereLight(nlightIndex,worldPosition);
        default: return 0.0;
    }
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Random Sequence
***************************************************************************/

static const uint gPrimes512LUT[] = {
	2,3,5,7,11,13,17,19,23,29,
	31,37,41,43,47,53,59,61,67,71,
	73,79,83,89,97,101,103,107,109,113,
	127,131,137,139,149,151,157,163,167,173,
	179,181,191,193,197,199,211,223,227,229,
	233,239,241,251,257,263,269,271,277,281,
	283,293,307,311,313,317,331,337,347,349,
	353,359,367,373,379,383,389,397,401,409,
	419,421,431,433,439,443,449,457,461,463,
	467,479,487,491,499,503,509,521,523,541,
	547,557,563,569,571,577,587,593,599,601,
	607,613,617,619,631,641,643,647,653,659,
	661,673,677,683,691,701,709,719,727,733,
	739,743,751,757,761,769,773,787,797,809,
	811,821,823,827,829,839,853,857,859,863,
	877,881,883,887,907,911,919,929,937,941,
	947,953,967,971,977,983,991,997,1009,1013,
	1019,1021,1031,1033,1039,1049,1051,1061,1063,1069,
	1087,1091,1093,1097,1103,1109,1117,1123,1129,1151,
	1153,1163,1171,1181,1187,1193,1201,1213,1217,1223,
	1229,1231,1237,1249,1259,1277,1279,1283,1289,1291,
	1297,1301,1303,1307,1319,1321,1327,1361,1367,1373,
	1381,1399,1409,1423,1427,1429,1433,1439,1447,1451,
	1453,1459,1471,1481,1483,1487,1489,1493,1499,1511,
	1523,1531,1543,1549,1553,1559,1567,1571,1579,1583,
	1597,1601,1607,1609,1613,1619,1621,1627,1637,1657,
	1663,1667,1669,1693,1697,1699,1709,1721,1723,1733,
	1741,1747,1753,1759,1777,1783,1787,1789,1801,1811,
	1823,1831,1847,1861,1867,1871,1873,1877,1879,1889,
	1901,1907,1913,1931,1933,1949,1951,1973,1979,1987,
	1993,1997,1999,2003,2011,2017,2027,2029,2039,2053,
	2063,2069,2081,2083,2087,2089,2099,2111,2113,2129,
	2131,2137,2141,2143,2153,2161,2179,2203,2207,2213,
	2221,2237,2239,2243,2251,2267,2269,2273,2281,2287,
	2293,2297,2309,2311,2333,2339,2341,2347,2351,2357,
	2371,2377,2381,2383,2389,2393,2399,2411,2417,2423,
	2437,2441,2447,2459,2467,2473,2477,2503,2521,2531,
	2539,2543,2549,2551,2557,2579,2591,2593,2609,2617,
	2621,2633,2647,2657,2659,2663,2671,2677,2683,2687,
	2689,2693,2699,2707,2711,2713,2719,2729,2731,2741,
	2749,2753,2767,2777,2789,2791,2797,2801,2803,2819,
	2833,2837,2843,2851,2857,2861,2879,2887,2897,2903,
	2909,2917,2927,2939,2953,2957,2963,2969,2971,2999,
	3001,3011,3019,3023,3037,3041,3049,3061,3067,3079,
	3083,3089,3109,3119,3121,3137,3163,3167,3169,3181,
	3187,3191,3203,3209,3217,3221,3229,3251,3253,3257,
	3259,3271,3299,3301,3307,3313,3319,3323,3329,3331,
	3343,3347,3359,3361,3371,3373,3389,3391,3407,3413,
	3433,3449,3457,3461,3463,3467,3469,3491,3499,3511,
	3517,3527,3529,3533,3539,3541,3547,3557,3559,3571,
	3581,3583,3593,3607,3613,3617,3623,3631,3637,3643,
	3659,3671
};

uint Prime512(uint dimension)
{
	return gPrimes512LUT[dimension % 512];
}

float Halton(uint nIndex, uint nBase)
{
	float r = 0.0;
	float f = 1.0;

	float nBaseInv = 1.0 / nBase;
	while (nIndex > 0)
	{
		f *= nBaseInv;
		r += f * (nIndex % nBase);
		nIndex /= nBase;
	}

	return r;
}

struct SRandomSequence
{
    uint m_nSampleIndex;
    uint m_randomSeed;
};

uint StrongIntegerHash(uint x)
{
	// From https://github.com/skeeto/hash-prospector
	x ^= x >> 16;
	x *= 0xa812d533;
	x ^= x >> 15;
	x *= 0xb278e4ad;
	x ^= x >> 17;
	return x;
}

float4 GetRandomSampleFloat4(inout SRandomSequence randomSequence)
{
    float4 result;
    result.x = Halton(randomSequence.m_nSampleIndex, Prime512(randomSequence.m_randomSeed + 0));
    result.y = Halton(randomSequence.m_nSampleIndex, Prime512(randomSequence.m_randomSeed + 1));
    result.z = Halton(randomSequence.m_nSampleIndex, Prime512(randomSequence.m_randomSeed + 2));
    result.w = Halton(randomSequence.m_nSampleIndex, Prime512(randomSequence.m_randomSeed + 3));
    randomSequence.m_randomSeed += 4;
    return result;
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       MonteCarlo
***************************************************************************/

// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
float3x3 GetTangentBasis( float3 tangentZ )
{
	const float sign = tangentZ.z >= 0 ? 1 : -1;
	const float a = -rcp( sign + tangentZ.z );
	const float b = tangentZ.x * tangentZ.y * a;
	
	float3 tangentX = { 1 + sign * a * Pow2( tangentZ.x ), sign * b, -sign * tangentZ.x };
	float3 tangentY = { b,  sign + a * Pow2( tangentZ.y ), -tangentZ.y };

	return float3x3( tangentX, tangentY, tangentZ );
}

float3 TangentToWorld( float3 inputVector, float3 tangentZ )
{
	return mul( inputVector, GetTangentBasis( tangentZ ) );
}

//https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources
float4 UniformSampleConeRobust(float2 E, float SinThetaMax2)
{
	float Phi = 2 * PI * E.x;
	float OneMinusCosThetaMax = SinThetaMax2 < 0.01 ? SinThetaMax2 * (0.5 + 0.125 * SinThetaMax2) : 1 - sqrt(1 - SinThetaMax2);

	float CosTheta = 1 - OneMinusCosThetaMax * E.y;
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 L;
	L.x = SinTheta * cos(Phi);
	L.y = SinTheta * sin(Phi);
	L.z = CosTheta;
	float PDF = 1.0 / (2 * PI * OneMinusCosThetaMax);

	return float4(L, PDF);
}

float4 CosineSampleHemisphere( float2 E )
{
	float Phi = 2 * PI * E.x;
	float CosTheta = sqrt(E.y);
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	float PDF = CosTheta * (1.0 / PI);

	return float4(H, PDF);
}

float MISWeightRobust(float Pdf, float OtherPdf) 
{
	if (Pdf == OtherPdf)
	{
		return 0.5f;
	}

	if (OtherPdf < Pdf)
	{
		float x = OtherPdf / Pdf;
		return 1.0 / (1.0 + x * x);
	}
	else
	{
		float x = Pdf / OtherPdf;
		return 1.0 - 1.0 / (1.0 + x * x);
	}
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Light Sampling
***************************************************************************/

struct SLightSample 
{
	float3 m_radianceOverPdf;
	float m_pdf;
	float3 m_direction;
	float m_distance;
};

void SelectLight(float vRandom, int nLights, inout float aLightPickingCdf[RT_MAX_SCENE_LIGHT], out uint nSelectedIndex, out float vLightPickPdf)
{
#if 1
    float preCdf = 0;
    for(nSelectedIndex = 0; nSelectedIndex < nLights;nSelectedIndex++)
    {
        if(vRandom < aLightPickingCdf[nSelectedIndex])
        {
            break;
        }
        preCdf = aLightPickingCdf[nSelectedIndex];
    }

    vLightPickPdf = aLightPickingCdf[nSelectedIndex] - preCdf;
#else
    nSelectedIndex = 0;
    for(int vRange = nLights; vRange > 0;)
    {
        int vStep = vRange / 2;
        int nLightIndex = nSelectedIndex + vStep;
        if(vRandom < aLightPickingCdf[nLightIndex])
        {
            vRange = vStep;
        }
        else
        {
            nSelectedIndex = nLightIndex + 1;
            vRange = vRange - (vStep + 1);
        }
    }

    vLightPickPdf = aLightPickingCdf[nSelectedIndex] - (nSelectedIndex > 0 ? aLightPickingCdf[nSelectedIndex - 1] : 0.0);
#endif
}

//TODO:
//1. Unform Sample Direction?
//2. ? Because the light is normalized by the solid angle, the radiance/pdf ratio is just the color
SLightSample SampleDirectionalLight(int nLightIndex, float2 randomSample, float3 worldPos, float3 worldNormal)
{
    SLightSample lightSample = (SLightSample)0;
    lightSample.m_radianceOverPdf = rtSceneLights[nLightIndex].m_color / 1.0f;
    lightSample.m_pdf = 1.0;
    lightSample.m_direction = normalize(rtSceneLights[nLightIndex].m_lightDirection);
    lightSample.m_distance = POSITIVE_INFINITY;
    return lightSample;
}

// TODO:
// 1. See pbrt-v4 sample sphere light
// 2. find a better way of handling the region inside the light than just clamping to 1.0 here
SLightSample SampleSphereLight(int nLightIndex, float2 randomSample, float3 worldPos, float3 worldNormal)
{
    float3 lightDirection = rtSceneLights[nLightIndex].m_worldPosition - worldPos;
	float lightDistanceSquared = dot(lightDirection, lightDirection);

    float radius = rtSceneLights[nLightIndex].m_radius;
	float radius2 = radius * radius;

    float sinThetaMax2 = saturate(radius2 / lightDistanceSquared);
    float4 dirAndPdf = UniformSampleConeRobust(randomSample, sinThetaMax2);

    float cosTheta = dirAndPdf.z;
	float sinTheta2 = 1.0 - cosTheta * cosTheta;

    SLightSample lightSample = (SLightSample)0;
	lightSample.m_direction = normalize(TangentToWorld(dirAndPdf.xyz, normalize(lightDirection)));
	lightSample.m_distance = length(lightDirection) * (cosTheta - sqrt(max(sinThetaMax2 - sinTheta2, 0.0)));
    lightSample.m_pdf = dirAndPdf.w;

    float3 lightPower = rtSceneLights[nLightIndex].m_color;
	float3 lightRadiance = lightPower / (PI * radius2);

    lightSample.m_radianceOverPdf = sinThetaMax2 < 0.001 ? lightPower / lightDistanceSquared : lightRadiance / lightSample.m_pdf;
    return lightSample;
}

SLightSample SampleLight(int nLightIndex,float2 vRandSample,float3 vWorldPos,float3 vWorldNormal)
{
    switch(rtSceneLights[nLightIndex].m_eLightType)
    {
        case RT_LIGHT_TYPE_DIRECTIONAL: return SampleDirectionalLight(nLightIndex,vRandSample,vWorldPos,vWorldNormal);
        case RT_LIGHT_TYPE_SPHERE: return SampleSphereLight(nLightIndex,vRandSample,vWorldPos,vWorldNormal);
        default: return (SLightSample)0;
    }
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Eval Material Brdf etc
***************************************************************************/

struct SMaterialEval
{
    float3 m_weight; 
    float m_pdf;
};

SMaterialEval EvalLambertMaterial(float3 outgoingDirection,SMaterialClosestHitPayload payload)
{
    SMaterialEval materialEval = (SMaterialEval)0;
    float3 N_World = payload.m_worldNormal;
	float NoL = saturate(dot(N_World, outgoingDirection));
    
    materialEval.m_weight = payload.m_baseColor;
    materialEval.m_pdf =  (NoL / PI);
    return materialEval;
}

SMaterialEval EvalMaterial(float3 outgoingDirection, SMaterialClosestHitPayload payload)
{
    return EvalLambertMaterial(outgoingDirection,payload);
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       sample Material
***************************************************************************/

struct SMaterialSample
{
    float3 m_direction;
    float m_pdf;
    float3 m_weight;
};

SMaterialSample SampleLambertMaterial(SMaterialClosestHitPayload payload, float4 randomSample)
{
    float3 worldNormal = payload.m_worldNormal;
    float4 sampleValue = CosineSampleHemisphere(randomSample.xy);

    SMaterialSample materialSample = (SMaterialSample)0;
    materialSample.m_direction = TangentToWorld(sampleValue.xyz,worldNormal);
    materialSample.m_weight = payload.m_baseColor;
    materialSample.m_pdf = sampleValue.w;
    return materialSample;
}

SMaterialSample SampleMaterial(SMaterialClosestHitPayload payload, float4 randomSample)
{
    return SampleLambertMaterial(payload,randomSample);
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Encode LightMap
***************************************************************************/

//TODO: ReduceSHRinging https://zhuanlan.zhihu.com/p/144910975
//https://upload-images.jianshu.io/upload_images/26934647-52d3e477a26bb1aa.png?imageMogr2/auto-orient/strip|imageView2/2/w/750/format/webp
float4 SHBasisFunction(float3 inputVector)
{
	float4 projectionResult;
	projectionResult.x = 0.282095f; 
	projectionResult.y = 0.488603f * inputVector.y;
	projectionResult.z = 0.488603f * inputVector.z;
	projectionResult.w = 0.488603f * inputVector.x;
	return projectionResult;
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Trace Light
***************************************************************************/

struct SLightTraceResult
{
    float3 m_radiance;
    float m_pdf;
    float m_hitT;
};

SLightTraceResult TraceDirectionalLight(RayDesc ray,uint nlightIndex)
{
    SLightTraceResult lightTraceResult = (SLightTraceResult)0;
    if(ray.TMax == POSITIVE_INFINITY)
    {
        float cosTheta = dot(rtSceneLights[nlightIndex].m_lightDirection,ray.Direction);
        if(cosTheta > 0)
        {
            lightTraceResult.m_radiance = rtSceneLights[nlightIndex].m_color * cosTheta;
            lightTraceResult.m_pdf = 1.0;
            lightTraceResult.m_hitT = POSITIVE_INFINITY;
        }
    }
    return lightTraceResult;
}

SLightTraceResult TraceSphereLight(RayDesc ray,uint nlightIndex)
{
    SLightTraceResult lightTraceResult = (SLightTraceResult)0;

    float3 lightPosition = rtSceneLights[nlightIndex].m_worldPosition;
    float lightRadius = rtSceneLights[nlightIndex].m_radius;
    float lightRadius2 = lightRadius * lightRadius;
    float3 oc = ray.Origin - lightPosition;
    float b = dot(oc,ray.Direction);
    float3 sub = oc - b * ray.Direction;
    float h = lightRadius2 - dot(sub,sub);
    if (h > 0)
	{
		float t = -b - sqrt(h);
		if (t > ray.TMin && t < ray.TMax)
		{
            float lightDistanceSquared = dot(oc, oc);

            float3 radiance = rtSceneLights[nlightIndex].m_color / ( 4.0 * 3.1415926535 * lightRadius2) * GetLightFallof(lightDistanceSquared,nlightIndex);
            
            float sinThetaMax2 = saturate(lightRadius2 / lightDistanceSquared);
			float oneMinusCosThetaMax = sinThetaMax2 < 0.01 ? sinThetaMax2 * (0.5 + 0.125 * sinThetaMax2) : 1 - sqrt(1 - sinThetaMax2);

			float solidAngle = 2 * PI * oneMinusCosThetaMax;
            lightTraceResult.m_radiance = radiance;
            lightTraceResult.m_pdf = 1.0 / solidAngle;
            lightTraceResult.m_hitT = t;
        }
    }
    return lightTraceResult;
}

SLightTraceResult TraceLight(RayDesc ray,uint nlightIndex)
{
    switch(rtSceneLights[nlightIndex].m_eLightType)
    {
        case RT_LIGHT_TYPE_DIRECTIONAL: return TraceDirectionalLight(ray,nlightIndex);
        case RT_LIGHT_TYPE_SPHERE: return TraceSphereLight(ray,nlightIndex);
        default: return (SLightTraceResult)0;
    }
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Trace Light Ray
***************************************************************************/

SMaterialClosestHitPayload TraceLightRay(RayDesc ray, bool bLastBounce, inout float3 pathThroughput,inout float3 radiance)
{
    SMaterialClosestHitPayload materialCHSPayload = (SMaterialClosestHitPayload)0;
    if(bLastBounce)
    {
        materialCHSPayload.m_vHiTt = -1.0;
        pathThroughput = 0.0;
        return materialCHSPayload;
    }

    TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_MATERIAL_SHADER_INDEX, 1, 0, ray, materialCHSPayload);

    // step3: Calculate Le in TraceLightRay function
    for(uint index = 0; index < m_nRtSceneLightCount; index++)
    {
        RayDesc lightRay = ray;
        lightRay.TMax = materialCHSPayload.m_vHiTt < 0.0 ? ray.TMax : materialCHSPayload.m_vHiTt;
        float3 lightRadiance = TraceLight(lightRay,index).m_radiance;
        radiance += pathThroughput * lightRadiance;
    }

    return materialCHSPayload;
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Trace Ray
***************************************************************************/

void DoRayTracing(
#if RT_DEBUG_OUTPUT
    inout float4 rtDebugOutput,
#endif
    float3 worldPosition,
    float3 faceNormal,
    inout SRandomSequence randomSequence,
    inout bool bIsValidSample,

    inout float3 radianceValue,
    inout float3 radianceDirection,

    inout float3 directionalLightRadianceValue,
	inout float3 directionalLightRadianceDirection
)
{
    float3 radiance = 0;

    RayDesc ray;
    
    // path state variables
	float3 pathThroughput = 1.0;
    float aLightPickingCdf[RT_MAX_SCENE_LIGHT];

    // render equation
    // Lo = Le + Int Li * fr * cos * vis
    
    // unreal gpu light mass use simplified material: lambert material
    // fr = albedo / pi = baseColor / pi
    // Lo = Le + Int Li * (baseColor / pi) * NoL

    //https://zhuanlan.zhihu.com/p/600466413
    // multi important sampling
    // fx = Li
    // gx = fr * cos
    // Lo = Int fx gx dx
    // xf = light sample xg = material sample

    // step1: Sample Light, Choose a [LIGHT] randomly
    // f(xf) * g(xf) * w (xf) / pdf(xf)
    // f(xf) / pdf(xf) = lightSample.radianceoverpdf
    // g(xf) = (baseColor / Pi) * NoL = materialEval.weight(basecolor) * materialEval.pdf(NoL / Pi)
    // w(xf) = pdf(xf) / ( pdf(xf) + pdf(xg) ) = mis(lightSample.m_pdf, materialEval.m_pdf)

    // step2: Sample Material, Choose a [LIGHT DIRECTION] based on material randomly
    // f(xg) * g(xg) * w (xg) / pdf(xg)
    // f(xg) = lightTraceResult.m_radiance
    // g(xg) = materialSample.weight = pathThroughput * materialSample.m_weight
    // pdf(xg) = CosineSampleHemisphere
    // pdf(xg) = lightPickPdf * lightTraceResult.m_pdf
    // w(xg) = pdf(xg) / ( pdf(xf) + pdf(xg) ) = mis(materialSample.m_pdf,lightPickPdf * lightTraceResult.m_pdf)

    // step3: Calculate Le in TraceLightRay function
    // for(uint index = 0; index < m_nRtSceneLightCount; index++)
    //  radiance += Le

    for(int bounce = 0; bounce <= maxBounces; bounce++)
    {
        const bool bIsCameraRay = bounce == 0;
        const bool bIsLastBounce = bounce == maxBounces;

        SMaterialClosestHitPayload rtRaylod = (SMaterialClosestHitPayload)0;
        if(bIsCameraRay)
        {
            rtRaylod.m_worldPosition = worldPosition;
            rtRaylod.m_worldNormal = faceNormal;
            rtRaylod.m_vHiTt = 1.0f;
            rtRaylod.m_eFlag |= RT_PAYLOAD_FLAG_FRONT_FACE;
            rtRaylod.m_roughness = 1.0f;
            rtRaylod.m_baseColor = float3(1,1,1);
            rtRaylod.m_diffuseColor = float3(1,1,1);
            rtRaylod.m_specColor = float3(0,0,0);
        }
        else
        {
            rtRaylod = TraceLightRay(ray,bIsLastBounce,pathThroughput,radiance);
        }

#if RT_DEBUG_OUTPUT
        if(bounce == 1)
        {
            rtDebugOutput = float4(radiance,1.0);
        }
#endif 

        if(rtRaylod.m_vHiTt < 0.0)// missing
        {
            break;
        }

        if((rtRaylod.m_eFlag & RT_PAYLOAD_FLAG_FRONT_FACE) == 0)
        {
            bIsValidSample = false;
            return;
        }

        // x: light sample y: light direction sample z: light direction sample w: russian roulette
        float4 randomSample = GetRandomSampleFloat4(randomSequence);

        float vLightPickingCdfPreSum = 0.0;
        float3 worldPosition = rtRaylod.m_worldPosition;
        float3 worldNormal = rtRaylod.m_worldNormal;

        for(uint index = 0; index < m_nRtSceneLightCount; index++)
        {
            vLightPickingCdfPreSum += EstimateLight(index,worldPosition,worldNormal);
            aLightPickingCdf[index] = vLightPickingCdfPreSum;
        }

        // step1: Sample Light, Choose a [LIGHT] randomly
        if(debugSample != 1)
        {
            if (vLightPickingCdfPreSum > 0)
            {
                int nSelectedLightIndex = 0;
                float vSlectedLightPdf = 0.0; 

                SelectLight(randomSample.x * vLightPickingCdfPreSum,m_nRtSceneLightCount,aLightPickingCdf,nSelectedLightIndex,vSlectedLightPdf);
                vSlectedLightPdf /= vLightPickingCdfPreSum;

                SLightSample lightSample = SampleLight(nSelectedLightIndex,randomSample.yz,worldPosition,worldNormal);
                lightSample.m_radianceOverPdf /= vSlectedLightPdf;
                lightSample.m_pdf *= vSlectedLightPdf;

                if(lightSample.m_pdf > 0)
                {
                    // trace a visibility ray
                    {
                        SMaterialClosestHitPayload shadowRayPaylod = (SMaterialClosestHitPayload)0;
                        
                        RayDesc shadowRay;
                        shadowRay.Origin = worldPosition;
                        shadowRay.TMin = 0.0f;
                        shadowRay.Direction = lightSample.m_direction;
                        shadowRay.TMax = lightSample.m_distance;
                        shadowRay.Origin += abs(worldPosition) * 0.001f * worldNormal; // todo : betther bias calculation

                        TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_SHADOW_SHADER_INDEX, 1,0, shadowRay, shadowRayPaylod);

                        float sampleContribution = 0.0;
                        if(shadowRayPaylod.m_vHiTt <= 0)
                        {
                            sampleContribution = 1.0;
                        }

                        lightSample.m_radianceOverPdf *= sampleContribution;
                    }

                    if (any(lightSample.m_radianceOverPdf > 0))
                    {   
                        SMaterialEval materialEval = EvalMaterial(lightSample.m_direction,rtRaylod);
                        
                        float3 lightContrib = pathThroughput * lightSample.m_radianceOverPdf * materialEval.m_weight * materialEval.m_pdf;
                        lightContrib *= MISWeightRobust(lightSample.m_pdf,materialEval.m_pdf);
                        
                        if (bounce > 0)
                        {
                            radiance += lightContrib;
                        }
                        else
                        {
                            if(rtSceneLights[nSelectedLightIndex].m_isStationary == 0)
                            {
                                directionalLightRadianceValue += lightContrib;
							    directionalLightRadianceDirection = lightSample.m_direction;
                            }
                        }
                    }

                }
            }
        }

        // step2: Sample Material, Choose a [LIGHT DIRECTION] based on material randomly
        if(debugSample != 2)
        {
            SMaterialSample materialSample = SampleMaterial(rtRaylod,randomSample);
            if(materialSample.m_pdf < 0.0 || asuint(materialSample.m_pdf) > 0x7F800000)
            {
                break;
            }

            float3 nextPathThroughput = pathThroughput * materialSample.m_weight;
            if(!any(nextPathThroughput > 0))
            {
                break;
            }

            float maxNextPathThroughput = max(max(nextPathThroughput.x, nextPathThroughput.y),nextPathThroughput.z);
            float maxPathThroughput = max(max(pathThroughput.x, pathThroughput.y),pathThroughput.z);

            float continuationProbability = sqrt(saturate(maxNextPathThroughput / maxPathThroughput));
            if(continuationProbability < 1 && bounce != 0)
            {
                float russianRouletteRand = randomSample.w;
		    	if (russianRouletteRand >= continuationProbability)
		    	{
		    		break;
		    	}
		    	pathThroughput = nextPathThroughput / continuationProbability;
            }
            else
            {
                pathThroughput = nextPathThroughput;
            }

            ray.Origin = rtRaylod.m_worldPosition;
            ray.Direction = normalize(materialSample.m_direction);
            ray.TMin = 0.0f;
            ray.TMax = POSITIVE_INFINITY;

            ray.Origin += (abs(rtRaylod.m_worldPosition) + 0.5) * 0.001f * rtRaylod.m_worldNormal;

            if(bounce == 0)
            {
                radianceDirection = ray.Direction;
            }

            for(uint index = 0; index < m_nRtSceneLightCount; index++)
            {
                SLightTraceResult lightTraceResult = TraceLight(ray,index);

                if(lightTraceResult.m_hitT < 0.0)
                {
                    continue;
                }

                float3 lightContribution = pathThroughput * lightTraceResult.m_radiance;
 
                if(vLightPickingCdfPreSum > 0)
                {
                    float previousCdfValue = index > 0 ? aLightPickingCdf[index - 1] : 0.0;
		    		float lightPickPdf = (aLightPickingCdf[index] - previousCdfValue) / vLightPickingCdfPreSum;

                    lightContribution *= MISWeightRobust(materialSample.m_pdf,lightPickPdf * lightTraceResult.m_pdf);

                    if (any(lightContribution > 0))
                    {
                        SMaterialClosestHitPayload shadowRayPaylod = (SMaterialClosestHitPayload)0;
                        RayDesc shadowRay = ray;
                        shadowRay.TMax = lightTraceResult.m_hitT;

                        TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_SHADOW_SHADER_INDEX, 1, 0, shadowRay, shadowRayPaylod);

                        if(shadowRayPaylod.m_vHiTt > 0)
                        {
                            lightContribution = 0.0;
                        }

                        radiance += lightContribution;
                    }
                }
            }
        }
    }


    radianceValue = radiance;
}

[shader("raygeneration")]
void LightMapRayTracingRayGen()
{
    const uint2 rayIndex = DispatchRaysIndex().xy;

    float3 worldPosition = rtWorldPosition[rayIndex].xyz;
    float3 worldFaceNormal = rtWorldNormal[rayIndex].xyz;

    bool bIsValidSample = true;
    if(all(worldPosition < 0.001f) && all(worldFaceNormal < 0.001f))
    {
        bIsValidSample = false;
        return;
    }

    SRandomSequence randomSequence;
    randomSequence.m_randomSeed = 0;
    randomSequence.m_nSampleIndex = StrongIntegerHash(rayIndex.y * m_nAtlasSize.x + rayIndex.x) + rtRenderPassInfo.m_renderPassIndex;

    float3 radianceValue = 0; // unused currently
    float3 radianceDirection = 0;

    float3 directionalLightRadianceValue = 0;
    float3 directionalLightRadianceDirection = 0;
    
 #if RT_DEBUG_OUTPUT
    float4 rtDebugOutput = 0.0;
#endif

    DoRayTracing(
#if RT_DEBUG_OUTPUT
        rtDebugOutput,
#endif
        worldPosition,
        worldFaceNormal,
        randomSequence,
        bIsValidSample,

        radianceValue,
        radianceDirection,

        directionalLightRadianceValue,
        directionalLightRadianceDirection);

    if (any(isnan(radianceValue)) || any(radianceValue < 0) || any(isinf(radianceValue)))
	{
		bIsValidSample = false;
	}

    if (bIsValidSample)
    {
        {
            float TangentZ = saturate(dot(directionalLightRadianceDirection, worldFaceNormal)); //TODO: Shading Normal?
            if(TangentZ > 0.0)
            {
                shDirectionality[rayIndex].rgba += Luminance(directionalLightRadianceValue) * SHBasisFunction(directionalLightRadianceDirection);
            }
            irradianceAndValidSampleCount[rayIndex].rgb += directionalLightRadianceValue;
        }

        {
            float TangentZ = saturate(dot(radianceDirection, worldFaceNormal)); //TODO: Shading Normal?
            if(TangentZ > 0.0)
            {
                shDirectionality[rayIndex].rgba += Luminance(radianceValue) * SHBasisFunction(radianceDirection);
            }
            irradianceAndValidSampleCount[rayIndex].rgb += radianceValue;            
        }

    }   

    if (bIsValidSample)
    {
        irradianceAndValidSampleCount[rayIndex].w += 1.0;
    }

#if RT_DEBUG_OUTPUT
    encodedIrradianceAndSubLuma1[rayIndex] = rtDebugOutput;
#endif
}

[shader("closesthit")]
void MaterialClosestHitMain(inout SMaterialClosestHitPayload payload, in SRayTracingIntersectionAttributes attributes)
{
    const float3 barycentrics = float3(1.0 - attributes.x - attributes.y, attributes.x, attributes.y);
    SMeshInstanceGpuData meshInstanceGpuData = rtSceneInstanceGpuData[InstanceIndex()];

    const float4x4 worldTM = meshInstanceGpuData.m_worldTM;
    const uint ibStride = meshInstanceGpuData.ibStride;
    const uint ibIndex = meshInstanceGpuData.ibIndex;
    const uint vbIndex = meshInstanceGpuData.vbIndex;

    uint primitiveIndex = PrimitiveIndex();
    uint baseIndex = primitiveIndex * 3;

    uint3 indices;
    if(ibStride == 0) // dont have index buffer
    {
        indices = uint3(baseIndex, baseIndex + 1, baseIndex + 2);
    }
    else if(ibStride == 2) // 16 bit
    {
        // todo
    }
    else // 32 bit
    {
        // todo
    }

    // vertex stride = 32 bit * 3 = 24 byte
    float3 localVertex0 = asfloat(bindlessByteAddressBuffer[vbIndex].Load<float3>(indices.x * 12));
    float3 localVertex1 = asfloat(bindlessByteAddressBuffer[vbIndex].Load<float3>(indices.y * 12));
    float3 localVertex2 = asfloat(bindlessByteAddressBuffer[vbIndex].Load<float3>(indices.z * 12));

    float3 wolrdPosition0 =  mul(worldTM, float4(localVertex0,1.0)).xyz;
    float3 wolrdPosition1 =  mul(worldTM, float4(localVertex1,1.0)).xyz;
    float3 wolrdPosition2 =  mul(worldTM, float4(localVertex2,1.0)).xyz;

    float3 PA = wolrdPosition1 - wolrdPosition0;
	float3 PB = wolrdPosition2 - wolrdPosition0;
    float3 Unnormalized = cross(PB, PA);
	float InvWorldArea = rsqrt(dot(Unnormalized, Unnormalized));
    float3 FaceNormal = Unnormalized * InvWorldArea;

    float3 worldPosition = wolrdPosition0 * barycentrics.x + wolrdPosition1 * barycentrics.y + wolrdPosition2 * barycentrics.z;
    

    payload.m_roughness = 1.0;

    //New concrete albedo = 0.55
    payload.m_baseColor = float3(0.55,0.55,0.55);
    payload.m_specColor = float3(0,0,0);
    payload.m_diffuseColor = float3(0.55,0.55,0.55);
    payload.m_worldPosition = worldPosition;
    payload.m_worldNormal = FaceNormal;

#if RT_DEBUG_OUTPUT
    payload.m_debugPayload = float4(payload.m_worldNormal * 0.5 + 0.5 ,1.0);
#endif

    // isFronFace
    if (HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE)
    {
        payload.m_eFlag = RT_PAYLOAD_FLAG_FRONT_FACE;
    }
    else
    {
        payload.m_eFlag = 0;
    }

    //hit t
    payload.m_vHiTt = RayTCurrent();;
}

[shader("closesthit")]
void ShadowClosestHitMain(inout SMaterialClosestHitPayload payload, in SRayTracingIntersectionAttributes attributes)
{
    payload.m_vHiTt = RayTCurrent();;
}

[shader("miss")]
void RayMiassMain(inout SMaterialClosestHitPayload payload)
{
    payload.m_vHiTt = -1.0;
}
#endif


/***************************************************************************
*   LightMap Denoise Pass
***************************************************************************/

struct SDenoiseGeometryApp2VS
{
	float3 position    : TEXCOORD0;
	float2 textureCoord : TEXCOORD1;
};

struct SDenoiseGeometryVS2PS
{
  float4 position : SV_POSITION;
  float2 textureCoord :TEXCOORD0;
};

SDenoiseGeometryVS2PS DenoiseLightMapVS(SDenoiseGeometryApp2VS IN )
{
    SDenoiseGeometryVS2PS denoiseGeoVS2PS = (SDenoiseGeometryVS2PS)0;
    denoiseGeoVS2PS.position = float4(IN.position.xy,1.0,1.0);
    denoiseGeoVS2PS.textureCoord = IN.textureCoord;
    return denoiseGeoVS2PS;
}

struct SDenoiseOutputs
{
    float4 irradianceAndSampleCount :SV_Target0;
    float4 shDirectionality :SV_Target1;
};

struct SDenoiseParams
{
    float m_spatialBandWidth;
    float m_resultBandWidth;
    float m_normalBandWidth;
    float m_filterStrength;

    float4 inputTexSizeAndInvSize;

    float denoiseCBPadding[56];
};
ConstantBuffer<SDenoiseParams> denoiseParamsBuffer      : register(b0);

Texture2D<float4> denoiseInputIrradianceTexture         : register(t0);
Texture2D<float4> denoiseInputSHDirectionalityTexture   : register(t1);
Texture2D<float4> denoiseInputNormalTexture             : register(t2);

//Joint Non-local means (JNLM) denoiser.
//Based on Godot and YoctoImageDenoiser's JNLM implementation
//https://github.com/ManuelPrandini/YoctoImageDenoiser/blob/06e19489dd64e47792acffde536393802ba48607/libs/yocto_extension/yocto_extension.cpp#L207
//https://benedikt-bitterli.me/nfor/nfor.pdf

// MIT License
//
// Copyright (c) 2020 ManuelPrandini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


float4 DenoiseLightMap(Texture2D<float4> inputTexture,float2 texUV)
{
    const int HALF_PATCH_WINDOW = 4;
    const int HALF_SEARCH_WINDOW = 10;

    const float SIGMA_SPATIAL = denoiseParamsBuffer.m_spatialBandWidth;
	const float SIGMA_LIGHT = denoiseParamsBuffer.m_resultBandWidth;
	const float SIGMA_NORMAL = denoiseParamsBuffer.m_normalBandWidth;
	const float FILTER_VALUE = denoiseParamsBuffer.m_filterStrength * SIGMA_LIGHT;

    const int PATCH_WINDOW_DIMENSION = (HALF_PATCH_WINDOW * 2 + 1);
	const int PATCH_WINDOW_DIMENSION_SQUARE = (PATCH_WINDOW_DIMENSION * PATCH_WINDOW_DIMENSION);
	const float TWO_SIGMA_SPATIAL_SQUARE = 2.0f * SIGMA_SPATIAL * SIGMA_SPATIAL;
	const float TWO_SIGMA_LIGHT_SQUARE = 2.0f * SIGMA_LIGHT * SIGMA_LIGHT;
	const float TWO_SIGMA_NORMAL_SQUARE = 2.0f * SIGMA_NORMAL * SIGMA_NORMAL;
	const float FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE = FILTER_VALUE * FILTER_VALUE * TWO_SIGMA_LIGHT_SQUARE;
	const float EPSILON = 1e-6f;

    float3 denoisedRGB = float3(0,0,0);
    float4 inputValue = inputTexture.SampleLevel(gSamPointWarp, texUV , 0.0).xyzw;
    float3 inputColor = inputValue.rgb;
    float3 inputNormal = denoiseInputNormalTexture.SampleLevel(gSamPointWarp, texUV ,0.0).xyz;
    if (length(inputNormal) > EPSILON)
    {
        float sumWeights = 0.0f;
        float3 inputRGB = inputColor.rgb;

        for (int searchY = -HALF_SEARCH_WINDOW; searchY <= HALF_SEARCH_WINDOW; searchY++) 
        {
			for (int searchX = -HALF_SEARCH_WINDOW; searchX <= HALF_SEARCH_WINDOW; searchX++) 
            {
                float2 searchUV = texUV + float2(searchX,searchY) * denoiseParamsBuffer.inputTexSizeAndInvSize.zw;
                // TODO: point or linear sampler?
                float3 searchRGB = inputTexture.SampleLevel(gSamPointWarp, searchUV, 0.0).xyz;
                float3 searchNormal = denoiseInputNormalTexture.SampleLevel(gSamPointWarp, searchUV, 0.0).xyz;

                float patchSquareDist = 0.0f;
				for (int offsetY = -HALF_PATCH_WINDOW; offsetY <= HALF_PATCH_WINDOW; offsetY++) 
                {
					for (int offsetX = -HALF_PATCH_WINDOW; offsetX <= HALF_PATCH_WINDOW; offsetX++) 
                    {
                        float2 offsetInputUV = texUV + float2(offsetX,offsetY) * denoiseParamsBuffer.inputTexSizeAndInvSize.zw;
                        float2 offsetSearchUV = searchUV + float2(offsetX,offsetY) * denoiseParamsBuffer.inputTexSizeAndInvSize.zw;

                        float3 offsetInputRGB = inputTexture.SampleLevel(gSamPointWarp, offsetInputUV, 0.0).xyz;
                        float3 offsetSearchRGB = inputTexture.SampleLevel(gSamPointWarp, offsetSearchUV, 0.0).xyz;
                        float3 offsetDeltaRGB = offsetInputRGB - offsetSearchRGB;
                        patchSquareDist += dot(offsetDeltaRGB, offsetDeltaRGB) - TWO_SIGMA_LIGHT_SQUARE;
					}
				}

				patchSquareDist = max(0.0f, patchSquareDist / (3.0f * PATCH_WINDOW_DIMENSION_SQUARE));

				float weight = 1.0f;

                if(searchUV.x > 1.0 || searchUV.y > 1.0 || searchUV.x < 0.0 || searchUV.x < 0.0)
                {
                    weight = 0.0;
                }

                if(length(searchNormal) < EPSILON)
                {
                    weight = 0.0;
                }

				float2 pixelDelta = float2(searchX, searchY);
				float pixelSquareDist = dot(pixelDelta, pixelDelta);
				weight *= exp(-pixelSquareDist / TWO_SIGMA_SPATIAL_SQUARE);
				weight *= exp(-pixelSquareDist / FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE);

				float3 normalDelta = inputNormal - searchNormal;
				float normalSquareDist = dot(normalDelta, normalDelta);
				weight *= exp(-normalSquareDist / TWO_SIGMA_NORMAL_SQUARE);

				denoisedRGB += weight * searchRGB;
				sumWeights += weight;
			}
		}
		denoisedRGB /= sumWeights;
    }
    else
    {
        denoisedRGB = inputColor;
    }
    return float4(denoisedRGB,inputValue.w);
}

SDenoiseOutputs DenoiseLightMapPS(SDenoiseGeometryVS2PS IN )
{
    SDenoiseOutputs output;
    output.irradianceAndSampleCount = DenoiseLightMap(denoiseInputIrradianceTexture,IN.textureCoord);
    output.shDirectionality = DenoiseLightMap(denoiseInputSHDirectionalityTexture,IN.textureCoord);
    return output;
}

/***************************************************************************
*   LightMap Dilate Pass
***************************************************************************/

struct SDilateGeometryApp2VS
{
	float3 position    : TEXCOORD0;
	float2 textureCoord : TEXCOORD1;
};

struct SDilateGeometryVS2PS
{
  float4 position : SV_POSITION;
  float2 textureCoord :TEXCOORD0;
};

SDilateGeometryVS2PS DilateLightMapVS(SDilateGeometryApp2VS IN )
{
    SDilateGeometryVS2PS dilateGeoVS2PS = (SDilateGeometryVS2PS)0;
    dilateGeoVS2PS.position = float4(IN.position.xy,1.0,1.0);
    dilateGeoVS2PS.textureCoord = IN.textureCoord;
    return dilateGeoVS2PS;
}

struct SDilateOutputs
{
    float4 irradianceAndSampleCount :SV_Target0;
    float4 shDirectionality :SV_Target1;
};

struct SDilateParams
{
    float m_spatialBandWidth;
    float m_resultBandWidth;
    float m_normalBandWidth;
    float m_filterStrength;

    float4 inputTexSizeAndInvSize;

    float denoiseCBPadding[56];
};
ConstantBuffer<SDilateParams> dilateParamsBuffer      : register(b0);

Texture2D<float4> dilateInputIrradianceTexture         : register(t0);
Texture2D<float4> dilateInputSHDirectionalityTexture   : register(t1);

bool IsNotEmptyPixel(float4 pixel)
{
    const float EPSILON = 1e-6f;
    return abs(pixel.x) > EPSILON && abs(pixel.y) > EPSILON && abs(pixel.z) > EPSILON && abs(pixel.w) > EPSILON;
}

float4 DilateLightMap(Texture2D<float4> inputTexture,float2 texUV)
{
    float4 centerValue = inputTexture.SampleLevel(gSamPointWarp, texUV , 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-1,+0) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+1,+0) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+0,-1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+0,+1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-1,-1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-1,+1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+1,-1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+1,+1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-2,+0) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,+0) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+0,-2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+0,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-2,-1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-2,+1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,-1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,+1) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-1,-2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-1,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+1,-2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+1,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(-2,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,-2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;
    centerValue = IsNotEmptyPixel(centerValue) ? centerValue : inputTexture.SampleLevel(gSamPointWarp, texUV + float2(+2,+2) * dilateParamsBuffer.inputTexSizeAndInvSize.zw, 0.0).xyzw;

    return centerValue;
}

SDilateOutputs DilateeLightMapPS(SDilateGeometryVS2PS IN )
{
    SDilateOutputs output;
    output.irradianceAndSampleCount = DilateLightMap(dilateInputIrradianceTexture,IN.textureCoord);
    output.shDirectionality = DilateLightMap(dilateInputSHDirectionalityTexture,IN.textureCoord);
    return output;
}

/***************************************************************************
*   LightMap Encoding Pass
***************************************************************************/

struct SEncodeGeometryApp2VS
{
	float3 position    : TEXCOORD0;
	float2 textureCoord : TEXCOORD1;
};

struct SEncodeGeometryVS2PS
{
  float4 position : SV_POSITION;
  float2 textureCoord :TEXCOORD0;
};

SEncodeGeometryVS2PS EncodeLightMapVS(SEncodeGeometryApp2VS IN )
{
    SEncodeGeometryVS2PS encodeGeoVS2PS = (SEncodeGeometryVS2PS)0;
    encodeGeoVS2PS.position = float4(IN.position.xy,1.0,1.0);
    encodeGeoVS2PS.textureCoord = IN.textureCoord;
    return encodeGeoVS2PS;
}

struct SEncodeOutputs
{
    float4 irradianceAndLuma :SV_Target0;
    float4 shDirectionalityAndLuma :SV_Target1;
};

struct SEncodeParams
{
    float m_spatialBandWidth;
    float m_resultBandWidth;
    float m_normalBandWidth;
    float m_filterStrength;

    float4 inputTexSizeAndInvSize;

    float encodeCBPadding[56];
};
ConstantBuffer<SEncodeParams> encodeParamsBuffer      : register(b0);

Texture2D<float4> encodeInputIrradianceTexture         : register(t0);
Texture2D<float4> encodeInputSHDirectionalityTexture   : register(t1);

SEncodeOutputs EncodeLightMapPS(SEncodeGeometryVS2PS IN )
{
    SEncodeOutputs output;

    float4 lightMap0 = encodeInputIrradianceTexture.SampleLevel(gSamPointWarp, IN.textureCoord, 0.0).xyzw;
    float4 lightMap1 = encodeInputSHDirectionalityTexture.SampleLevel(gSamPointWarp, IN.textureCoord, 0.0).xyzw;

    float sampleCount = lightMap0.w;
    if(sampleCount > 0)
    {
        float4 encodedSH = lightMap1.yzwx;
        float3 irradiance = lightMap0.xyz / sampleCount;

        const half logBlackPoint = 0.01858136;
        output.irradianceAndLuma = float4(sqrt(max(irradiance, float3(0.00001, 0.00001, 0.00001))), log2( 1 + logBlackPoint ) - (encodedSH.w / 255 - 0.5 / 255));
        output.shDirectionalityAndLuma = encodedSH;
    }
    else
    {
        output.irradianceAndLuma = float4(0,0,0,0);
        output.shDirectionalityAndLuma = float4(0,0,0,0);
    }

    return output;
}

/***************************************************************************
*   LightMap Visualize Pass
***************************************************************************/

struct SVisualizeGeometryApp2VS
{
	float3 posistion    : TEXCOORD0;
	float2 lightmapuv   : TEXCOORD1;
    float3 normal       : TEXCOORD2;
};

struct SVisualizeGeometryVS2PS
{
  float4 position : SV_POSITION;
  float2 lightMapUV :TEXCOORD0;
  float3 normal : TEXCOORD1;
};

Texture2D<float4> visResultIrradianceAndLuma: register(t0);
Texture2D<float4> visResultDirectionalityAndLuma: register(t1);

cbuffer CVisualizeGeomConstantBuffer : register(b0)
{
    float4x4 vis_worldTM;
    float4   vis_lightMapScaleAndBias;
    float vis_geoPadding[44];
};

cbuffer CVisualizeViewConstantBuffer : register(b1)
{
    float4x4 vis_vpMat;
    float vis_viewPadding[48];
};

struct SVisualizeGIResult
{
    float4 giResult :SV_Target0;
};

SVisualizeGeometryVS2PS VisualizeGIResultVS(SVisualizeGeometryApp2VS IN )
{
    SVisualizeGeometryVS2PS vs2PS = (SVisualizeGeometryVS2PS) 0;
    vs2PS.lightMapUV = IN.lightmapuv * vis_lightMapScaleAndBias.xy + vis_lightMapScaleAndBias.zw;
    float4 worldPosition = mul(vis_worldTM, float4(IN.posistion,1.0));
    vs2PS.position = mul(vis_vpMat, worldPosition);
    vs2PS.normal = IN.normal; // vis_worldTM dont have the rotation part in this example, just ouput to the pixel shader
    return vs2PS;
}

SVisualizeGIResult VisualizeGIResultPS(SVisualizeGeometryVS2PS IN)
{
    SVisualizeGIResult output;

    float4 lightmap0 = visResultIrradianceAndLuma.SampleLevel(gSamPointWarp, IN.lightMapUV, 0.0);
    float4 lightmap1 = visResultDirectionalityAndLuma.SampleLevel(gSamPointWarp, IN.lightMapUV, 0.0);

    // irradiance
    float3 irradiance = lightmap0.rgb * lightmap0.rgb;
    
    // luma
    float logL = lightmap0.w;
    logL += lightmap1.w * (1.0 / 255) - (0.5 / 255);
    const float logBlackPoint = 0.01858136;
	float luma = exp2( logL ) - logBlackPoint;

    // directionality
    float3 wordlNormal = IN.normal;
    float4 SH = lightmap1.xyzw;
    float Directionality = dot(SH,float4(wordlNormal.yzx,1.0));

    output.giResult = float4(irradiance * luma * Directionality, 1.0);
    return output;
}
