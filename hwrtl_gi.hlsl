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
    
    //
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

struct SRayTracingLight
{
    float3 m_color; // light power
    uint m_isStationary; // stationary or static light

    float3 m_lightDirectionl; // light direction
    uint m_eLightType; // spjere light / directional light

    float3 m_worldPosition; // spjere light world position
    float m_vAttenuation; // spjere light attenuation

    float m_radius; // spjere light radius
    float3 m_rtLightpadding;
};

cbuffer CRtGlobalConstantBuffer : register(b0)
{
    uint m_nRtSceneLightCount;
    uint2 m_nAtlasSize;
    float m_rtGlobalCbPadding[61];
};

RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> rtWorldPosition : register(t1);
Texture2D<float4> rtWorldNormal : register(t2);
StructuredBuffer<SRayTracingLight> rtSceneLights : register(t3);

RWTexture2D<float4> encodedIrradianceAndSubLuma1 : register(u0);
RWTexture2D<float4> shDirectionalityAndSubLuma2 : register(u1);

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

struct SMaterialClosestHitPayload
{
    float3 m_worldPosition;
    float3 m_worldNormal;

    float m_vHiTt;
    uint m_eFlag; //e.g. front face flag

    // eval material
    uint m_shadingModelID;
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
    lightSample.m_direction = normalize(rtSceneLights[nLightIndex].m_lightDirectionl);
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
*       BRDF define
***************************************************************************/
float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	
	return a2 / ( PI*d*d );
}

float Vis_SmithJoint(float a2, float NoV, float NoL) 
{
	float Vis_SmithV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
	float Vis_SmithL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

float3 F_Schlick( float3 SpecularColor, float VoH )
{
	float Fc = Pow5( 1 - VoH );	
	return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;
}

/***************************************************************************
*   LightMap Ray Tracing Pass:
*       Eval Material Brdf etc
***************************************************************************/

struct SMaterialEval
{
    float3 m_weight; 
};

float CalcLobeSelectionPdf(SMaterialClosestHitPayload payload)
{
	float3 diff = payload.m_diffuseColor;
	float3 spec = payload.m_specColor;

    float sumDiff = diff.x + diff.y + diff.z;
    float sumSpec = spec.x + spec.y + spec.z;
	return sumDiff / (sumDiff + sumSpec + 0.0001f);
}

SMaterialEval SampleDefaultLitMaterial(float3 incomingDirection,float3 outgoingDirection,SMaterialClosestHitPayload payload)
{
    SMaterialEval materialEval = (SMaterialEval)0;

    const float3 m_wolrdV = -incomingDirection;
    const float3 m_worldL = outgoingDirection;
    const float3 m_worldN = payload.m_worldNormal;

    float roughness = max(payload.m_roughness,0.01);
    float a2 = roughness * roughness;

    //TODO:?
    float3x3 Basis = GetTangentBasis( m_worldN );

    const float3 V = mul(Basis, m_wolrdV);
	const float3 L = mul(Basis, m_worldL);
	const float3 H = normalize(V + L);

    const float NoV = saturate(V.z);
    const float NoL = saturate(L.z);
    const float NoH = saturate(H.z);

    const float VoH = saturate(dot(V, H));

    const float diffLobePdf = CalcLobeSelectionPdf(payload);
    
    //weight * pdf * diff lobe pdf
    float3 diffWeight = payload.m_diffuseColor * (NoL / PI) * diffLobePdf;

    // fr = D * F * G / (4 * NoV * NoL)
    // weight = fr * cosine * (1- diff prob)
    float D = D_GGX(a2,NoH);
    float G = Vis_SmithJoint(a2,NoV,NoL);
    float3 F =  F_Schlick(payload.m_specColor, VoH);
    float3 specWeight = D * F * G / (4 * NoL * NoV) * (NoL) *(1.0 - diffLobePdf);

    //TODO: MIS?
    materialEval.m_weight = diffWeight + specWeight;
    return materialEval;
}

SMaterialEval SampleLambertMaterial(float3 outgoingDirection,SMaterialClosestHitPayload payload)
{
    SMaterialEval materialEval = (SMaterialEval)0;
    float3 N_World = payload.m_worldNormal;
	float NoL = saturate(dot(N_World, outgoingDirection));
    
    // weight = m_baseColor pdf = NoL / PI
    materialEval.m_weight = payload.m_baseColor * (NoL / PI);
    return materialEval;
}

SMaterialEval EvalMaterial(
	float3 incomingDirection,
	float3 outgoingDirection,
	SMaterialClosestHitPayload payload,
    bool bForceLambertMaterial)
{
    // eval lambert material for the first bounce
    if(bForceLambertMaterial)
    {
        return SampleLambertMaterial(outgoingDirection,payload);
    }

    switch(payload.m_shadingModelID)
    {
        case SHADING_MODEL_DEFAULT_LIT: return SampleDefaultLitMaterial(incomingDirection,outgoingDirection,payload);
        default:return SampleLambertMaterial(outgoingDirection,payload);
    }
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
    inout float3 directionalLightRadianceValue,
	inout float3 directionalLightRadianceDirection
)
{
    RayDesc ray;

    // path state variables
	float3 pathThroughput = 1.0;
	float pathRoughness = 0;

    float aLightPickingCdf[RT_MAX_SCENE_LIGHT];

    for(int bounce = 0; bounce < 1; bounce++)
    {
        const bool bIsCameraRay = bounce == 0;

        SMaterialClosestHitPayload rtRaylod = (SMaterialClosestHitPayload)0;
        if(bIsCameraRay)
        {
            rtRaylod.m_worldPosition = worldPosition;
            rtRaylod.m_worldNormal = faceNormal;
            rtRaylod.m_vHiTt = 1.0f;
            rtRaylod.m_eFlag |= RT_PAYLOAD_FLAG_FRONT_FACE;
            rtRaylod.m_shadingModelID = SHADING_MODEL_DEFAULT_LIT;
            rtRaylod.m_roughness = 1.0f;
            rtRaylod.m_baseColor = float3(1,1,1);
            rtRaylod.m_diffuseColor = float3(1,1,1);
            rtRaylod.m_specColor = float3(0,0,0);
        }
        else
        {

        }

        if(rtRaylod.m_vHiTt < 0.0)// missing
        {
            break;
        }

        if((rtRaylod.m_eFlag & RT_PAYLOAD_FLAG_FRONT_FACE) == 0)
        {
            bIsValidSample = false;
            return;
        }

        // x: light sample y:
        float4 randomSample = GetRandomSampleFloat4(randomSequence);

        // step1 : sample direct light
        float vLightPickingCdfPreSum = 0.0;
        {
            float3 worldPosition = rtRaylod.m_worldPosition;
            float3 worldNormal = rtRaylod.m_worldNormal;


            for(uint index = 0; index < m_nRtSceneLightCount; index++)
            {
                vLightPickingCdfPreSum += EstimateLight(index,worldPosition,worldNormal);
                aLightPickingCdf[index] = vLightPickingCdfPreSum;
            }



            if (vLightPickingCdfPreSum > 0)
            {
                int nSelectedLightIndex = 0;
                float vSlectedLightPdf = 0.0; 

                SelectLight(randomSample.x * vLightPickingCdfPreSum,m_nRtSceneLightCount,aLightPickingCdf,nSelectedLightIndex,vSlectedLightPdf);
                vSlectedLightPdf /= vLightPickingCdfPreSum;

#if RT_DEBUG_OUTPUT
                rtDebugOutput = float4(aLightPickingCdf[0],aLightPickingCdf[1],randomSample.x * vLightPickingCdfPreSum,1);
#endif
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
                        
                        // TODO: Apply A Bias, see codm?
                        shadowRay.Origin += abs(worldPosition) * 0.001f * worldNormal;

                        //TODO:
                        TraceRay(
	                    	rtScene, // AccelerationStructure
	                    	RAY_FLAG_FORCE_OPAQUE,
	                    	RAY_TRACING_MASK_OPAQUE, 
	                    	RT_SHADOW_SHADER_INDEX, // RayContributionToHitGroupIndex
	                    	1, // MultiplierForGeometryContributionToShaderIndex
	                    	0, // MissShaderIndex
	                    	shadowRay, // RayDesc
	                    	shadowRayPaylod // Payload
	                    );

                        float sampleContribution = 0.0;
                        if(shadowRayPaylod.m_vHiTt <= 0)
                        {
                            sampleContribution = 1.0;
                        }

                        lightSample.m_radianceOverPdf *= sampleContribution;
                    }

                    if (any(lightSample.m_radianceOverPdf > 0))
                    {   
                        SMaterialEval materialEval = EvalMaterial(ray.Direction, lightSample.m_direction, rtRaylod,bounce == 0);

                        //materialEval.m_weight = fr * cos
                        float3 lightContrib = pathThroughput * lightSample.m_radianceOverPdf * materialEval.m_weight;

                        if (bounce > 0)
                        {
                            
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

        // step2: sample indirect light
    }
}

[shader("raygeneration")]
void LightMapRayTracingRayGen()
{
    const uint2 rayIndex = DispatchRaysIndex().xy;
    shDirectionalityAndSubLuma2[rayIndex] = float4(0.0, 0.0, 1.0, 0.0);
    encodedIrradianceAndSubLuma1[rayIndex] = float4(0.0, 0.0, 1.0, 0.0);

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
    randomSequence.m_nSampleIndex = StrongIntegerHash(rayIndex.y * m_nAtlasSize.x + rayIndex.x);

    float3 radianceValue = 0; // unused currently

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
        directionalLightRadianceValue,
        directionalLightRadianceDirection);
    
    if (any(isnan(radianceValue)) || any(radianceValue < 0) || any(isinf(radianceValue)))
	{
		bIsValidSample = false;
	}

    float4 irradianceAndSampleCount = float4(0,0,0,0);
    float4 shDirectionality = float4(0,0,0,0);

    if (bIsValidSample)
    {
        float TangentZ = saturate(dot(directionalLightRadianceDirection, worldFaceNormal)); //TODO: Shading Normal?
        if(TangentZ > 0.0)
        {
            shDirectionality += Luminance(directionalLightRadianceValue) * SHBasisFunction(directionalLightRadianceDirection);
        }
        irradianceAndSampleCount.rgb += directionalLightRadianceValue;
    }   

    if (bIsValidSample)
    {
        irradianceAndSampleCount.w += 1.0;
        irradianceAndSampleCount.rgb /= irradianceAndSampleCount.w;

        float4 swizzedSH = shDirectionality.yzwx;
        shDirectionalityAndSubLuma2[rayIndex] = swizzedSH;

        const half LogBlackPoint = 0.01858136;
        encodedIrradianceAndSubLuma1[rayIndex] = float4(
            sqrt(max(irradianceAndSampleCount.rgb, float3(0.00001, 0.00001, 0.00001))),
            //log2( 1 + LogBlackPoint ) - (swizzedSH.w / 255 - 0.5 / 255)
            1.0 // currently use 128 bit light map for testing 
            );
    }

#if RT_DEBUG_OUTPUT
        encodedIrradianceAndSubLuma1[rayIndex] = rtDebugOutput;
#endif
}



[shader("closesthit")]
void MaterialClosestHitMain(inout SMaterialClosestHitPayload payload, in SRayTracingIntersectionAttributes attributes)
{
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
    payload.m_vHiTt = -1.0; //test
}
#endif

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

Texture2D<float4> visencodedIrradianceAndSubLuma1: register(t0);
Texture2D<float4> visshDirectionalityAndSubLuma2: register(t1);

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

    float4 lightmap0 = visencodedIrradianceAndSubLuma1.SampleLevel(gSamPointWarp, IN.lightMapUV, 0.0);
    float4 lightmap1 = visshDirectionalityAndSubLuma2.SampleLevel(gSamPointWarp, IN.lightMapUV, 0.0);
    lightmap1 = lightmap1;

    float3 irradiance = lightmap0.rgb * lightmap0.rgb;
    float luma = lightmap1.w;

    float3 wordlNormal = IN.normal;
    float4 SH = lightmap1.xyzw;
    float Directionality = dot(SH,float4(wordlNormal.yzx,1.0));

    output.giResult = float4(irradiance * luma * Directionality  + float3(IN.lightMapUV.xy , IN.lightMapUV.x + IN.lightMapUV.y) * 0.05f /*test code*/, 1.0);
    return output;
}
