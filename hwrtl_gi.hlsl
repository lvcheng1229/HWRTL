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
    output.worldFaceNormal    = float4(deltaPosition,texelSize);
    return output;
}
