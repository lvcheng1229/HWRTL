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

// DOCUMENTATION
// TODO:
//   1. light map gbuffer generation: jitter result
//   2. normal inverse transpose matrix
//

#include "hwrtl_gi.h"
#include <stdlib.h>
#include <assert.h>

#define STBRP_DEF static

/***************************************************************************
* https://github.com/nothings/stb/blob/master/stb_rect_pack.h
ALTERNATIVE A - MIT License
Copyright(c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and /or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***************************************************************************/

extern "C"
{
	typedef struct stbrp_context stbrp_context;
	typedef struct stbrp_node    stbrp_node;
	typedef struct stbrp_rect    stbrp_rect;

	typedef int            stbrp_coord;

#define STBRP__MAXVAL  0x7fffffff
	STBRP_DEF int stbrp_pack_rects(stbrp_context* context, stbrp_rect* rects, int num_rects);

	struct stbrp_rect
	{
		int            id;
		stbrp_coord    w, h;
		stbrp_coord    x, y;
		int            was_packed;  // non-zero if valid packing
	}; // 16 bytes, nominally

	STBRP_DEF void stbrp_init_target(stbrp_context* context, int width, int height, stbrp_node* nodes, int num_nodes);
	STBRP_DEF void stbrp_setup_allow_out_of_mem(stbrp_context* context, int allow_out_of_mem);
	STBRP_DEF void stbrp_setup_heuristic(stbrp_context* context, int heuristic);

	enum
	{
		STBRP_HEURISTIC_Skyline_default = 0,
		STBRP_HEURISTIC_Skyline_BL_sortHeight = STBRP_HEURISTIC_Skyline_default,
		STBRP_HEURISTIC_Skyline_BF_sortHeight
	};

	struct stbrp_node
	{
		stbrp_coord  x, y;
		stbrp_node* next;
	};

	struct stbrp_context
	{
		int width;
		int height;
		int align;
		int init_mode;
		int heuristic;
		int num_nodes;
		stbrp_node* active_head;
		stbrp_node* free_head;
		stbrp_node extra[2]; // we allocate two extra nodes so optimal user-node-count is 'width' not 'width+2'
	};
};

namespace hwrtl
{
namespace gi
{
	struct SGIMesh
	{
		std::shared_ptr<CBuffer> m_positionVB;
		std::shared_ptr<CBuffer> m_lightMapUVVB;
		std::shared_ptr<CBuffer> m_indexBuffer;

		std::shared_ptr<CBuffer> m_hConstantBuffer;
        
		std::shared_ptr<CBuffer> m_normalVB;
		std::shared_ptr<CBuffer> m_textureUVVB;

        std::shared_ptr<SGpuBlasData>m_pGpuMeshData;

        Vec2i m_nLightMapSize;
        Vec2i m_nAtlasOffset;
        Vec4 m_lightMapScaleAndBias;

        uint32_t m_nVertexCount = 0;
        uint32_t m_nIndexCount = 0;
        uint32_t m_nIndexStride = 0;

        int m_nAtlasIndex;

        SMeshInstanceInfo m_meshInstanceInfo;
	};

    struct SAtlas
    {
        std::vector<SGIMesh>m_atlasGeometries;

        // gbuffer output
        std::shared_ptr<CTexture2D> m_hPosTexture;
        std::shared_ptr<CTexture2D> m_hNormalTexture;

        // ray tracing output + dilate ouput
        std::shared_ptr<CTexture2D> m_irradianceAndSampleCount;
        std::shared_ptr<CTexture2D> m_shDirectionality;

        // denoiser output
        std::shared_ptr<CTexture2D> m_irradianceAndSampleCountPingPongTex;
        std::shared_ptr<CTexture2D> m_shDirectionalityPingPongTex;

        // encoded output
        std::shared_ptr<CTexture2D> m_irradianceAndSampleCountEncoded;
        std::shared_ptr<CTexture2D> m_shDirectionalityEncoded;
    };


    struct SRtRenderPassInfo
    {
        uint32_t m_rpIndex;
        uint32_t m_rpPadding0;
        uint32_t m_rpPadding1;
        uint32_t m_rpPadding2;
    };

    // must match the light type define in hlsl code
    enum class ELightType : uint32_t
    {
        LT_DIRECTION = 1 << 0,
        LT_SPHERE   = 1 << 1,
    };

    // must match the light define in hlsl code
    struct SRayTracingLight
    {
        Vec3 m_color;
        uint32_t m_isStationary;

        Vec3 m_lightDirectional;
        ELightType m_eLightType;

        Vec3 m_worldPosition;
        float m_vAttenuation;

        float m_radius;
        Vec3 m_rtLightPadding;
    };

    struct SRtGlobalConstantBuffer
    {
        uint32_t m_nRtSceneLightCount;
        Vec2i m_nAtlasSize;
        uint32_t m_sampleIndex;
        float m_rtGlobalCbPadding[60];
    };
    static_assert(sizeof(SRtGlobalConstantBuffer) == 256, "sizeof(SRtGlobalConstantBuffer) == 256");

    class CHWRTLLightMapDenoiser
    {
    public:
    };

	class CGIBaker
	{
	public:
		std::vector<SGIMesh> m_giMeshes;
        std::vector<SAtlas>m_atlas;

        SBakeConfig m_bakeConfig;
        Vec2i m_nAtlasSize;
        uint32_t m_nAtlasNum;

        std::shared_ptr<CBuffer> pRtSceneLight;
        std::shared_ptr<CBuffer> pRtSceneGlobalCB;
        std::shared_ptr<CBuffer> pVisualizeViewCB;

        std::shared_ptr<CGraphicsPipelineState>m_pLightMapGBufferPSO;
        std::shared_ptr<CRayTracingPipelineState>m_pRayTracingPSO;
        std::shared_ptr<CGraphicsPipelineState>m_pDenoisePSO;
        std::shared_ptr<CGraphicsPipelineState>m_pVisualizeGIPSO;

        std::shared_ptr<CTopLevelAccelerationStructure> m_pTLAS;

        std::vector<SRayTracingLight> m_aRayTracingLights;

        static CDeviceCommand* GetDeviceCommand();
        static CRayTracingContext* GetRayTracingContext();
        static CGraphicsContext* GetGraphicsContext();
        
        void Init() 
        {
            m_pDeviceCommand = CreateDeviceCommand();
            m_pRayTracingContext = CreateRayTracingContext();
            m_pGraphicsContext = CreateGraphicsContext();
        }
    private:
        std::shared_ptr <CDeviceCommand> m_pDeviceCommand;
        std::shared_ptr<CRayTracingContext> m_pRayTracingContext;
        std::shared_ptr<CGraphicsContext>m_pGraphicsContext;
	};

	static CGIBaker* pGiBaker = nullptr;

    CDeviceCommand* CGIBaker::GetDeviceCommand()
    {
        return pGiBaker->m_pDeviceCommand.get();
    }

    CRayTracingContext* CGIBaker::GetRayTracingContext()
    {
        return pGiBaker->m_pRayTracingContext.get();
    }

    CGraphicsContext* CGIBaker::GetGraphicsContext()
    {
        return pGiBaker->m_pGraphicsContext.get();
    }

    static void PackMeshIntoAtlas();

    static void GenerateAtlas()
    {
        pGiBaker->m_atlas.resize(pGiBaker->m_nAtlasNum);

        for (uint32_t index = 0; index < pGiBaker->m_giMeshes.size(); index++)
        {
            SGIMesh& giMeshDesc = pGiBaker->m_giMeshes[index];
            uint32_t atlasIndex = giMeshDesc.m_nAtlasIndex;
            pGiBaker->m_atlas[atlasIndex].m_atlasGeometries.push_back(giMeshDesc);
        }

        for (uint32_t index = 0; index < pGiBaker->m_atlas.size(); index++)
        {
            STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_SRV | ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA32_FLOAT,pGiBaker->m_nAtlasSize.x,pGiBaker->m_nAtlasSize.y };
            SAtlas& atlas = pGiBaker->m_atlas[index];
            atlas.m_hPosTexture = CGIBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);
            atlas.m_hNormalTexture = CGIBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);

            STextureCreateDesc resTexCreateDesc = texCreateDesc;
            resTexCreateDesc.m_eTexUsage = ETexUsage::USAGE_SRV | ETexUsage::USAGE_UAV;
            atlas.m_irradianceAndSampleCount = CGIBaker::GetDeviceCommand()->CreateTexture2D(resTexCreateDesc);
            atlas.m_shDirectionality = CGIBaker::GetDeviceCommand()->CreateTexture2D(resTexCreateDesc);
        }
    }

    static void PrePareGBufferPassPSO()
    {
        std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_gi.cpp");
        std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

        std::vector<SShader>rsShaders;
        rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"LightMapGBufferGenVS" });
        rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"LightMapGBufferGenPS" });

        SShaderResources rasterizationResources = { 0,0,1,0 };

        std::vector<EVertexFormat>vertexLayouts;
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT2);

        std::vector<ETexFormat>rtFormats;
        rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);
        rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);

        SRasterizationPSOCreateDesc rsPsoCreateDesc = { shaderPath, rsShaders, rasterizationResources, vertexLayouts, rtFormats, ETexFormat::FT_None };
        pGiBaker->m_pLightMapGBufferPSO = CGIBaker::GetDeviceCommand()->CreateRSPipelineState(rsPsoCreateDesc);
    }

    void hwrtl::gi::InitGIBaker(SBakeConfig bakeConfig)
    {
        Init();
        pGiBaker = new CGIBaker();
        pGiBaker->m_bakeConfig = bakeConfig;
        pGiBaker->Init();
    }

    void hwrtl::gi::AddBakeMesh(const SBakeMeshDesc& bakeMeshDesc)
    {
        std::vector<SBakeMeshDesc> bakeMeshDescs;
        bakeMeshDescs.push_back(bakeMeshDesc);
        AddBakeMeshsAndCreateVB(bakeMeshDescs);
    }

    static void ValidateMeshDesc(const SBakeMeshDesc& bakeMeshDesc)
    {
        assert(bakeMeshDesc.m_pPositionData != nullptr);
        assert(bakeMeshDesc.m_pLightMapUVData != nullptr);

        if (pGiBaker->m_bakeConfig.m_bAddVisualizePass)
        {
            assert((bakeMeshDesc.m_pNormalData != nullptr) && "gi baker visualize pass need normal vertex buffer");
        }
    }

    void hwrtl::gi::AddBakeMeshsAndCreateVB(const std::vector<SBakeMeshDesc>& bakeMeshDescs)
    {
        for (uint32_t index = 0; index < bakeMeshDescs.size(); index++)
        {
            const SBakeMeshDesc& bakeMeshDesc = bakeMeshDescs[index];
            SGIMesh giMesh;

            ValidateMeshDesc(bakeMeshDesc);

            giMesh.m_positionVB = CGIBaker::GetDeviceCommand()->CreateBuffer(bakeMeshDesc.m_pPositionData, bakeMeshDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB | EBufferUsage::USAGE_BYTE_ADDRESS);
            giMesh.m_lightMapUVVB = CGIBaker::GetDeviceCommand()->CreateBuffer(bakeMeshDesc.m_pLightMapUVData, bakeMeshDesc.m_nVertexCount * sizeof(Vec2), sizeof(Vec2), EBufferUsage::USAGE_VB | EBufferUsage::USAGE_BYTE_ADDRESS);

            if (bakeMeshDesc.m_pIndexData)
            {
                giMesh.m_indexBuffer = CGIBaker::GetDeviceCommand()->CreateBuffer(bakeMeshDesc.m_pIndexData, bakeMeshDesc.m_nIndexCount * sizeof(Vec3i), sizeof(Vec3i), EBufferUsage::USAGE_IB | EBufferUsage::USAGE_BYTE_ADDRESS);
            }

            if (bakeMeshDesc.m_pNormalData)
            {
                giMesh.m_normalVB = CGIBaker::GetDeviceCommand()->CreateBuffer(bakeMeshDesc.m_pNormalData, bakeMeshDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB | EBufferUsage::USAGE_BYTE_ADDRESS);
            }
           
            giMesh.m_nVertexCount = bakeMeshDesc.m_nVertexCount;
            giMesh.m_nLightMapSize = bakeMeshDesc.m_nLightMapSize;
            giMesh.m_meshInstanceInfo = bakeMeshDesc.m_meshInstanceInfo;
            giMesh.m_pGpuMeshData = std::make_shared<SGpuBlasData>();

            giMesh.m_pGpuMeshData->m_nVertexCount = giMesh.m_nVertexCount;
            giMesh.m_pGpuMeshData->m_pVertexBuffer = giMesh.m_positionVB;

            std::vector<SMeshInstanceInfo> instanceInfos;
            instanceInfos.push_back(giMesh.m_meshInstanceInfo);
            giMesh.m_pGpuMeshData->instanes = instanceInfos;
            pGiBaker->m_giMeshes.push_back(giMesh);
        }
    }

    void hwrtl::gi::AddDirectionalLight(Vec3 color, Vec3 direction, bool isStationary)
    {
        pGiBaker->m_aRayTracingLights.push_back(SRayTracingLight{ color ,isStationary ? 1u : 0u,NormalizeVec3(direction) ,ELightType::LT_DIRECTION });
    }

    void hwrtl::gi::AddSphereLight(Vec3 color, Vec3 worldPosition, bool isStationary, float attenuation, float radius)
    {
        pGiBaker->m_aRayTracingLights.push_back(SRayTracingLight{ color ,isStationary ? 1u : 0u,Vec3(0,0,0) ,ELightType::LT_SPHERE,worldPosition ,attenuation ,radius });
    }

	void hwrtl::gi::PrePareLightMapGBufferPass()
	{
        PrePareGBufferPassPSO();
        PackMeshIntoAtlas();
        GenerateAtlas();

        for (uint32_t index = 0; index < pGiBaker->m_atlas.size(); index++)
        {
            SAtlas& atlas = pGiBaker->m_atlas[index];
            for (uint32_t geoIndex = 0; geoIndex < atlas.m_atlasGeometries.size(); geoIndex++)
            {
                SGIMesh& giMesh = atlas.m_atlasGeometries[geoIndex];

                struct SGbufferGenPerGeoCB
                {
                    Matrix44 m_worldTM;
                    Vec4 lightMapScaleAndBias;
                    float padding[44];
                }gBufferCbData;
                static_assert(sizeof(SGbufferGenPerGeoCB) == 256,"sizeof(SGbufferGenPerGeoCB) == 256");

                gBufferCbData.m_worldTM.SetIdentity();

                for (uint32_t i = 0; i < 4; i++)
                {
                    for (uint32_t j = 0; j < 3; j++)
                    {
                        gBufferCbData.m_worldTM.m[i][j] = giMesh.m_meshInstanceInfo.m_transform[j][i];
                    }
                }

                for (uint32_t i = 0; i < 44; i++)
                {
                    gBufferCbData.padding[i] = 1.0;
                }

                gBufferCbData.lightMapScaleAndBias = giMesh.m_lightMapScaleAndBias;

                giMesh.m_hConstantBuffer = CGIBaker::GetDeviceCommand()->CreateBuffer(&gBufferCbData, sizeof(SGbufferGenPerGeoCB), sizeof(SGbufferGenPerGeoCB), EBufferUsage::USAGE_CB);
            }
        }
	}

	void hwrtl::gi::ExecuteLightMapGBufferPass()
	{
        CGIBaker::GetGraphicsContext()->BeginRenderPasss();
        CGIBaker::GetGraphicsContext()->SetGraphicsPipelineState(pGiBaker->m_pLightMapGBufferPSO);
        for (uint32_t index = 0; index < pGiBaker->m_atlas.size(); index++)
        {
            SAtlas& atlas = pGiBaker->m_atlas[index];

            std::shared_ptr<CTexture2D> posTex = atlas.m_hPosTexture;
            std::shared_ptr<CTexture2D> normTex = atlas.m_hNormalTexture;

            std::vector<std::shared_ptr<CTexture2D>> renderTargets;
            renderTargets.push_back(posTex);
            renderTargets.push_back(normTex);

            CGIBaker::GetGraphicsContext()->SetRenderTargets(renderTargets, nullptr, true, true);
            CGIBaker::GetGraphicsContext()->SetViewport(pGiBaker->m_nAtlasSize.x, pGiBaker->m_nAtlasSize.y);

            for (uint32_t geoIndex = 0; geoIndex < atlas.m_atlasGeometries.size(); geoIndex++)
            {
                SGIMesh& giMesh = atlas.m_atlasGeometries[geoIndex];

                std::vector<std::shared_ptr<CBuffer>> vertexBuffers;
                vertexBuffers.push_back(giMesh.m_positionVB);
                vertexBuffers.push_back(giMesh.m_lightMapUVVB);

                CGIBaker::GetGraphicsContext()->SetConstantBuffer(giMesh.m_hConstantBuffer, 0);
                CGIBaker::GetGraphicsContext()->SetVertexBuffers(vertexBuffers);

                CGIBaker::GetGraphicsContext()->DrawInstanced(giMesh.m_nVertexCount, 1, 0, 0);
            }
        }
        CGIBaker::GetGraphicsContext()->EndRenderPasss();
	}

    void hwrtl::gi::PrePareLightMapRayTracingPass()
    {
        CGIBaker::GetDeviceCommand()->OpenCmdList();

        std::vector<std::shared_ptr<SGpuBlasData>> inoutGpuBlasDataArray;

        for (uint32_t index = 0; index < pGiBaker->m_giMeshes.size(); index++)
        {
            auto& giMesh = pGiBaker->m_giMeshes[index];
            inoutGpuBlasDataArray.push_back(giMesh.m_pGpuMeshData);
        }

        CGIBaker::GetDeviceCommand()->BuildBottomLevelAccelerationStructure(inoutGpuBlasDataArray);
        pGiBaker->m_pTLAS = CGIBaker::GetDeviceCommand()->BuildTopAccelerationStructure(inoutGpuBlasDataArray);
        pGiBaker->pRtSceneLight = CGIBaker::GetDeviceCommand()->CreateBuffer(pGiBaker->m_aRayTracingLights.data(), sizeof(SRayTracingLight) * pGiBaker->m_aRayTracingLights.size(), sizeof(SRayTracingLight), EBufferUsage::USAGE_Structure);

        SRtGlobalConstantBuffer rtGloablCB;
        rtGloablCB.m_nRtSceneLightCount = pGiBaker->m_aRayTracingLights.size();
        rtGloablCB.m_nAtlasSize = pGiBaker->m_nAtlasSize;

        pGiBaker->pRtSceneGlobalCB = CGIBaker::GetDeviceCommand()->CreateBuffer(&rtGloablCB, sizeof(SRtGlobalConstantBuffer), sizeof(SRtGlobalConstantBuffer), EBufferUsage::USAGE_CB);

        std::vector<SShader>rtShaders;
        rtShaders.push_back(SShader{ ERayShaderType::RAY_RGS,L"LightMapRayTracingRayGen" });
        rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS,L"MaterialClosestHitMain" });
        rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS,L"ShadowClosestHitMain" });
        rtShaders.push_back(SShader{ ERayShaderType::RAY_MIH,L"RayMiassMain" });

        std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_gi.cpp");
        std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

        SShaderDefine shaderDefines;
        shaderDefines.m_defineName = std::wstring(L"RT_DEBUG_OUTPUT");
        if (pGiBaker->m_bakeConfig.m_bDebugRayTracing)
        {
            shaderDefines.m_defineValue = std::wstring(L"1");
        }
        else
        {
            shaderDefines.m_defineValue = std::wstring(L"0");
        }
        
        SRayTracingPSOCreateDesc rtPsoCreateDesc = { shaderPath, rtShaders, 1, SShaderResources{ 4,2,1,0 ,1} ,&shaderDefines,1 };
        pGiBaker->m_pRayTracingPSO = CGIBaker::GetDeviceCommand()->CreateRTPipelineStateAndShaderTable(rtPsoCreateDesc);

        CGIBaker::GetDeviceCommand()->CloseAndExecuteCmdList();
        CGIBaker::GetDeviceCommand()->WaitGPUCmdListFinish();
    }

    void hwrtl::gi::ExecuteLightMapRayTracingPass()
    {
        CGIBaker::GetRayTracingContext()->BeginRayTacingPasss();
        CGIBaker::GetRayTracingContext()->SetRayTracingPipelineState(pGiBaker->m_pRayTracingPSO);
        
        for (uint32_t index = 0; index < pGiBaker->m_atlas.size(); index++)
        {
            SAtlas& atlas = pGiBaker->m_atlas[index];

            CGIBaker::GetRayTracingContext()->SetConstantBuffer(pGiBaker->pRtSceneGlobalCB,0);
            CGIBaker::GetRayTracingContext()->SetShaderUAV(atlas.m_irradianceAndSampleCount, 0);
            CGIBaker::GetRayTracingContext()->SetShaderUAV(atlas.m_shDirectionality, 1);
            CGIBaker::GetRayTracingContext()->SetTLAS(pGiBaker->m_pTLAS,0);
            CGIBaker::GetRayTracingContext()->SetShaderSRV(atlas.m_hPosTexture, 1);
            CGIBaker::GetRayTracingContext()->SetShaderSRV(atlas.m_hNormalTexture, 2);
            CGIBaker::GetRayTracingContext()->SetShaderSRV(pGiBaker->pRtSceneLight, 3);
            for (uint32_t sampleIndex = 0; sampleIndex <= pGiBaker->m_bakeConfig.m_bakerSamples; sampleIndex++)
            {
                SRtRenderPassInfo rtRpInfo;
                rtRpInfo.m_rpIndex = sampleIndex;
                CGIBaker::GetRayTracingContext()->SetRootConstants(0, sizeof(SRtRenderPassInfo) / sizeof(uint32_t), &rtRpInfo, 0);
                CGIBaker::GetRayTracingContext()->DispatchRayTracicing(pGiBaker->m_nAtlasSize.x, pGiBaker->m_nAtlasSize.y);
            }
        }
        CGIBaker::GetRayTracingContext()->EndRayTacingPasss();
    }

    static void PrePareDenoiseLightMapPass()
    {
        CGIBaker::GetDeviceCommand()->OpenCmdList();

        std::vector<SShader>rsShaders;
        rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"DenoiseLightMapVS" });
        rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"DenoiseLightMapPS" });

        SShaderResources rasterizationResources = { 3,0,1,0 };

        std::vector<EVertexFormat>vertexLayouts;
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT2);

        std::vector<ETexFormat>rtFormats;
        rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);
        rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);

        std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_gi.cpp");
        std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

        SRasterizationPSOCreateDesc rsPsoCreateDesc = { shaderPath, rsShaders, rasterizationResources, vertexLayouts, rtFormats, ETexFormat::FT_None };
        pGiBaker->m_pDenoisePSO = CGIBaker::GetDeviceCommand()->CreateRSPipelineState(rsPsoCreateDesc);

        CGIBaker::GetDeviceCommand()->CloseAndExecuteCmdList();
        CGIBaker::GetDeviceCommand()->WaitGPUCmdListFinish();
    }

    void hwrtl::gi::DenoiseAndDilateLightMap()
    {
        PrePareDenoiseLightMapPass();
    }

    void hwrtl::gi::EncodeResulttLightMap()
    {

    }

    void hwrtl::gi::GetEncodedLightMapTexture(std::vector<SOutputAtlasInfo>& outputAtlas)
    {

    }

    void hwrtl::gi::PrePareVisualizeResultPass()
    {
        CGIBaker::GetDeviceCommand()->OpenCmdList();

        {
            Vec2i visualTex(1024, 1024);
            Vec3 eyePosition = Vec3(0, -12, 6);
            Vec3 eyeDirection = Vec3(0, 1, 0); // focus - eyePos
            Vec3 upDirection = Vec3(0, 0, 1);

            struct SViewCB
            {
                Matrix44 vpMat;
                float padding[48];
            }viewCB;
            static_assert(sizeof(SViewCB) == 256, "sizeof(SViewCB) == 256");

            float fovAngleY = 90;
            float aspectRatio = float(visualTex.y) / float(visualTex.x);
            viewCB.vpMat = GetViewProjectionMatrixRightHand(eyePosition, eyeDirection, upDirection, fovAngleY, aspectRatio, 0.1, 100.0).GetTrasnpose();
            pGiBaker->pVisualizeViewCB = CGIBaker::GetDeviceCommand()->CreateBuffer(&viewCB, sizeof(SViewCB), sizeof(SViewCB), EBufferUsage::USAGE_CB);
        }

        std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_gi.cpp");
        std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

        std::vector<SShader>rsShaders;
        rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"VisualizeGIResultVS" });
        rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"VisualizeGIResultPS" });

        SShaderResources rasterizationResources = { 2,0,2,0 };

        std::vector<EVertexFormat>vertexLayouts;
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT2);
        vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);

        std::vector<ETexFormat>rtFormats;
        rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);

        SRasterizationPSOCreateDesc rsPsoCreateDesc = { shaderPath, rsShaders, rasterizationResources, vertexLayouts, rtFormats, ETexFormat::FT_DepthStencil };
        pGiBaker->m_pVisualizeGIPSO = CGIBaker::GetDeviceCommand()->CreateRSPipelineState(rsPsoCreateDesc);

        CGIBaker::GetDeviceCommand()->CloseAndExecuteCmdList();
        CGIBaker::GetDeviceCommand()->WaitGPUCmdListFinish();
    }

    void hwrtl::gi::ExecuteVisualizeResultPass()
    {
        Vec2i visualTex(1024,1024);
        STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA32_FLOAT,visualTex.x,visualTex.y };
        STextureCreateDesc dsCreateDesc{ ETexUsage::USAGE_DSV,ETexFormat::FT_DepthStencil,visualTex.x,visualTex.y };
        std::shared_ptr<CTexture2D> renderTarget = CGIBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);
        std::shared_ptr<CTexture2D> dsTarget = CGIBaker::GetDeviceCommand()->CreateTexture2D(dsCreateDesc);
 
        CGIBaker::GetGraphicsContext()->BeginRenderPasss();
        CGIBaker::GetGraphicsContext()->SetGraphicsPipelineState(pGiBaker->m_pVisualizeGIPSO);

        std::vector<std::shared_ptr<CTexture2D>>renderTargets;
        renderTargets.push_back(renderTarget);
        CGIBaker::GetGraphicsContext()->SetRenderTargets(renderTargets, dsTarget);
        CGIBaker::GetGraphicsContext()->SetViewport(visualTex.x, visualTex.y);
        CGIBaker::GetGraphicsContext()->SetConstantBuffer(pGiBaker->pVisualizeViewCB,1);

        for (uint32_t index = 0; index < pGiBaker->m_atlas.size(); index++)
        {
            SAtlas& atlas = pGiBaker->m_atlas[index];
            CGIBaker::GetGraphicsContext()->SetShaderSRV(atlas.m_irradianceAndSampleCount, 0);
            CGIBaker::GetGraphicsContext()->SetShaderSRV(atlas.m_shDirectionality, 1);
            for (uint32_t geoIndex = 0; geoIndex < atlas.m_atlasGeometries.size(); geoIndex++)
            {
                SGIMesh& giMesh = atlas.m_atlasGeometries[geoIndex];
                CGIBaker::GetGraphicsContext()->SetConstantBuffer(giMesh.m_hConstantBuffer, 0);
                std::vector<std::shared_ptr<CBuffer>>vertexBuffers;
                vertexBuffers.push_back(giMesh.m_positionVB);
                vertexBuffers.push_back(giMesh.m_lightMapUVVB);
                vertexBuffers.push_back(giMesh.m_normalVB);
                CGIBaker::GetGraphicsContext()->SetVertexBuffers(vertexBuffers);
                CGIBaker::GetGraphicsContext()->DrawInstanced(giMesh.m_nVertexCount, 1, 0, 0);
            }
        }
        CGIBaker::GetGraphicsContext()->EndRenderPasss();
    }

	void hwrtl::gi::DeleteGIBaker()
	{
		delete pGiBaker;
        Shutdown();
	}

    /***************************************************************************
    * PackMeshIntoAtlas
    ***************************************************************************/

    static int NextPow2(int x)
    {
        return static_cast<int>(pow(2, static_cast<int>(ceil(log(x) / log(2)))));
    }

    static std::vector<Vec3i> PackRects(const std::vector<Vec2i> sourceLightMapSizes, const Vec2i targetLightMapSize)
    {
        std::vector<stbrp_node>stbrpNodes;
        stbrpNodes.resize(targetLightMapSize.x);
        memset(stbrpNodes.data(), 0, sizeof(stbrp_node) * stbrpNodes.size());

        stbrp_context context;
        stbrp_init_target(&context, targetLightMapSize.x, targetLightMapSize.y, stbrpNodes.data(), targetLightMapSize.x);

        std::vector<stbrp_rect> stbrpRects;
        stbrpRects.resize(sourceLightMapSizes.size());

        for (int i = 0; i < sourceLightMapSizes.size(); i++) {
            stbrpRects[i].id = i;
            stbrpRects[i].w = sourceLightMapSizes[i].x;
            stbrpRects[i].h = sourceLightMapSizes[i].y;
            stbrpRects[i].x = 0;
            stbrpRects[i].y = 0;
            stbrpRects[i].was_packed = 0;
        }

        stbrp_pack_rects(&context, stbrpRects.data(), stbrpRects.size());

        std::vector<Vec3i> result;
        for (int i = 0; i < sourceLightMapSizes.size(); i++)
        {
            result.push_back(Vec3i(stbrpRects[i].x, stbrpRects[i].y, stbrpRects[i].was_packed != 0 ? 1 : 0));
        }
        return result;
    }

    static void PackMeshIntoAtlas()
    {
        Vec2i nAtlasSize = pGiBaker->m_nAtlasSize;
        nAtlasSize = Vec2i(0, 0);

        std::vector<Vec2i> giMeshLightMapSize;
        for (uint32_t index = 0; index < pGiBaker->m_giMeshes.size(); index++)
        {
            SGIMesh& giMeshDesc = pGiBaker->m_giMeshes[index];
            giMeshLightMapSize.push_back(giMeshDesc.m_nLightMapSize);
            nAtlasSize.x = std::max(nAtlasSize.x, giMeshDesc.m_nLightMapSize.x + 2);
            nAtlasSize.y = std::max(nAtlasSize.y, giMeshDesc.m_nLightMapSize.y + 2);
        }

        int nextPow2 = NextPow2(nAtlasSize.x);
        nextPow2 = std::max(nextPow2, NextPow2(nAtlasSize.y));

        nAtlasSize = Vec2i(nextPow2, nextPow2);

        Vec2i bestAtlasSize;
        int bestAtlasSlices = 0;
        int bestAtlasArea = std::numeric_limits<int>::max();
        std::vector<Vec3i> bestAtlasOffsets;

        uint32_t maxAtlasSize = pGiBaker->m_bakeConfig.m_maxAtlasSize;
        while (nAtlasSize.x <= maxAtlasSize && nAtlasSize.y <= maxAtlasSize)
        {
            std::vector<Vec2i>remainLightMapSizes;
            std::vector<int>remainLightMapIndices;

            for (uint32_t index = 0; index < giMeshLightMapSize.size(); index++)
            {
                remainLightMapSizes.push_back(giMeshLightMapSize[index] + Vec2i(2, 2)); //padding
                remainLightMapIndices.push_back(index);
            }

            std::vector<Vec3i> lightMapOffsets;
            lightMapOffsets.resize(giMeshLightMapSize.size());

            int atlasIndex = 0;

            while (remainLightMapSizes.size() > 0)
            {
                std::vector<Vec3i> offsets = PackRects(remainLightMapSizes, nAtlasSize);

                std::vector<Vec2i>newRemainSizes;
                std::vector<int>newRemainIndices;

                for (int offsetIndex = 0; offsetIndex < offsets.size(); offsetIndex++)
                {
                    Vec3i subOffset = offsets[offsetIndex];
                    int lightMapIndex = remainLightMapIndices[offsetIndex];

                    if (subOffset.z > 0)
                    {
                        subOffset.z = atlasIndex;
                        lightMapOffsets[lightMapIndex] = subOffset + Vec3i(1, 1, 0);
                    }
                    else
                    {
                        newRemainSizes.push_back(remainLightMapSizes[offsetIndex]);
                        newRemainIndices.push_back(lightMapIndex);
                    }
                }

                remainLightMapSizes = newRemainSizes;
                remainLightMapIndices = newRemainIndices;
                atlasIndex++;
            }

            int totalArea = nAtlasSize.x * nAtlasSize.y * atlasIndex;
            if (totalArea < bestAtlasArea)
            {
                bestAtlasSize = nAtlasSize;
                bestAtlasOffsets = lightMapOffsets;
                bestAtlasSlices = atlasIndex;
                bestAtlasArea = totalArea;
            }

            if (nAtlasSize.x == nAtlasSize.y)
            {
                nAtlasSize.x *= 2;
            }
            else
            {
                nAtlasSize.y *= 2;
            }
        }

        pGiBaker->m_nAtlasSize = bestAtlasSize;

        for (uint32_t index = 0; index < pGiBaker->m_giMeshes.size(); index++)
        {
            SGIMesh& giMeshDesc = pGiBaker->m_giMeshes[index];
            giMeshDesc.m_nAtlasOffset = Vec2i(bestAtlasOffsets[index].x, bestAtlasOffsets[index].y);
            giMeshDesc.m_nAtlasIndex = bestAtlasOffsets[index].z;
            Vec2 scale = Vec2(giMeshDesc.m_nLightMapSize) / Vec2(bestAtlasSize);
            Vec2 bias = Vec2(giMeshDesc.m_nAtlasOffset) / Vec2(bestAtlasSize);
            Vec4 scaleAndBias = Vec4(scale.x, scale.y, bias.x, bias.y);
            giMeshDesc.m_lightMapScaleAndBias = scaleAndBias;
        }

        pGiBaker->m_nAtlasNum = bestAtlasSlices;
    }
}
}

/***************************************************************************
* https://github.com/nothings/stb/blob/master/stb_rect_pack.h
ALTERNATIVE A - MIT License
Copyright(c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and /or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***************************************************************************/

#define STBRP_SORT qsort
#define STBRP_ASSERT assert

#ifdef _MSC_VER
#define STBRP__NOTUSED(v)  (void)(v)
#define STBRP__CDECL       __cdecl
#else
#define STBRP__NOTUSED(v)  (void)sizeof(v)
#define STBRP__CDECL
#endif

enum
{
    STBRP__INIT_skyline = 1
};

STBRP_DEF void stbrp_setup_heuristic(stbrp_context* context, int heuristic)
{
    switch (context->init_mode) {
    case STBRP__INIT_skyline:
        STBRP_ASSERT(heuristic == STBRP_HEURISTIC_Skyline_BL_sortHeight || heuristic == STBRP_HEURISTIC_Skyline_BF_sortHeight);
        context->heuristic = heuristic;
        break;
    default:
        STBRP_ASSERT(0);
    }
}

STBRP_DEF void stbrp_setup_allow_out_of_mem(stbrp_context* context, int allow_out_of_mem)
{
    if (allow_out_of_mem)
        context->align = 1;
    else {
        context->align = (context->width + context->num_nodes - 1) / context->num_nodes;
    }
}

STBRP_DEF void stbrp_init_target(stbrp_context* context, int width, int height, stbrp_node* nodes, int num_nodes)
{
    int i;

    for (i = 0; i < num_nodes - 1; ++i)
        nodes[i].next = &nodes[i + 1];
    nodes[i].next = NULL;
    context->init_mode = STBRP__INIT_skyline;
    context->heuristic = STBRP_HEURISTIC_Skyline_default;
    context->free_head = &nodes[0];
    context->active_head = &context->extra[0];
    context->width = width;
    context->height = height;
    context->num_nodes = num_nodes;
    stbrp_setup_allow_out_of_mem(context, 0);

    context->extra[0].x = 0;
    context->extra[0].y = 0;
    context->extra[0].next = &context->extra[1];
    context->extra[1].x = (stbrp_coord)width;
    context->extra[1].y = (1 << 30);
    context->extra[1].next = NULL;
}

static int stbrp__skyline_find_min_y(stbrp_context* c, stbrp_node* first, int x0, int width, int* pwaste)
{
    stbrp_node* node = first;
    int x1 = x0 + width;
    int min_y, visited_width, waste_area;

    STBRP__NOTUSED(c);
    STBRP_ASSERT(first->x <= x0);
    STBRP_ASSERT(node->next->x > x0);
    STBRP_ASSERT(node->x <= x0);

    min_y = 0;
    waste_area = 0;
    visited_width = 0;
    while (node->x < x1) {
        if (node->y > min_y) {
            waste_area += visited_width * (node->y - min_y);
            min_y = node->y;
            if (node->x < x0)
                visited_width += node->next->x - x0;
            else
                visited_width += node->next->x - node->x;
        }
        else {
            int under_width = node->next->x - node->x;
            if (under_width + visited_width > width)
                under_width = width - visited_width;
            waste_area += under_width * (min_y - node->y);
            visited_width += under_width;
        }
        node = node->next;
    }

    *pwaste = waste_area;
    return min_y;
}

typedef struct
{
    int x, y;
    stbrp_node** prev_link;
} stbrp__findresult;

static stbrp__findresult stbrp__skyline_find_best_pos(stbrp_context* c, int width, int height)
{
    int best_waste = (1 << 30), best_x, best_y = (1 << 30);
    stbrp__findresult fr;
    stbrp_node** prev, * node, * tail, ** best = NULL;

    width = (width + c->align - 1);
    width -= width % c->align;
    STBRP_ASSERT(width % c->align == 0);

    if (width > c->width || height > c->height) {
        fr.prev_link = NULL;
        fr.x = fr.y = 0;
        return fr;
    }

    node = c->active_head;
    prev = &c->active_head;
    while (node->x + width <= c->width) {
        int y, waste;
        y = stbrp__skyline_find_min_y(c, node, node->x, width, &waste);
        if (c->heuristic == STBRP_HEURISTIC_Skyline_BL_sortHeight) {
            if (y < best_y) {
                best_y = y;
                best = prev;
            }
        }
        else {
            if (y + height <= c->height) {
                if (y < best_y || (y == best_y && waste < best_waste)) {
                    best_y = y;
                    best_waste = waste;
                    best = prev;
                }
            }
        }
        prev = &node->next;
        node = node->next;
    }

    best_x = (best == NULL) ? 0 : (*best)->x;

    if (c->heuristic == STBRP_HEURISTIC_Skyline_BF_sortHeight) {
        tail = c->active_head;
        node = c->active_head;
        prev = &c->active_head;
        while (tail->x < width)
            tail = tail->next;
        while (tail) {
            int xpos = tail->x - width;
            int y, waste;
            STBRP_ASSERT(xpos >= 0);
            while (node->next->x <= xpos) {
                prev = &node->next;
                node = node->next;
            }
            STBRP_ASSERT(node->next->x > xpos && node->x <= xpos);
            y = stbrp__skyline_find_min_y(c, node, xpos, width, &waste);
            if (y + height <= c->height) {
                if (y <= best_y) {
                    if (y < best_y || waste < best_waste || (waste == best_waste && xpos < best_x)) {
                        best_x = xpos;
                        STBRP_ASSERT(y <= best_y);
                        best_y = y;
                        best_waste = waste;
                        best = prev;
                    }
                }
            }
            tail = tail->next;
        }
    }

    fr.prev_link = best;
    fr.x = best_x;
    fr.y = best_y;
    return fr;
}

static stbrp__findresult stbrp__skyline_pack_rectangle(stbrp_context* context, int width, int height)
{
    stbrp__findresult res = stbrp__skyline_find_best_pos(context, width, height);
    stbrp_node* node, * cur;

    if (res.prev_link == NULL || res.y + height > context->height || context->free_head == NULL) {
        res.prev_link = NULL;
        return res;
    }

    node = context->free_head;
    node->x = (stbrp_coord)res.x;
    node->y = (stbrp_coord)(res.y + height);

    context->free_head = node->next;

    cur = *res.prev_link;
    if (cur->x < res.x) {
        stbrp_node* next = cur->next;
        cur->next = node;
        cur = next;
    }
    else {
        *res.prev_link = node;
    }

    while (cur->next && cur->next->x <= res.x + width) {
        stbrp_node* next = cur->next;
        cur->next = context->free_head;
        context->free_head = cur;
        cur = next;
    }

    node->next = cur;

    if (cur->x < res.x + width)
        cur->x = (stbrp_coord)(res.x + width);

    return res;
}

static int STBRP__CDECL rect_height_compare(const void* a, const void* b)
{
    const stbrp_rect* p = (const stbrp_rect*)a;
    const stbrp_rect* q = (const stbrp_rect*)b;
    if (p->h > q->h)
        return -1;
    if (p->h < q->h)
        return  1;
    return (p->w > q->w) ? -1 : (p->w < q->w);
}

static int STBRP__CDECL rect_original_order(const void* a, const void* b)
{
    const stbrp_rect* p = (const stbrp_rect*)a;
    const stbrp_rect* q = (const stbrp_rect*)b;
    return (p->was_packed < q->was_packed) ? -1 : (p->was_packed > q->was_packed);
}

STBRP_DEF int stbrp_pack_rects(stbrp_context* context, stbrp_rect* rects, int num_rects)
{
    int i, all_rects_packed = 1;

    for (i = 0; i < num_rects; ++i) {
        rects[i].was_packed = i;
    }

    STBRP_SORT(rects, num_rects, sizeof(rects[0]), rect_height_compare);

    for (i = 0; i < num_rects; ++i) {
        if (rects[i].w == 0 || rects[i].h == 0) {
            rects[i].x = rects[i].y = 0;  // empty rect needs no space
        }
        else {
            stbrp__findresult fr = stbrp__skyline_pack_rectangle(context, rects[i].w, rects[i].h);
            if (fr.prev_link) {
                rects[i].x = (stbrp_coord)fr.x;
                rects[i].y = (stbrp_coord)fr.y;
            }
            else {
                rects[i].x = rects[i].y = STBRP__MAXVAL;
            }
        }
    }

    STBRP_SORT(rects, num_rects, sizeof(rects[0]), rect_original_order);

    for (i = 0; i < num_rects; ++i) {
        rects[i].was_packed = !(rects[i].x == STBRP__MAXVAL && rects[i].y == STBRP__MAXVAL);
        if (!rects[i].was_packed)
            all_rects_packed = 0;
    }

    return all_rects_packed;
}
