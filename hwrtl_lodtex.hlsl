SamplerState gSamPointWarp : register(s0, space1000);

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


#define RAY_TRACING_MASK_OPAQUE				0x01
#define RT_HLOAD_CHS_SHADER_INDEX            0
#define RT_MAX_HIT_T 1e30

struct SRayTracingIntersectionAttributes
{
    float x;
    float y;
};

struct SHLODClosestHitPayload
{
    float2 uv;
    uint baseColorIndex;
    uint normalIndex;
    float m_vHiTt;
};

struct SMeshInstanceGpuData
{   
    float4x4 m_worldTM;
    
    uint ibStride;
    uint ibIndex;
    uint vbIndex;
    uint uvBufferIndex;

    uint highPolyBaseColorIndex;
    uint highPolyNormalIndex;
};


RaytracingAccelerationStructure rtScene : register(t0);
Texture2D<float4> rtWorldPosition : register(t1);
Texture2D<float4> rtWorldNormal : register(t2);
StructuredBuffer<SMeshInstanceGpuData> rtSceneInstanceGpuData : register(t3);

ByteAddressBuffer bindlessByteAddressBuffer[] : BINDLESS_BYTE_ADDRESS_BUFFER_REGISTER;
Texture2D<float4> highPolyBindlessTexture[] : BINDLESS_TEXTURE_REGISTER;

RWTexture2D<float4> outputBaseColor : register(u0);
RWTexture2D<float4> outputNormal : register(u1);

[shader("raygeneration")]
void HLODRayTracingRayGen()
{
    const uint2 rayIndex = DispatchRaysIndex().xy;

    float3 worldPosition = rtWorldPosition[rayIndex].xyz;
    float3 worldFaceNormal = rtWorldNormal[rayIndex].xyz;

    if(all(abs(worldPosition) < 0.001f) && all(abs(worldFaceNormal) < 0.001f))
    {
        //outputBaseColor[rayIndex].rgba = float4(0,1,0,1);
        //outputNormal[rayIndex].rgba = float4(0,0,1,1);
        return;
    }

    SHLODClosestHitPayload hlodClosestFrontPayload = (SHLODClosestHitPayload)0;
    SHLODClosestHitPayload hlodClosestBackPayload = (SHLODClosestHitPayload)0;

    RayDesc forntRay;
    forntRay.Origin = worldPosition + normalize(worldFaceNormal);
    forntRay.Direction = normalize(worldFaceNormal);
    forntRay.TMin = 0.0f;
    forntRay.TMax = RT_MAX_HIT_T;
    TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_HLOAD_CHS_SHADER_INDEX, 1, 0, forntRay, hlodClosestFrontPayload);

    RayDesc backRay = forntRay;
    backRay.Direction = -forntRay.Direction;
    TraceRay(rtScene, RAY_FLAG_FORCE_OPAQUE, RAY_TRACING_MASK_OPAQUE, RT_HLOAD_CHS_SHADER_INDEX, 1, 0, backRay, hlodClosestBackPayload);

    float2 minHitUV = hlodClosestFrontPayload.uv;
    uint minHitBaseColorIndex = hlodClosestFrontPayload.baseColorIndex;
    uint minHitNormalIndex = hlodClosestFrontPayload.normalIndex;
    float minHitT = hlodClosestFrontPayload.m_vHiTt;

    if(hlodClosestBackPayload.m_vHiTt < hlodClosestFrontPayload.m_vHiTt)
    {
        minHitUV = hlodClosestBackPayload.uv;
        minHitBaseColorIndex = hlodClosestBackPayload.baseColorIndex;
        minHitNormalIndex = hlodClosestBackPayload.normalIndex;
        minHitT = hlodClosestBackPayload.m_vHiTt;
    }

    if(RT_MAX_HIT_T != minHitT)
    {
        outputBaseColor[rayIndex].rgba = highPolyBindlessTexture[minHitBaseColorIndex].SampleLevel(gSamPointWarp, minHitUV , 0.0).xyzw;
        outputNormal[rayIndex].rgba = highPolyBindlessTexture[minHitNormalIndex].SampleLevel(gSamPointWarp, minHitUV , 0.0).xyzw;
    }
    else
    {
        //outputBaseColor[rayIndex].rgba = float4(0,1,0,1);
        //outputNormal[rayIndex].rgba = float4(0,0,1,1);
    }
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
    const uint uvBufferIndex = meshInstanceGpuData.uvBufferIndex;
    const uint highPolyBaseColorIndex = meshInstanceGpuData.highPolyBaseColorIndex;
    const uint highPolyNormalIndex = meshInstanceGpuData.highPolyNormalIndex;

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

    float2 uv0 = asfloat(bindlessByteAddressBuffer[uvBufferIndex].Load<float2>(indices.x * 8));
    float2 uv1 = asfloat(bindlessByteAddressBuffer[uvBufferIndex].Load<float2>(indices.y * 8));
    float2 uv2 = asfloat(bindlessByteAddressBuffer[uvBufferIndex].Load<float2>(indices.z * 8));

    float2 uv = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;

    payload.uv = uv;
    payload.baseColorIndex = highPolyBaseColorIndex;
    payload.normalIndex = highPolyNormalIndex;
    payload.m_vHiTt = RayTCurrent();;
}

[shader("miss")]
void HLODRayMiassMain(inout SHLODClosestHitPayload payload)
{
   payload.m_vHiTt = RT_MAX_HIT_T;
}