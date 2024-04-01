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
#ifndef HWRTL_LODTEX_H
#define HWRTL_LODTEX_H

#include "hwrtl.h"

namespace hwrtl
{
namespace hlod
{
	struct SHLODConfig
	{
		Vec2i m_nHLODTextureSize;
	};

	struct SHLODBakerMeshDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const Vec2* m_pUVData = nullptr;
		uint32_t m_nVertexCount = 0;
		SMeshInstanceInfo m_meshInstanceInfo;

		Vec2i m_bakedTextureSize;
	};

	struct SHLODHighPolyMeshDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const Vec2* m_pUVData = nullptr;
		uint32_t m_nVertexCount = 0;
		SMeshInstanceInfo m_meshInstanceInfo;

		std::shared_ptr<CTexture2D> m_pBaseColorTexture;
		std::shared_ptr<CTexture2D> m_pNormalTexture;
	};


	struct SHLODTextureOutData
	{
		void* destBaseColorOutputData = nullptr;
		void* destNormalOutputData = nullptr;
		uint32_t m_texByteSize = 0;
		uint32_t m_pixelStride = 0;
		Vec2i m_texSize;
	};

	std::shared_ptr<CTexture2D> CreateHighPolyTexture2D(STextureCreateDesc texCreateDesc);

	void InitHLODTextureBaker(SHLODConfig hlodConfig);
	void SetHLODMesh(const SHLODBakerMeshDesc& hlodMeshDesc);
	void SetHighPolyMesh(const std::vector<SHLODHighPolyMeshDesc>& highMeshDescs);
	void BakeHLODTexture();
	void GetHLODTextureData(SHLODTextureOutData& hlodTextureData);
	void DeleteHLODBaker();
}
}

#endif