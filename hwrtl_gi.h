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
		bool m_bDebugRayTracing = false; // see RT_DEBUG_OUTPUT in hwrtl_gi.hlsl
		bool m_bAddVisualizePass = false;
	};

	// must math the shading model id define in the hlsl shader
	enum class EShadingModelID : uint32_t
	{
		E_DEFAULT_LIT = (1 << 1),
	};

	class CMaterialProperty
	{
	public:
		inline bool IsValid()
		{
			return m_isValid;
		}

		inline bool IsUseTexture()
		{
			return m_bUseTexture;
		}
	protected:
		bool m_isValid = false;
		bool m_bUseTexture = false;
	};

	class CMaterialBaseColorProperty : public CMaterialProperty
	{
	public:
		inline void SetBaseColorTexture(std::shared_ptr<CTexture2D>baseColorTexture)
		{
			m_baseColorTex = baseColorTexture;
			m_bUseTexture = true;
			m_isValid = true;
		}

		inline void SetDefaultValue(Vec3 defaultValue)
		{
			m_baseColorDefaultValue = defaultValue;
			m_isValid = true;
		}

	private:
		std::shared_ptr<CTexture2D> m_baseColorTex;
		Vec3 m_baseColorDefaultValue;
	};

	class CMaterialRoughnessProperty : public CMaterialProperty
	{
	public:
		inline void SetRoughnessTexture(std::shared_ptr<CTexture2D>roughnessTexture, uint32_t roughnessChannelInTex)
		{
			m_roughnessTexture = roughnessTexture;
			roughnessChannel = roughnessChannelInTex;
			m_bUseTexture = true;
			m_isValid = true;
		}

		inline void SetDefaultValue(float defaultValue)
		{
			m_roughnessDefaultValue = defaultValue;
			m_isValid = true;
		}
	private:
		uint32_t roughnessChannel;
		std::shared_ptr<CTexture2D> m_roughnessTexture;
		float m_roughnessDefaultValue;
	};

	class CMaterialMetallicProperty : public CMaterialProperty
	{
	public:
		inline void SetMetallicTexture(std::shared_ptr<CTexture2D>metallicTexture, uint32_t metallicChannelInTex)
		{
			m_metallicTexture = metallicTexture;
			metallicChannel = metallicChannelInTex;
			m_bUseTexture = true;
			m_isValid = true;
		}

		inline void SetDefaultValue(float defaultValue)
		{
			m_metallicDefaultValue = defaultValue;
			m_isValid = true;
		}
	private:
		uint32_t metallicChannel;
		std::shared_ptr<CTexture2D> m_metallicTexture;
		float m_metallicDefaultValue;
	};

	struct SBakeMeshDesc
	{
		const Vec3* m_pPositionData = nullptr;
		const Vec2* m_pLightMapUVData = nullptr;
		const Vec3* m_pNormalData = nullptr; // unused

		uint32_t m_nVertexCount = 0;
		Vec2i m_nLightMapSize;

		CMaterialBaseColorProperty m_materialBaseColorProperty;
		CMaterialRoughnessProperty m_materialRoughnessProperty;
		CMaterialRoughnessProperty m_materialMetallicProperty;
		EShadingModelID m_shadingModelID;

		SMeshInstanceInfo m_meshInstanceInfo;
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


	void DeleteGIBaker();

}
}
#endif