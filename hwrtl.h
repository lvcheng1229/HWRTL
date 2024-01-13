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

		inline T operator[](uint32_t index) const
		{
			return (((T*)this)[index]);
		}

		inline R operator-() const
		{
			R r;
			for (int i = 0; i < N; ++i)
				((T*)&r)[i] = -((T*)this)[i];
			return r;
		}

		inline R operator*(T s) const
		{
			R r;
			for (int i = 0; i < N; ++i)
				((T*)&r)[i] = ((T*)this)[i] * s;
			return r;
		}

		template<typename T2, typename R2>
		inline T Dot(const TVecN<T2, N, R2>& o) const
		{
			T s = ((T*)this)[0] * o[0];
			for (int i = 1; i < N; ++i)
				s = s + (((T*)this)[i] * o[i]);
			return s;
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

	inline Vec3 NormalizeVec3(Vec3 vec)
	{
		float lenght = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
		if (lenght > 0.0)
		{
			lenght = 1.0 / lenght;
		}
		return Vec3(vec.x, vec.y, vec.z) * lenght;
	}

	inline Vec3 CrossVec3(Vec3 A, Vec3 B)
	{
		return Vec3(A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x);
	}

	struct Matrix44
	{
		union
		{
			float m[4][4];
			Vec4 row[4];
		};

		Matrix44()
		{
			for (uint32_t i = 0; i < 4; i++)
				for (uint32_t j = 0; j < 4; j++)
					m[i][j] = 0;
		}

		inline void SetIdentity()
		{
			row[0] = Vec4(1, 0, 0, 0);
			row[1] = Vec4(0, 1, 0, 0);
			row[2] = Vec4(0, 0, 1, 0);
			row[3] = Vec4(0, 0, 0, 1);
		}

		inline Matrix44 GetTrasnpose()
		{
			Matrix44 C;
			for (uint32_t i = 0; i < 4; i++)
				for (uint32_t j = 0; j < 4; j++)
					C.m[i][j] = m[j][i];
			return C;
		}
	};

	inline Matrix44 MatrixMulti(Matrix44 A, Matrix44 B)
	{
		Matrix44 C;
		for (uint32_t i = 0; i < 4; i++)
		{
			for (uint32_t j = 0; j < 4; j++)
			{
				C.m[i][j] = A.row[i].Dot(Vec4(B.m[0][j], B.m[1][j], B.m[2][j], B.m[3][j]));
			}
		}
		return C;
	}

	inline Matrix44 GetViewProjectionMatrixRightHand(Vec3 eyePosition, Vec3 eyeDirection, Vec3 upDirection, float fovAngleY, float aspectRatio, float nearZ, float farZ)
	{	
		//wolrd space			 
		//	  z y				 
		//	  |/				 
		//    --->x				 
		
		//capera space
		//	  y
		//	  |
		//    ---->x
		//	 /
		//  z

		Vec3 zAxis = NormalizeVec3(-eyeDirection);
		Vec3 xAxis = NormalizeVec3(CrossVec3(upDirection,zAxis));
		Vec3 yAxis = CrossVec3(zAxis, xAxis);

		Vec3 negEye = -eyePosition;

		Matrix44 camTranslate;
		camTranslate.SetIdentity();
		camTranslate.m[0][3] = negEye.x;
		camTranslate.m[1][3] = negEye.y;
		camTranslate.m[2][3] = negEye.z;

		Matrix44 camRotate;
		camRotate.row[0] = Vec4(xAxis.x, xAxis.y, xAxis.z, 0);
		camRotate.row[1] = Vec4(yAxis.x, yAxis.y, yAxis.z, 0);
		camRotate.row[2] = Vec4(zAxis.x, zAxis.y, zAxis.z, 0);
		camRotate.row[3] = Vec4(0, 0, 0, 1);

		Matrix44 viewMat = MatrixMulti(camRotate, camTranslate);

		float radians = 0.5f * fovAngleY * 3.1415926535f / 180.0f;
		float sinFov = std::sin(radians);
		float cosFov = std::cos(radians);

		float Height = cosFov / sinFov;
		float Width = Height / aspectRatio;
		float fRange = farZ / (nearZ - farZ);

		Matrix44 projMat;
		projMat.m[0][0] = Width;
		projMat.m[1][1] = Height;
		projMat.m[2][2] = fRange;
		projMat.m[2][3] = fRange * nearZ;
		projMat.m[3][2] = -1.0f;
		//return viewMat;
		return MatrixMulti(projMat, viewMat);
	}

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
		USAGE_VB = 0, // vertex buffer
		USAGE_IB, // index buffer
		USAGE_CB, // constant buffer
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

	class CGraphicsPipelineState {};

	void Init();

	SResourceHandle CreateTexture2D(STextureCreateDesc texCreateDesc);
	SResourceHandle CreateBuffer(const void* pInitData, uint64_t nByteSize, uint64_t nStride, EBufferUsage bufferUsage);
	void UpdateConstantBuffer(SResourceHandle resourceHandle, uint64_t nByteSize, const void* pData);
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
	std::shared_ptr<CGraphicsPipelineState>  CreateRSPipelineState(const std::wstring filename, std::vector<SShader>rtShaders, SShaderResources rasterizationResources, std::vector<EVertexFormat>vertexLayouts, std::vector<ETexFormat>rtFormats);

	void WaitForPreviousFrame();
	void ResetCmdList();
	void BeginRasterization(std::shared_ptr<CGraphicsPipelineState> graphicsPipelineStata);
	void SetRenderTargets(SResourceHandle* renderTargets, uint32_t renderTargetNum, SResourceHandle depthStencil = -1, bool bClearRT = true, bool bClearDs = true);
	void SetConstantBuffer(SResourceHandle cbHandle, uint32_t offset);
	void SetTexture(SResourceHandle cbHandle, uint32_t offset);
	void SetViewport(float width, float height);
	void SetVertexBuffers(SResourceHandle* vertexBuffer, uint32_t slotNum = 1);
	void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t InstanceCount, uint32_t StartVertexLocation, uint32_t StartInstanceLocation);
	void SubmitCommandlist();

	void DestroyScene();
	void Shutdown();
}

#endif