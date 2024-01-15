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
// Dx12 hardware ray tracing gi baker usage:
//		step 1. copy hwrtl.h, d3dx12.h, hwrtl_dx12.cpp, hwrtl_gi.h and hwrtl_gi.cpp to your project
// 
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
	};

	struct SBakeMeshDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const Vec2* m_pLightMapUVData = nullptr;

		uint32_t m_nVertexCount = 0;
		Vec2i m_nLightMapSize;

		SMeshInstanceInfo m_meshInstanceInfo;
	};

	void InitGIBaker(SBakeConfig bakeConfig);
	void AddBakeMesh(const SBakeMeshDesc& bakeMeshDesc);
	void AddBakeMeshsAndCreateVB(const std::vector<SBakeMeshDesc>& bakeMeshDescs);

	void PrePareLightMapGBufferPass();
	void ExecuteLightMapGBufferPass();
	
	void PrePareLightMapRayTracingPass();
	void ExecuteLightMapRayTracingPass();

	void PrePareVisualizeResultPass();
	void ExecuteVisualizeResultPass();


	void DeleteGIBaker();

}
}
#endif