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

	EAddMeshInstancesResult AddOccluder(const SOccluderDesc& occluderDesc, const std::vector<SMeshInstanceInfo>& meshInstanceInfo)
	{
		return EAddMeshInstancesResult();
	}

	EAddMeshInstancesResult AddOccluderBound(const SOccluderBound& occluderBound)
	{
		SMeshInstanceInfo meshInstanceInfo;
		std::vector<SMeshInstanceInfo> meshInstanceInfos;
		meshInstanceInfos.push_back(meshInstanceInfo);
		return AddOccluderBounds(occluderBound, meshInstanceInfos);
	}

	EAddMeshInstancesResult AddOccluderBounds(const SOccluderBound& occluderBound, const std::vector<SMeshInstanceInfo>& meshInstanceInfo)
	{
		const Vec3 boundMin = occluderBound.m_min;
		const Vec3 boundMax = occluderBound.m_max;
		Vec3 vertices[12 * 3];

		//top triangle 1
		vertices[0] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		vertices[1] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		vertices[2] = Vec3(boundMin.x, boundMax.y, boundMax.z);

		//top triangle 2
		vertices[3] = Vec3(boundMin.x, boundMax.y, boundMax.z); 
		vertices[4] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		vertices[5] = Vec3(boundMin.x, boundMin.y, boundMax.z);

		//front triangle 1
		vertices[6] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		vertices[7] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		vertices[8] = Vec3(boundMax.x, boundMin.y, boundMin.z);

		//front triangle 2
		vertices[9] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		vertices[10] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		vertices[11] = Vec3(boundMin.x, boundMin.y, boundMin.z);

		//back triangle 1
		vertices[12] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		vertices[13] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		vertices[14] = Vec3(boundMin.x, boundMax.y, boundMin.z);

		//back triangle 2
		vertices[15] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		vertices[16] = Vec3(boundMin.x, boundMax.y, boundMin.z);
		vertices[17] = Vec3(boundMax.x, boundMax.y, boundMin.z);

		//bottom triangle 1
		vertices[18] = Vec3(boundMax.x, boundMin.y, boundMin.z);
		vertices[19] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		vertices[20] = Vec3(boundMax.x, boundMax.y, boundMin.z);

		//bottom triangle 2
		vertices[21] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		vertices[22] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		vertices[23] = Vec3(boundMin.x, boundMax.y, boundMin.z);

		//right triangle 1
		vertices[24] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		vertices[25] = Vec3(boundMax.x, boundMax.y, boundMax.z);
		vertices[26] = Vec3(boundMax.x, boundMax.y, boundMin.z);

		//right triangle 2
		vertices[27] = Vec3(boundMax.x, boundMin.y, boundMax.z);
		vertices[28] = Vec3(boundMax.x, boundMax.y, boundMin.z);
		vertices[29] = Vec3(boundMax.x, boundMin.y, boundMin.z);

		//left triangle 1
		vertices[30] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		vertices[31] = Vec3(boundMin.x, boundMin.y, boundMax.z);
		vertices[32] = Vec3(boundMin.x, boundMin.y, boundMin.z);

		//left triangle 2
		vertices[32] = Vec3(boundMin.x, boundMax.y, boundMax.z);
		vertices[33] = Vec3(boundMin.x, boundMin.y, boundMin.z);
		vertices[34] = Vec3(boundMin.x, boundMax.y, boundMin.z);

		SMeshInstancesDesc meshInstancesDesc;
		meshInstancesDesc.instanes = meshInstanceInfo;
		meshInstancesDesc.m_pPositionData = vertices;
		meshInstancesDesc.m_nVertexCount = 12;
		
		return AddMeshInstances(meshInstancesDesc);
	}

	void AddPlayerCell(SPVSCell pvsCell)
	{
	}

	void GenerateVisibility()
	{
	}

	void GetVisibility()
	{
	}

	void DestoryPVSGenerator()
	{
		Shutdown();
	}

}
};
