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

#pragma once
#ifndef HWRTL_PVS_H
#define HWRTL_PVS_H

#include "hwrtl.h"


namespace hwrtl
{
namespace pvs
{
	//temporary 
	class CDynamicBitSet
	{
	public:
		std::vector<bool> result;
	};

	enum class EPVSInitResult
	{
		SUCESS,
	};

	struct SPVSOptiion
	{

	};

	void InitPVSGenerator();

	struct SOccluderDesc
	{
		const Vec3* m_pPositionData;
		const void* m_pIndexData;// optional

		uint32_t m_nVertexCount;
		uint32_t m_nIndexCount;

		uint32_t m_nIndexStride;
	};

	struct SOccluderBound
	{
		Vec3 m_min;
		Vec3 m_max;
	};

	EAddMeshInstancesResult AddOccluder(const SOccluderDesc& occluderDesc, const std::vector<SMeshInstanceInfo>& meshInstanceInfo, std::vector<uint32_t>& instanceIndices);

	EAddMeshInstancesResult AddOccluderBound(const SOccluderBound& occluderBound, uint32_t instanceIndex);
	EAddMeshInstancesResult AddOccluderBounds(const SOccluderBound& occluderBound, const std::vector<SMeshInstanceInfo>& meshInstanceInfo, std::vector<uint32_t>& instanceIndices);

	struct SPVSCell
	{
		Vec3 m_min;
		Vec3 m_max;

		int m_cellIndex = -1;
	};

	void AddPlayerCell(SPVSCell pvsCell);
	
	
	void GenerateVisibility();
	CDynamicBitSet GetVisibility(uint32_t cellIndex);

	void DestoryPVSGenerator();
}
};
#endif