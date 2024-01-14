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

SamplerState gsamPointWarp : register(s0, space1000);
SamplerState gsamLinearWarp : register(s4, space1000);
SamplerState gsamLinearClamp : register(s5, space1000);

struct SGeometryApp2VS
{
	float3 posistion    : TEXCOORD0;
	float2 lightmapuv   : TEXCOORD1;
};

struct SGeometryVS2PS
{
  float4 position : SV_POSITION;
  float4 worldPosition :TEXCOORD0;
};

cbuffer CGeomConstantBuffer : register(b0)
{
    float4x4 worldTM;
    float4   lightMapScaleAndBias;
    float padding[44];
};

SGeometryVS2PS LightMapGBufferGenVS(SGeometryApp2VS IN )
{
    SGeometryVS2PS vs2PS = (SGeometryVS2PS) 0;

    float2 lightMapCoord = IN.lightmapuv * lightMapScaleAndBias.xy + lightMapScaleAndBias.zw;

    vs2PS.position = float4((lightMapCoord - float2(0.5,0.5)) * float2(2.0,-2.0),0.0,1.0);
    vs2PS.worldPosition = mul(worldTM, float4(IN.posistion,1.0));
    return vs2PS;
}

struct SLightMapGBufferOutput
{
    float4 worldPosition :SV_Target0;
    float4 worldFaceNormal :SV_Target1;
};

SLightMapGBufferOutput LightMapGBufferGenPS(SGeometryVS2PS IN)
{
    SLightMapGBufferOutput output;

    float3 faceNormal = normalize(cross(ddx(IN.worldPosition.xyz), ddy(IN.worldPosition.xyz)));
    float3 deltaPosition = max(abs(ddx(IN.worldPosition)), abs(ddy(IN.worldPosition)));
    float texelSize = max(deltaPosition.x, max(deltaPosition.y, deltaPosition.z));
    texelSize *= sqrt(2.0); 

    output.worldPosition      = IN.worldPosition;
    output.worldFaceNormal = float4(-faceNormal, texelSize);
    return output;
}

#define RAY_TRACING_MASK_OPAQUE				0x01

struct SRayTracingLight
{
    float3 color;
};


RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> rtWorldPosition : register(t1);
StructuredBuffer<SRayTracingLight> rtSceneLights : register(t2);

RWTexture2D<float4> rtOutput : register(u0);

struct SHitPayload
{
    bool bHit;
    float3 RetColor;
};

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

[shader("raygeneration")]
void LightMapRayTracingRayGen()
{
    const uint2 rayIndex = DispatchRaysIndex().xy;
    float3 worldPosition = rtWorldPosition[rayIndex].xyz;
    if (abs(worldPosition.x) < 0.0001 && abs(worldPosition.y) < 0.0001 && abs(worldPosition.z) < 0.0001)
    {
        rtOutput[rayIndex] = float4(0.0, 0.0, 1.0, 0.0);
        return;
    }
    
    RayDesc ray;
    ray.Origin = worldPosition;
    ray.Direction = normalize(float3(-1, -1, 1));
    ray.TMin = 0.01f;
    ray.TMax = 10000.0;
    
    SHitPayload payload = (SHitPayload) 0;
    TraceRay(
		rtScene, // AccelerationStructure
		RAY_FLAG_FORCE_OPAQUE,
		RAY_TRACING_MASK_OPAQUE, 
		0, // RayContributionToHitGroupIndex
		1, // MultiplierForGeometryContributionToShaderIndex
		0, // MissShaderIndex
		ray, // RayDesc
		payload // Payload
	);

    rtOutput[rayIndex] = float4(payload.RetColor, 0.0);
    rtOutput[rayIndex].y += rtSceneLights[0].color.x;

}

[shader("closesthit")]
void ClostHitMain(inout SHitPayload payload, in SRayTracingIntersectionAttributes attributes)
{
    payload.bHit = true;
    payload.RetColor = float3(0.5, 0.0, 0.0);
}

[shader("miss")]
void RayMiassMain(inout SHitPayload payload)
{
    payload.bHit = false;
    payload.RetColor = float3(1.0, 0.0, 0.0);
}

struct SVisualizeGeometryApp2VS
{
	float3 posistion    : TEXCOORD0;
	float2 lightmapuv   : TEXCOORD1;
};

struct SVisualizeGeometryVS2PS
{
  float4 position : SV_POSITION;
  float2 lightMapUV :TEXCOORD0;
};

Texture2D<float4> visLightMapResult: register(t0);

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
    return vs2PS;
}

SVisualizeGIResult VisualizeGIResultPS(SVisualizeGeometryVS2PS IN)
{
    SVisualizeGIResult output;
    float4 result = visLightMapResult.SampleLevel(gsamPointWarp, IN.lightMapUV, 0.0);
    output.giResult = float4(result.x, IN.lightMapUV, 1.0);
    return output;
}
