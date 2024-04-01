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
#include "hwrtl_lodtex.h"

namespace hwrtl
{
namespace hlod
{
	struct SMeshInstanceGpuData
	{
		Matrix44 m_worldTM;

		uint32_t m_ibStride;
		uint32_t m_ibIndex;
		uint32_t m_vbIndex;
		uint32_t uvBufferIndex;

		uint32_t highPolyBaseColorIndex;
		uint32_t highPolyNormalIndex;
	};

	struct CHLODBakerContext
	{
		std::shared_ptr<CBuffer> m_pConstantBuffer;
		std::shared_ptr<CBuffer> m_pPositionVB;
		std::shared_ptr<CBuffer> m_pHLODUVVB;

		std::shared_ptr<CTexture2D>m_pResultBaseColorTexture2D;
		std::shared_ptr<CTexture2D>m_pResultNormalTexture2D;

		std::vector<std::shared_ptr<CTexture2D>> m_pInputBaseColoeTexture2Ds;
		std::vector<std::shared_ptr<CTexture2D>> m_pInputNormalTexture2Ds;
		std::vector<std::shared_ptr<CBuffer>> m_pInputTextureCoords;

		std::vector<std::shared_ptr<SGpuBlasData>> m_pGpuBlasDataArray;
		uint32_t m_nVertexCount;
	};

	struct SHLODGbufferGenPerGeoCB
	{
		Matrix44 m_worldTM;
		float padding[48];
	};

	class CHLODTextureBaker
	{
	public:
		void Init()
		{
			m_pDeviceCommand = CreateDeviceCommand();
			m_pRayTracingContext = CreateRayTracingContext();
			m_pGraphicsContext = CreateGraphicsContext();
		}

		std::shared_ptr<CGraphicsPipelineState>m_pHLODGBufferPSO;
		std::shared_ptr<CRayTracingPipelineState>m_pRayTracingPSO;

		CHLODBakerContext m_hlodBakerContext;

		static CDeviceCommand* GetDeviceCommand();
		static CRayTracingContext* GetRayTracingContext();
		static CGraphicsContext* GetGraphicsContext();

		SHLODConfig m_hlodConfig;

		std::shared_ptr<CTopLevelAccelerationStructure> m_pTLAS;
		std::vector<SMeshInstanceGpuData> m_sceneInstanceData;
		std::shared_ptr<CBuffer> m_instanceGpuData;

		std::shared_ptr<CTexture2D> m_pPosTexture;
		std::shared_ptr<CTexture2D> m_pNormalTexture;
	private:
		std::shared_ptr <CDeviceCommand> m_pDeviceCommand;
		std::shared_ptr<CRayTracingContext> m_pRayTracingContext;
		std::shared_ptr<CGraphicsContext>m_pGraphicsContext;
	};

	static CHLODTextureBaker* pHLODTextureBaker = nullptr;

	CDeviceCommand* CHLODTextureBaker::GetDeviceCommand()
	{
		return pHLODTextureBaker->m_pDeviceCommand.get();
	}

	CRayTracingContext* CHLODTextureBaker::GetRayTracingContext()
	{
		return pHLODTextureBaker->m_pRayTracingContext.get();
	}

	CGraphicsContext* CHLODTextureBaker::GetGraphicsContext()
	{
		return pHLODTextureBaker->m_pGraphicsContext.get();
	}

	static void InitHLODMeshGBufferPass()
	{
		std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_lodtex.cpp");
		std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_lodtex.hlsl";

		std::vector<SShader>rsShaders;
		rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"HLODGBufferGenVS" });
		rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"HLODGBufferGenPS" });

		SShaderResources rasterizationResources = { 0,0,1,0 };

		std::vector<EVertexFormat>vertexLayouts;
		vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);
		vertexLayouts.push_back(EVertexFormat::FT_FLOAT2);

		std::vector<ETexFormat>rtFormats;
		rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);
		rtFormats.push_back(ETexFormat::FT_RGBA32_FLOAT);

		SRasterizationPSOCreateDesc rsPsoCreateDesc = { shaderPath, rsShaders, rasterizationResources, vertexLayouts, rtFormats, ETexFormat::FT_None };
		pHLODTextureBaker->m_pHLODGBufferPSO = CHLODTextureBaker::GetDeviceCommand()->CreateRSPipelineState(rsPsoCreateDesc);

		STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_SRV | ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA32_FLOAT,pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.x,pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.y };
		pHLODTextureBaker->m_pPosTexture = CHLODTextureBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);
		pHLODTextureBaker->m_pNormalTexture = CHLODTextureBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);
	}

	static void InitHLODTextureRayTracingBakePass()
	{
		std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_lodtex.cpp");
		std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_lodtex.hlsl";

		std::vector<SShader>rtShaders;
		rtShaders.push_back(SShader{ ERayShaderType::RAY_RGS,L"HLODRayTracingRayGen" });
		rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS,L"HLODClosestHitMain" });
		rtShaders.push_back(SShader{ ERayShaderType::RAY_MIH,L"HLODRayMiassMain" });

		SRayTracingPSOCreateDesc rtPsoCreateDesc = { shaderPath, rtShaders, 1, SShaderResources{ 4,2,0,0,0,true,true} ,nullptr,0 };
		pHLODTextureBaker->m_pRayTracingPSO = CHLODTextureBaker::GetDeviceCommand()->CreateRTPipelineStateAndShaderTable(rtPsoCreateDesc);
	}

	std::shared_ptr<CTexture2D> hwrtl::hlod::CreateHighPolyTexture2D(STextureCreateDesc texCreateDesc)
	{
		return CHLODTextureBaker::GetDeviceCommand()->CreateTexture2D(texCreateDesc);
	}

	void InitHLODTextureBaker(SHLODConfig hlodConfig)
	{
		Init();
		pHLODTextureBaker = new CHLODTextureBaker();
		pHLODTextureBaker->m_hlodConfig = hlodConfig;
		pHLODTextureBaker->Init();

		InitHLODMeshGBufferPass();
		InitHLODTextureRayTracingBakePass();
	}

	void SetHLODMesh(const SHLODBakerMeshDesc& hlodMeshDesc)
	{
		pHLODTextureBaker->m_hlodBakerContext.m_pPositionVB = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(hlodMeshDesc.m_pPositionData, hlodMeshDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB);
		pHLODTextureBaker->m_hlodBakerContext.m_pHLODUVVB = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(hlodMeshDesc.m_pUVData, hlodMeshDesc.m_nVertexCount * sizeof(Vec2), sizeof(Vec2), EBufferUsage::USAGE_VB);

		SHLODGbufferGenPerGeoCB gBufferCbData;
		gBufferCbData.m_worldTM.SetIdentity();
		for (uint32_t i = 0; i < 4; i++)
		{
			for (uint32_t j = 0; j < 3; j++)
			{
				gBufferCbData.m_worldTM.m[i][j] = hlodMeshDesc.m_meshInstanceInfo.m_transform[j][i];
			}
		}

		for (uint32_t i = 0; i < 48; i++)
		{
			gBufferCbData.padding[i] = 1.0;
		}

		pHLODTextureBaker->m_hlodBakerContext.m_pConstantBuffer = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(&gBufferCbData, sizeof(SHLODGbufferGenPerGeoCB), sizeof(SHLODGbufferGenPerGeoCB), EBufferUsage::USAGE_CB);

		STextureCreateDesc resultTexCreateDesc{ ETexUsage::USAGE_SRV | ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA8_UNORM,pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.x, pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.y };
		pHLODTextureBaker->m_hlodBakerContext.m_pResultBaseColorTexture2D = CHLODTextureBaker::GetDeviceCommand()->CreateTexture2D(resultTexCreateDesc);
		pHLODTextureBaker->m_hlodBakerContext.m_pResultNormalTexture2D = CHLODTextureBaker::GetDeviceCommand()->CreateTexture2D(resultTexCreateDesc);
	}

	void SetHighPolyMesh(const std::vector<SHLODHighPolyMeshDesc>& highMeshDescs)
	{
		pHLODTextureBaker->m_hlodBakerContext.m_pGpuBlasDataArray.resize(highMeshDescs.size());
		pHLODTextureBaker->m_hlodBakerContext.m_pInputBaseColoeTexture2Ds.resize(highMeshDescs.size());
		pHLODTextureBaker->m_hlodBakerContext.m_pInputNormalTexture2Ds.resize(highMeshDescs.size());
		
		for (uint32_t index = 0; index < highMeshDescs.size(); index++)
		{
			const SHLODHighPolyMeshDesc& hlodBakerMeshDesc = highMeshDescs[index];
			std::shared_ptr<SGpuBlasData>& pGpuBlasData = pHLODTextureBaker->m_hlodBakerContext.m_pGpuBlasDataArray[index];

			pGpuBlasData = std::make_shared<SGpuBlasData>();
			pGpuBlasData->m_nVertexCount = hlodBakerMeshDesc.m_nVertexCount;
			pGpuBlasData->m_pVertexBuffer = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(hlodBakerMeshDesc.m_pPositionData, hlodBakerMeshDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB | EBufferUsage::USAGE_BYTE_ADDRESS);
			
			std::vector<SMeshInstanceInfo> instanceInfos;
			instanceInfos.push_back(hlodBakerMeshDesc.m_meshInstanceInfo);
			instanceInfos[0].m_instanceID = index;
			pGpuBlasData->instanes = instanceInfos;

			pHLODTextureBaker->m_hlodBakerContext.m_pInputBaseColoeTexture2Ds[index] = hlodBakerMeshDesc.m_pBaseColorTexture;
			pHLODTextureBaker->m_hlodBakerContext.m_pInputNormalTexture2Ds[index] = hlodBakerMeshDesc.m_pNormalTexture;
			pHLODTextureBaker->m_hlodBakerContext.m_pInputTextureCoords[index] = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(hlodBakerMeshDesc.m_pUVData, hlodBakerMeshDesc.m_nVertexCount * sizeof(Vec2), sizeof(Vec2), EBufferUsage::USAGE_VB | EBufferUsage::USAGE_BYTE_ADDRESS);;
		}

		CHLODTextureBaker::GetDeviceCommand()->BuildBottomLevelAccelerationStructure(pHLODTextureBaker->m_hlodBakerContext.m_pGpuBlasDataArray);
		std::vector< std::shared_ptr<SGpuBlasData>>& inoutGPUMeshData = pHLODTextureBaker->m_hlodBakerContext.m_pGpuBlasDataArray;
		{
			for (uint32_t indexMesh = 0; indexMesh < inoutGPUMeshData.size(); indexMesh++)
			{
				for (uint32_t indexInstance = 0; indexInstance < inoutGPUMeshData[indexMesh]->instanes.size(); indexInstance++)
				{
					auto& gpuMeshDataIns = inoutGPUMeshData[indexMesh];
					SMeshInstanceInfo& meshInstanceInfo = gpuMeshDataIns->instanes[indexInstance];
					meshInstanceInfo.m_instanceID = pHLODTextureBaker->m_sceneInstanceData.size();

					SMeshInstanceGpuData meshInstanceGpuData;
					{
						meshInstanceGpuData.m_worldTM.SetIdentity();
						for (uint32_t i = 0; i < 4; i++)
						{
							for (uint32_t j = 0; j < 3; j++)
							{
								meshInstanceGpuData.m_worldTM.m[i][j] = gpuMeshDataIns->instanes[indexInstance].m_transform[j][i];
							}
						}
						meshInstanceGpuData.m_ibStride = gpuMeshDataIns->m_nIndexStride;
						if (gpuMeshDataIns->m_nIndexStride > 0)
						{
							meshInstanceGpuData.m_ibIndex = gpuMeshDataIns->m_pIndexBuffer->GetOrAddByteAddressBindlessIndex();
						}
						else
						{
							meshInstanceGpuData.m_ibIndex = 0;
						}

						meshInstanceGpuData.m_vbIndex = gpuMeshDataIns->m_pVertexBuffer->GetOrAddByteAddressBindlessIndex();
						meshInstanceGpuData.uvBufferIndex = pHLODTextureBaker->m_hlodBakerContext.m_pInputTextureCoords[indexMesh]->GetOrAddByteAddressBindlessIndex();
						meshInstanceGpuData.highPolyBaseColorIndex = pHLODTextureBaker->m_hlodBakerContext.m_pInputBaseColoeTexture2Ds[indexMesh]->GetOrAddTexBindlessIndex();
						meshInstanceGpuData.highPolyNormalIndex = pHLODTextureBaker->m_hlodBakerContext.m_pInputNormalTexture2Ds[indexMesh]->GetOrAddTexBindlessIndex();
					}
					pHLODTextureBaker->m_sceneInstanceData.push_back(meshInstanceGpuData);
				}
			}
			pHLODTextureBaker->m_instanceGpuData = CHLODTextureBaker::GetDeviceCommand()->CreateBuffer(
				pHLODTextureBaker->m_sceneInstanceData.data(), sizeof(SMeshInstanceGpuData) * pHLODTextureBaker->m_sceneInstanceData.size(),
				sizeof(SMeshInstanceGpuData), EBufferUsage::USAGE_Structure);
		}
		pHLODTextureBaker->m_pTLAS = CHLODTextureBaker::GetDeviceCommand()->BuildTopAccelerationStructure(pHLODTextureBaker->m_hlodBakerContext.m_pGpuBlasDataArray);
	}

	void BakeHLODTexture()
	{
		// Generate hlod gbuffer
		{
			CHLODTextureBaker::GetGraphicsContext()->BeginRenderPasss();
			CHLODTextureBaker::GetGraphicsContext()->SetGraphicsPipelineState(pHLODTextureBaker->m_pHLODGBufferPSO);

			std::vector<std::shared_ptr<CTexture2D>> renderTargets;
			renderTargets.push_back(pHLODTextureBaker->m_pPosTexture);
			renderTargets.push_back(pHLODTextureBaker->m_pNormalTexture);

			const CHLODBakerContext& bakeContext = pHLODTextureBaker->m_hlodBakerContext;

			std::vector<std::shared_ptr<CBuffer>> vertexBuffers;
			vertexBuffers.push_back(bakeContext.m_pPositionVB);
			vertexBuffers.push_back(bakeContext.m_pHLODUVVB);

			CHLODTextureBaker::GetGraphicsContext()->SetRenderTargets(renderTargets, nullptr, true, true);
			CHLODTextureBaker::GetGraphicsContext()->SetViewport(pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.x, pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.y);
			CHLODTextureBaker::GetGraphicsContext()->SetConstantBuffer(bakeContext.m_pConstantBuffer, 0);
			CHLODTextureBaker::GetGraphicsContext()->SetVertexBuffers(vertexBuffers);
			CHLODTextureBaker::GetGraphicsContext()->DrawInstanced(bakeContext.m_nVertexCount, 1, 0, 0);
		}

		{
			CHLODTextureBaker::GetRayTracingContext()->BeginRayTacingPasss();
			CHLODTextureBaker::GetRayTracingContext()->SetRayTracingPipelineState(pHLODTextureBaker->m_pRayTracingPSO);
			CHLODTextureBaker::GetRayTracingContext()->SetShaderUAV(pHLODTextureBaker->m_hlodBakerContext.m_pResultBaseColorTexture2D, 0);
			CHLODTextureBaker::GetRayTracingContext()->SetShaderUAV(pHLODTextureBaker->m_hlodBakerContext.m_pResultNormalTexture2D, 1);
			CHLODTextureBaker::GetRayTracingContext()->SetTLAS(pHLODTextureBaker->m_pTLAS, 0);
			CHLODTextureBaker::GetRayTracingContext()->SetShaderSRV(pHLODTextureBaker->m_pPosTexture, 1);
			CHLODTextureBaker::GetRayTracingContext()->SetShaderSRV(pHLODTextureBaker->m_pNormalTexture, 2);
			CHLODTextureBaker::GetRayTracingContext()->SetShaderSRV(pHLODTextureBaker->m_instanceGpuData, 4);
			CHLODTextureBaker::GetRayTracingContext()->DispatchRayTracicing(pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.x, pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.y);
			CHLODTextureBaker::GetRayTracingContext()->EndRayTacingPasss();
		}
	}

	void GetHLODTextureData(SHLODTextureOutData& hlodTextureData)
	{
		uint32_t imageSize = pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.x * pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize.y * sizeof(uint8_t) * 4;
		hlodTextureData.destBaseColorOutputData = malloc(imageSize);
		hlodTextureData.destNormalOutputData = malloc(imageSize);

		void* lockedBaseColorData = CHLODTextureBaker::GetDeviceCommand()->LockTextureForRead(pHLODTextureBaker->m_hlodBakerContext.m_pResultBaseColorTexture2D);
		void* lockedNormalData = CHLODTextureBaker::GetDeviceCommand()->LockTextureForRead(pHLODTextureBaker->m_hlodBakerContext.m_pResultNormalTexture2D);

		if (imageSize != 0)
		{
			memcpy(hlodTextureData.destBaseColorOutputData, lockedBaseColorData, imageSize);
			memcpy(hlodTextureData.destNormalOutputData, lockedNormalData, imageSize);
		}

		CHLODTextureBaker::GetDeviceCommand()->UnLockTexture(pHLODTextureBaker->m_hlodBakerContext.m_pResultBaseColorTexture2D);
		CHLODTextureBaker::GetDeviceCommand()->UnLockTexture(pHLODTextureBaker->m_hlodBakerContext.m_pResultNormalTexture2D);

		hlodTextureData.m_texByteSize = imageSize;
		hlodTextureData.m_pixelStride = sizeof(uint8_t) * 4;
		hlodTextureData.m_texSize = pHLODTextureBaker->m_hlodConfig.m_nHLODTextureSize;
	}

	void DeleteHLODBaker()
	{
		delete pHLODTextureBaker;
		Shutdown();
	}

}
}


