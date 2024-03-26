struct SGeometryApp2VS
{
	float3 m_posistion    : TEXCOORD0;
	float2 m_hloduv       : TEXCOORD1;
};

struct SGeometryVS2PS
{
  float4 m_position : SV_POSITION;
  float4 m_worldPosition :TEXCOORD0;
};

cbuffer CGeomConstantBuffer : register(b0)
{
    float4x4 m_worldTM;
    float padding[48];
};

SGeometryVS2PS HLODGBufferGenVS(SGeometryApp2VS IN )
{
    SGeometryVS2PS vs2PS = (SGeometryVS2PS) 0;

    vs2PS.m_position = float4((IN.m_hloduv - float2(0.5,0.5)) * float2(2.0,-2.0),0.0,1.0);
    vs2PS.m_worldPosition = mul(m_worldTM, float4(IN.m_posistion,1.0));
    return vs2PS;
}

struct SHLODGBufferOutput
{
    float4 m_worldPosition :SV_Target0;
    float4 m_worldFaceNormal :SV_Target1;
};

SHLODGBufferOutput HLODGBufferGenPS(SGeometryVS2PS IN)
{
    SHLODGBufferOutput output;

    float3 faceNormal = normalize(cross(ddx(IN.m_worldPosition.xyz), ddy(IN.m_worldPosition.xyz)));
    output.m_worldPosition      = IN.m_worldPosition;
    output.m_worldFaceNormal = float4(-faceNormal, 1.0);
    return output;
}





struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

struct SHLODClosestHitPayload
{
    float3 m_worldPosition;
};

struct SMeshInstanceGpuData
{   
    float4x4 m_worldTM;
    uint ibStride;
    uint ibIndex;
    uint vbIndex;
    uint unused;
};

#define TEX_BUFFER_ARRAY_MAX_NUM 32

RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> rtWorldPosition : register(t1);
Texture2D<float4> rtWorldNormal : register(t2);

ByteAddressBuffer uvBufferArray[TEX_BUFFER_ARRAY_MAX_NUM] : register(t3);
Texture2D<float4> highPolyBaseColorTexArray[TEX_BUFFER_ARRAY_MAX_NUM] : register(t4);
Texture2D<float4> highPolyNormalTexArray[TEX_BUFFER_ARRAY_MAX_NUM] : register(t5);

RWTexture2D<float4> outputBaseColor : register(u0);
RWTexture2D<float4> outputNormal : register(u0);

StructuredBuffer<SMeshInstanceGpuData> rtSceneInstanceGpuData : register(t0, space2);

[shader("raygeneration")]
void HLODRayTracingRayGen()
{
    const uint2 rayIndex = DispatchRaysIndex().xy;

    float3 worldPosition = rtWorldPosition[rayIndex].xyz;
    float3 worldFaceNormal = rtWorldNormal[rayIndex].xyz;

    if(all(worldPosition < 0.001f) && all(worldFaceNormal < 0.001f))
    {
        return;
    }

    outputBaseColor[rayIndex].rgba = float4(0,0,0,0);
    outputNormal[rayIndex].rgba = float4(0,0,0,0);
}

[shader("closesthit")]
void HLODClosestHitMain(inout SHLODClosestHitPayload payload, in SRayTracingIntersectionAttributes attributes)
{
    const float3 barycentrics = float3(1.0 - attributes.x - attributes.y, attributes.x, attributes.y);
    SMeshInstanceGpuData meshInstanceGpuData = rtSceneInstanceGpuData[InstanceID()];

    const float4x4 worldTM = meshInstanceGpuData.m_worldTM;
    const uint ibStride = meshInstanceGpuData.ibStride;
    const uint ibIndex = meshInstanceGpuData.ibIndex;
    const uint vbIndex = meshInstanceGpuData.vbIndex;

    uint instanceID = InstanceID();


    float3 worldPosition = wolrdPosition0 * barycentrics.x + wolrdPosition1 * barycentrics.y + wolrdPosition2 * barycentrics.z;
}

[shader("miss")]
void HLODRayMiassMain(inout SHLODClosestHitPayload payload)
{
   
}