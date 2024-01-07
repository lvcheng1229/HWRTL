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
	void InitGIBaker()
	{
		Init();
	}

	void AddBakeMesh(const SBakeMeshDesc& bakeMeshDesc)
	{
		std::vector<SBakeMeshDesc> bakeMeshDescs;
		bakeMeshDescs.push_back(bakeMeshDesc);
		AddBakeMeshs(bakeMeshDescs);
	}

	void AddBakeMeshs(const std::vector<SBakeMeshDesc>& bakeMeshDescs)
	{
		for (uint32_t index = 0; index < bakeMeshDescs.size(); index++)
		{
			const SBakeMeshDesc& bakeMeshDesc = bakeMeshDescs[index];
			SMeshInstancesDesc meshInstancesDesc;
			meshInstancesDesc.instanes.push_back(bakeMeshDesc.m_meshInstanceInfo);
			meshInstancesDesc.m_pPositionData = bakeMeshDesc.m_pPositionData;
			meshInstancesDesc.m_pUVData = bakeMeshDesc.m_pLightMapUVData;
			meshInstancesDesc.m_nVertexCount = bakeMeshDesc.m_nVertexCount;
			AddRayTracingMeshInstances(meshInstancesDesc);
		}
	}

	void GenerateLightMapGBuffer()
	{
		STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_SRV | ETexUsage::USAGE_RTV,ETexFormat::FT_RGBA32_FLOAT,512,512 };
		SResourceHandle posTex = CreateTexture2D(texCreateDesc);
		SResourceHandle normTex = CreateTexture2D(texCreateDesc);

		std::size_t dirPos = String2Wstring(__FILE__).find(L"hwrtl_gi.cpp");
		std::wstring shaderPath = String2Wstring(__FILE__).substr(0, dirPos) + L"hwrtl_gi.hlsl";

		std::vector<SShader>rsShaders;
		rsShaders.push_back(SShader{ ERayShaderType::RS_VS,L"VSMain" });
		rsShaders.push_back(SShader{ ERayShaderType::RS_PS,L"PSMain" });

		SShaderResources rasterizationResources;
		CreateRSPipelineState(shaderPath, rsShaders, rasterizationResources);
	}
}
}