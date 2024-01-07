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

#include "hwrtl_gi.h"



namespace hwrtl
{
namespace gi
{
	struct SGIMesh
	{
		SResourceHandle m_hPositionBuffer;
		uint32_t vertexCount = 0;
	};

	class CGIBaker
	{
	public:
		std::vector<SGIMesh> m_giMeshes;
	};

	static CGIBaker* pGiBaker = nullptr;

	void hwrtl::gi::InitGIBaker()
	{
		Init();
		pGiBaker = new CGIBaker();
	}

	void hwrtl::gi::AddBakeMesh(const SBakeMeshDesc& bakeMeshDesc)
	{
		std::vector<SBakeMeshDesc> bakeMeshDescs;
		bakeMeshDescs.push_back(bakeMeshDesc);
		AddBakeMeshs(bakeMeshDescs);
	}

	void hwrtl::gi::AddBakeMeshs(const std::vector<SBakeMeshDesc>& bakeMeshDescs)
	{
		for (uint32_t index = 0; index < bakeMeshDescs.size(); index++)
		{
			const SBakeMeshDesc& bakeMeshDesc = bakeMeshDescs[index];
			SMeshInstancesDesc meshInstancesDesc;
			meshInstancesDesc.instanes.push_back(bakeMeshDesc.m_meshInstanceInfo);
			meshInstancesDesc.m_pPositionData = bakeMeshDesc.m_pPositionData;
			meshInstancesDesc.m_pUVData = bakeMeshDesc.m_pLightMapUVData;
			meshInstancesDesc.m_nVertexCount = bakeMeshDesc.m_nVertexCount;
			
			SGIMesh giMesh;
			giMesh.m_hPositionBuffer = CreateBuffer(bakeMeshDesc.m_pPositionData, bakeMeshDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB);
			giMesh.vertexCount = bakeMeshDesc.m_nVertexCount;
			pGiBaker->m_giMeshes.push_back(giMesh);

			AddRayTracingMeshInstances(meshInstancesDesc, giMesh.m_hPositionBuffer);
		}
	}

	void hwrtl::gi::PrePareLightMapGBufferPass()
	{
		std::size_t dirPos = String2Wstring(__FILE__).find(L"hwrtl_gi.cpp");
		std::wstring shaderPath = String2Wstring(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

		std::vector<SShader>rsShaders;
		rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"VSMain" });
		rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"PSMain" });

		SShaderResources rasterizationResources = { 0,0,1,0 };

		std::vector<EVertexFormat>vertexLayouts;
		vertexLayouts.push_back(EVertexFormat::FT_FLOAT3);
		vertexLayouts.push_back(EVertexFormat::FT_FLOAT2);

		CreateRSPipelineState(shaderPath, rsShaders, rasterizationResources, vertexLayouts);
	}

	void hwrtl::gi::GenerateLightMapGBuffer()
	{
		STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_SRV | ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA32_FLOAT,512,512 };
		SResourceHandle posTex = CreateTexture2D(texCreateDesc);
		SResourceHandle normTex = CreateTexture2D(texCreateDesc);
		SResourceHandle resouceHandles[2] = { posTex ,normTex };
		SetRenderTargets(resouceHandles, 2, -1, true, true);
		SetViewport(512, 512);
		SetVertexBuffers(&pGiBaker->m_giMeshes[0].m_hPositionBuffer,1);
		DrawInstanced(pGiBaker->m_giMeshes[0].vertexCount,1,0,0);
		SubmitCommandlist();
	}

	void hwrtl::gi::DeleteGIBaker()
	{
		delete pGiBaker;
	}
}
}