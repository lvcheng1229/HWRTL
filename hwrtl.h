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
#ifndef HWRTL_H
#define HWRTL_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <fstream>
#include <sstream>

#define USE_DX12_WIN 1

namespace hwrtl
{
	struct Vec3
	{
		float x;
		float y;
		float z;

		Vec3()
		{
			x = y = z = 0.0;
		}

		Vec3(float x, float y, float z)
			: x(x), y(y), z(z) 
		{
		}
	};
	
	struct Vec2
	{
		float x;
		float y;

		Vec2()
		{
			x = y = 0.0;
		}

		Vec2(float x, float y)
			: x(x), y(y)
		{
		}
	};

	enum class EInstanceFlag
	{
		NONE,
		CULL_DISABLE,
		FRONTFACE_CCW,
	};

	struct SMeshInstanceInfo
	{
		float m_transform[3][4];

		EInstanceFlag m_instanceFlag;

		SMeshInstanceInfo()
		{
			for (int i = 0; i < 3; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					if (i == j)
					{
						m_transform[i][j] = 1;
					}
					else
					{
						m_transform[i][j] = 0;
					}
				}
			}

			m_instanceFlag = EInstanceFlag::NONE;
		}
	};

	struct SMeshInstancesDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const void* m_pIndexData = nullptr; // optional

		const Vec2* m_pUVData = nullptr; // optional
		const Vec3* m_pNormalData = nullptr; // optional

		uint32_t m_nIndexStride = 0;

		uint32_t m_nVertexCount = 0;
		uint32_t m_nIndexCount = 0;

		std::vector<SMeshInstanceInfo>instanes; 
	};

	enum class EAddMeshInstancesResult
	{
		SUCESS,
		INVALID_VERTEX_COUNT, //Not evenly divisible by 3
		INVALID_INDEX_COUNT, //Not evenly divisible by 3
		INVALID_INSTANCE_INFO_NUM, // the size of mesh instance info should be > 0
	};

	void Init();
	EAddMeshInstancesResult AddMeshInstances(const SMeshInstancesDesc& meshInstancesDesc);
	void BuildAccelerationStructure();
	void CreateRTPipelineState(const std::wstring filename, const std::wstring targetString, 
		const std::vector<std::wstring>& anyHitEntryPoints,
		const std::vector<std::wstring>& clsHitEntryPoints,
		const std::vector<std::wstring>& entryPoints);
	void DestroyScene();
	void Shutdown();
}

#endif