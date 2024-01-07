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
// 
// Dx12 hardware ray tracing library usage:
//		step 1. copy hwrtl.h, d3dx12.h, hwrtl_dx12.cpp to your project
//		step 2. enable graphics api by #define ENABLE_DX12_WIN 1
// 
// Vk hardware ray tracing libirary usage:
//		
// 



#pragma once
#ifndef HWRTL_H
#define HWRTL_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>

#define ENABLE_DX12_WIN 1

namespace hwrtl
{
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE)\
inline constexpr ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b)  { return ENUMTYPE(((uint32_t)a) | ((uint32_t)b)); } \
inline ENUMTYPE &operator |= (ENUMTYPE &a, ENUMTYPE b)  { return (ENUMTYPE &)(((uint32_t &)a) |= ((uint32_t)b)); } \
inline constexpr ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b)  { return ENUMTYPE(((uint32_t)a) & ((uint32_t)b)); } \
inline ENUMTYPE &operator &= (ENUMTYPE &a, ENUMTYPE b)  { return (ENUMTYPE &)(((uint32_t &)a) &= ((uint32_t)b)); } \
inline constexpr ENUMTYPE operator ~ (ENUMTYPE a)  { return ENUMTYPE(~((uint32_t)a)); } \
inline constexpr ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b)  { return ENUMTYPE(((uint32_t)a) ^ ((uint32_t)b)); } \
inline ENUMTYPE &operator ^= (ENUMTYPE &a, ENUMTYPE b)  { return (ENUMTYPE &)(((uint32_t &)a) ^= ((uint32_t)b)); } \

	inline std::wstring String2Wstring(const std::string& str)
	{
		return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(str);
	}

	typedef int SResourceHandle;
	
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

	enum class ERayShaderType
	{
		RAY_RGS, // ray gen shader
		RAY_MIH, // ray miss shader 
		RAY_AHS, // any hit shader
		RAY_CHS, // close hit shader

		RS_VS,	 // vertex shader
		RS_PS,	 // pixel shader
	};

	struct SShader
	{
		ERayShaderType m_eShaderType;
		const std::wstring m_entryPoint;
	};

	struct SShaderResources
	{
		uint32_t m_nSRV = 0;
		uint32_t m_nUAV = 0;
		uint32_t m_nCBV = 0;
		uint32_t m_nSampler = 0;

		uint32_t operator[](std::size_t index)
		{
			return *((uint32_t*)(this) + index);
		}
	};

	enum class ETexFormat
	{
		FT_RGBA8_UNORM,
		FT_RGBA32_FLOAT,
	};

	enum class ETexUsage
	{
		USAGE_UAV,
		USAGE_SRV,
		USAGE_RTV,
		USAGE_DSV,
	};
	DEFINE_ENUM_FLAG_OPERATORS(ETexUsage);

	struct STextureCreateDesc
	{
		ETexUsage m_eTexUsage;
		ETexFormat m_eTexFormat;
		uint32_t m_width;
		uint32_t m_height;
	};

	enum class ESlotType
	{
		ST_T = 0, // SRV
		ST_U, // UAV
		ST_B, // CBV
		ST_S, // Samper
	};

	void Init();

	SResourceHandle CreateTexture2D(STextureCreateDesc texCreateDesc);

	// ray tracing pipeline
	EAddMeshInstancesResult AddRayTracingMeshInstances(const SMeshInstancesDesc& meshInstancesDesc);
	void BuildAccelerationStructure();
	void CreateRTPipelineStateAndShaderTable(const std::wstring filename, std::vector<SShader>rtShaders, uint32_t maxTraceRecursionDepth, SShaderResources rayTracingResources);
	
	void SetShaderResource(SResourceHandle resource, ESlotType slotType, uint32_t bindIndex);
	void SetTLAS(uint32_t bindIndex);
	void BeginRayTracing();
	void DispatchRayTracicing(uint32_t width, uint32_t height);

	//rasterization pipeline
	void AddRasterizationMeshs(const SMeshInstancesDesc& meshDescs);
	void CreateRSPipelineState(const std::wstring filename, std::vector<SShader>rtShaders, SShaderResources rasterizationResources);
	void SetRenderTargets(SResourceHandle renderTarget, SResourceHandle depthStencil = -1, bool bClearRT = true, bool bClearDs = true);
	void ExecuteRasterization(float width, float height);

	void DestroyScene();
	void Shutdown();
}

#endif