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

#include "hwrtl_pvs.h"

namespace hwrtl
{
namespace pvs
{
	void InitPVSGenerator()
	{
		Init();
	}

	bool AddOccluder(const SOccluderDesc& occluderDesc, const std::vector<SMeshInstanceInfo>& meshInstanceInfo, std::vector<uint32_t>& instanceIndices)
	{
		return bool();
	}

	bool AddOccluderBound(const SOccluderBound& occluderBound, uint32_t instanceIndex)
	{
		SMeshInstanceInfo meshInstanceInfo;
		std::vector<SMeshInstanceInfo> meshInstanceInfos;
		std::vector<uint32_t>instanceIndices;

		meshInstanceInfos.push_back(meshInstanceInfo);
		instanceIndices.push_back(instanceIndex);
		return AddOccluderBounds(occluderBound, meshInstanceInfos, instanceIndices);
	}

	bool AddOccluderBounds(const SOccluderBound& occluderBound, const std::vector<SMeshInstanceInfo>& meshInstanceInfo, std::vector<uint32_t>& instanceIndices)
	{
		//const Vec3 boundMin = occluderBound.m_min;
		//const Vec3 boundMax = occluderBound.m_max;
		//Vec3 vertices[12 * 3];
		//
		////top triangle 1
		//vertices[0] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		//vertices[1] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		//vertices[2] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		//
		////top triangle 2
		//vertices[3] = Vec3(boundMin.x, boundMax.y, boundMax.z); 
		//vertices[4] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		//vertices[5] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		//
		////front triangle 1
		//vertices[6] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		//vertices[7] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		//vertices[8] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		//
		////front triangle 2
		//vertices[9] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		//vertices[10] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		//vertices[11] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		//
		////back triangle 1
		//vertices[12] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		//vertices[13] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		//vertices[14] = Vec3(boundMin.x, boundMax.y, boundMin.z);
		//
		////back triangle 2
		//vertices[15] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		//vertices[16] = Vec3(boundMin.x, boundMax.y, boundMin.z);
		//vertices[17] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		//
		////bottom triangle 1
		//vertices[18] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		//vertices[19] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		//vertices[20] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		//
		////bottom triangle 2
		//vertices[21] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		//vertices[22] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		//vertices[23] = Vec3(boundMin.x, boundMax.y, boundMin.z);
		//
		////right triangle 1
		//vertices[24] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		//vertices[25] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		//vertices[26] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		//
		////right triangle 2
		//vertices[27] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		//vertices[28] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		//vertices[29] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		//
		////left triangle 1
		//vertices[30] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		//vertices[31] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		//vertices[32] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		//
		////left triangle 2
		//vertices[32] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		//vertices[33] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		//vertices[34] = Vec3(boundMin.x, boundMax.y, boundMin.z);
		//
		//SCPUMeshData meshInstancesDesc;
		//meshInstancesDesc.instanes = meshInstanceInfo;
		//meshInstancesDesc.m_pPositionData = vertices;
		//meshInstancesDesc.m_nVertexCount = 12;
		//
		//SResourceHandle handle = CreateBuffer(meshInstancesDesc.m_pPositionData, meshInstancesDesc.m_nVertexCount * sizeof(Vec3), sizeof(Vec3), EBufferUsage::USAGE_VB);
		//
		//return AddRayTracingMeshInstances(meshInstancesDesc, handle);
		return false;
	}

	void AddPlayerCell(SPVSCell pvsCell)
	{

	}

	void GenerateVisibility()
	{
		//BuildAccelerationStructure();
		std::vector<SShader>rtShaders;
		rtShaders.push_back(SShader{ ERayShaderType::RAY_RGS,L"rayGen"});
		rtShaders.push_back(SShader{ ERayShaderType::RAY_MIH,L"miss"});
		rtShaders.push_back(SShader{ ERayShaderType::RAY_CHS,L"chs"});

		std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_pvs.cpp");
		std::wstring shaderPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"hwrtl_pvs.hlsl";
		
		//pGiBaker->m_pDeviceCommand->CreateRTPipelineStateAndShaderTable(shaderPath, rtShaders, 1, SShaderResources{ 1,1,0,0 });
	}

	CDynamicBitSet GetVisibility(uint32_t cellIndex)
	{
		STextureCreateDesc texCreateDesc{ ETexUsage::USAGE_UAV,ETexFormat::FT_RGBA8_UNORM,512,512 };
		//SResourceHandle bufferHandle = CreateTexture2D(texCreateDesc);

		//BeginRayTracing();
		//SetShaderResource(bufferHandle, ESlotType::ST_U, 0);
		//SetTLAS(0);
		//DispatchRayTracicing(512,512);
		return CDynamicBitSet();
	}

	void DestoryPVSGenerator()
	{
		Shutdown();
	}

}
};
