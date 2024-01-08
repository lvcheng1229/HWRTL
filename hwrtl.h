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

	template<typename T,int N, typename R>
	struct TVecN
	{
		TVecN()
		{
			for (uint32_t index = 0; index < N; index++)
			{
				(((T*)this)[index]) = 0;
			}
		}

		template<typename T2, typename R2>
		inline void Set(const TVecN<T2, N,R2>& o)
		{
			for (int i = 0; i < N; ++i)
				((T*)this)[i] = static_cast<T>(((T2*)&o)[i]);
		}

#define VEC_OPERATOR(op) \
		inline R operator op(const TVecN<T,N,R>& o) const \
		{ \
			R r; \
			for (int i = 0; i < N; ++i) \
				((T*)(&r))[i] = ((T*)(this))[i] op ((T*)(&o))[i]; \
			return r; \
		} \

		VEC_OPERATOR(+);
		VEC_OPERATOR(-);
		VEC_OPERATOR(*);
		VEC_OPERATOR(/);
	};

	template<class F> 
	struct TVec2 : TVecN<F,2, TVec2<F>>
	{
		F x; F y;
		template<typename T2>
		TVec2(const TVec2<T2>& in) { TVecN<F, 2, TVec2<F>>::Set(in); }
		TVec2() :TVecN<F, 2, TVec2<F>>() {}
		TVec2(F x, F y) : x(x), y(y) {}
	};

	template<class F>
	struct TVec3 : TVecN<F, 3, TVec3<F>>
	{
		F x; F y; F z;
		template<typename T2>
		TVec3(const TVec3<T2>& in) { TVecN<F, 3, TVec3<F>>::Set(in); }
		TVec3() :TVecN<F, 3, TVec3<F>>() {}
		TVec3(F x, F y, F z): x(x), y(y),z(z){}
	};

	template<class F>
	struct TVec4 : TVecN<F, 4, TVec4<F>>
	{
		F x; F y; F z; F w;
		template<typename T2>
		TVec4(const TVec4<T2>& in) { TVecN<F, 4, TVec4<F>>::Set(in); }
		TVec4() :TVecN<F, 4, TVec4<F>>() {}
		TVec4(F x, F y, F z, F w) : x(x), y(y), z(z), w(w) {}
	};

	using Vec2 = TVec2<float>;
	using Vec2i = TVec2<int>;

	using Vec3 = TVec3<float>;
	using Vec3i = TVec3<int>;

	using Vec4 = TVec4<float>;
	using Vec4i = TVec4<int>;

	using WstringConverter = std::wstring_convert<std::codecvt_utf8<wchar_t>>;
	using SResourceHandle = int;

	// enum class 
	
	enum class EInstanceFlag
	{
		NONE,
		CULL_DISABLE,
		FRONTFACE_CCW,
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

	enum class EVertexFormat
	{
		FT_FLOAT3,
		FT_FLOAT2,
	};

	enum class ESlotType
	{
		ST_T = 0, // SRV
		ST_U, // UAV
		ST_B, // CBV
		ST_S, // Samper
	};

	enum class ETexFormat
	{
		FT_RGBA8_UNORM, 
		FT_RGBA32_FLOAT,
	};

	enum class ETexUsage
	{
		USAGE_UAV = 1 << 1,
		USAGE_SRV = 1 << 2,
		USAGE_RTV = 1 << 3,
		USAGE_DSV = 1 << 4,
	};
	DEFINE_ENUM_FLAG_OPERATORS(ETexUsage);

	enum class EBufferUsage
	{
		USAGE_VB = 1 << 1, // vertex buffer
		USAGE_IB = 1 << 2, // index buffer
		USAGE_CB = 1 << 3, // constant buffer
	};
	DEFINE_ENUM_FLAG_OPERATORS(EBufferUsage);

	// struct 

	struct SMeshInstanceInfo
	{
		float m_transform[3][4];

		EInstanceFlag m_instanceFlag;

		SMeshInstanceInfo()
		{
			for (int i = 0; i < 12; i++)
			{
				((float*)m_transform)[i] = 0;
			}
			m_transform[0][0] = 1;
			m_transform[1][1] = 1;
			m_transform[2][2] = 1;

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

	struct SShader
	{
		ERayShaderType m_eShaderType;
		const std::wstring m_entryPoint;
	};

	struct STextureCreateDesc
	{
		ETexUsage m_eTexUsage;
		ETexFormat m_eTexFormat;
		uint32_t m_width;
		uint32_t m_height;
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

		uint32_t GetPreSumNum(uint32_t index)
		{
			uint32_t result = 0;
			for (uint32_t i = 0; i < index; i++)
			{
				result += this->operator[](i);
			}
			return result;
		}
	};

	void Init();

	SResourceHandle CreateTexture2D(STextureCreateDesc texCreateDesc);
	SResourceHandle CreateBuffer(const void* pInitData, uint64_t nByteSize, uint64_t nStride, EBufferUsage bufferUsage);
	void SubmitCommand();

	// ray tracing pipeline
	EAddMeshInstancesResult AddRayTracingMeshInstances(const SMeshInstancesDesc& meshInstancesDesc, SResourceHandle vbResouce);
	void BuildAccelerationStructure();
	void CreateRTPipelineStateAndShaderTable(const std::wstring filename, std::vector<SShader>rtShaders, uint32_t maxTraceRecursionDepth, SShaderResources rayTracingResources);
	
	void SetShaderResource(SResourceHandle resource, ESlotType slotType, uint32_t bindIndex);
	void SetTLAS(uint32_t bindIndex);
	void BeginRayTracing();
	void DispatchRayTracicing(uint32_t width, uint32_t height);

	//rasterization pipeline
	void AddRasterizationMeshs(const SMeshInstancesDesc& meshDescs);
	void CreateRSPipelineState(const std::wstring filename, std::vector<SShader>rtShaders, SShaderResources rasterizationResources, std::vector<EVertexFormat>vertexLayouts, std::vector<ETexFormat>rtFormats);

	void BeginRasterization();
	void SetRenderTargets(SResourceHandle* renderTargets, uint32_t renderTargetNum, SResourceHandle depthStencil = -1, bool bClearRT = true, bool bClearDs = true);
	void SetViewport(float width, float height);
	void SetVertexBuffers(SResourceHandle* vertexBuffer, uint32_t slotNum = 1);
	void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t InstanceCount, uint32_t StartVertexLocation, uint32_t StartInstanceLocation);
	void SubmitCommandlist();

	void DestroyScene();
	void Shutdown();
}

#endif