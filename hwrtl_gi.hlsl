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

RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[shader("raygeneration")]
void LightMapRayTracingRayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    gOutput[launchIndex.xy] = float4(1.0, 1.0, 0.0, 1.0);
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
    vs2PS.position = worldPosition;
    return vs2PS;
}

SVisualizeGIResult VisualizeGIResultPS(SVisualizeGeometryVS2PS IN)
{
    SVisualizeGIResult output;
    output.giResult = float4(IN.lightMapUV,1.0,1.0);
    return output;
}
