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
// Basic usage:
//		step 1. copy hwrtl.h, d3dx12.h, hwrtl_dx12.cpp, hwrtl_gi.h and hwrtl_gi.cpp to your project
// 
// Custom denoiser usage:
//		
// Notice:
//		1. we use right-handed coordinate system, so the front face of the triangle is counter-clockwise
// TODO:
//		1. instance mesh		
// 

#pragma once
#ifndef HWRTL_GI_H
#define HWRTL_GI_H

#include "hwrtl.h"

namespace hwrtl
{
namespace gi
{
	struct SBakeConfig
	{
		uint32_t m_maxAtlasSize;
		uint32_t m_bakerSamples;
		bool m_bDebugRayTracing = false; // see RT_DEBUG_OUTPUT in hwrtl_gi.hlsl
		bool m_bAddVisualizePass = false;
		bool m_bUseCustomDenoiser = false; // use custom denoiser or hwrtl default denoiser
	};

	// must match the shading model id define in the hlsl shader
	enum class EShadingModelID : uint32_t
	{
		E_DEFAULT_LIT = (1 << 1),
	};


	struct SBakeMeshDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const Vec2* m_pLightMapUVData = nullptr;
		const Vec3i* m_pIndexData = nullptr;
		const Vec3* m_pNormalData = nullptr; // optional

		uint32_t m_nVertexCount = 0;
		uint32_t m_nIndexCount = 0;

		Vec2i m_nLightMapSize;

		SMeshInstanceInfo m_meshInstanceInfo;
	};

	
	class CLightMapDenoiser
	{
	public:  
		virtual void InitDenoiser() = 0;
	};

	struct SOutputAtlasInfo
	{
		std::vector<uint32_t> m_orginalMeshIndex;
		void* destOutputData = nullptr;
		uint32_t m_lightMapByteSize = 0;
		uint32_t m_pixelStride = 0;
		Vec2i m_lightMapSize;
	};

	void InitGIBaker(SBakeConfig bakeConfig);
	void AddBakeMesh(const SBakeMeshDesc& bakeMeshDesc);
	void AddBakeMeshsAndCreateVB(const std::vector<SBakeMeshDesc>& bakeMeshDescs);

	void AddDirectionalLight(Vec3 color, Vec3 direction, bool isStationary);
	void AddSphereLight(Vec3 color, Vec3 worldPosition, bool isStationary, float attenuation, float radius);

	void PrePareLightMapGBufferPass();
	void ExecuteLightMapGBufferPass();
	
	void PrePareLightMapRayTracingPass();
	void ExecuteLightMapRayTracingPass();

	void PrePareVisualizeResultPass();
	void ExecuteVisualizeResultPass();

	void DenoiseAndDilateLightMap(); // optional pass, you can denoise the output lightmap with your custom denoiser such as oidn
	void EncodeResulttLightMap(); // optional pass, you can encode the lightmap by you self
	void GetEncodedLightMapTexture(std::vector<SOutputAtlasInfo>& outputAtlas);

	void FreeLightMapCpuData();

	void GetEncodedLightMap(uint32_t orinalMeshIndex); //TODO
	void GetUnEncodedLightMapTexture(); // TODO:

	void DeleteGIBaker();
	

}
}
#endif