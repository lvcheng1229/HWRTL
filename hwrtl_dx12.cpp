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

#include "HWRTL.h"
#if USE_DX12_WIN
#include <d3d12.h>
#include <comdef.h>
#include <dxgi1_4.h>
#include <dxcapi.h>

#include "d3dx12.h"

#include <Windows.h>

#include <locale>
#include <codecvt>

#include <iostream>
#include <vector>
#include <stdexcept>

#define ENABLE_THROW_FAILED_RESULT 1
#define ENABLE_DX12_DEBUG_LAYER 1

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"D3D12.lib")
#pragma comment(lib,"dxgi.lib")

_COM_SMARTPTR_TYPEDEF(IDXGIFactory4, __uuidof(IDXGIFactory4));
_COM_SMARTPTR_TYPEDEF(ID3D12Device5, __uuidof(ID3D12Device5));
_COM_SMARTPTR_TYPEDEF(IDXGIAdapter1, __uuidof(IDXGIAdapter1));
_COM_SMARTPTR_TYPEDEF(ID3D12Debug, __uuidof(ID3D12Debug));
_COM_SMARTPTR_TYPEDEF(ID3D12CommandQueue, __uuidof(ID3D12CommandQueue));
_COM_SMARTPTR_TYPEDEF(ID3D12CommandAllocator, __uuidof(ID3D12CommandAllocator));
_COM_SMARTPTR_TYPEDEF(ID3D12GraphicsCommandList4, __uuidof(ID3D12GraphicsCommandList4));
_COM_SMARTPTR_TYPEDEF(ID3D12Resource, __uuidof(ID3D12Resource));
_COM_SMARTPTR_TYPEDEF(ID3D12Fence, __uuidof(ID3D12Fence));
_COM_SMARTPTR_TYPEDEF(IDxcBlobEncoding, __uuidof(IDxcBlobEncoding));
_COM_SMARTPTR_TYPEDEF(IDxcCompiler, __uuidof(IDxcCompiler));
_COM_SMARTPTR_TYPEDEF(IDxcLibrary, __uuidof(IDxcLibrary));
_COM_SMARTPTR_TYPEDEF(IDxcOperationResult, __uuidof(IDxcOperationResult));
_COM_SMARTPTR_TYPEDEF(IDxcBlob, __uuidof(IDxcBlob));
_COM_SMARTPTR_TYPEDEF(ID3DBlob, __uuidof(ID3DBlob));
_COM_SMARTPTR_TYPEDEF(ID3D12StateObject, __uuidof(ID3D12StateObject));
_COM_SMARTPTR_TYPEDEF(ID3D12RootSignature, __uuidof(ID3D12RootSignature));


namespace hwrtl
{
    //https://github.com/microsoft/DirectX-Graphics-Samples
    /***************************************************************************
    The MIT License (MIT)
    
    Copyright (c) 2015 Microsoft
    
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    ***************************************************************************/

    inline std::string ResultToString(HRESULT hr)
    {
        char s_str[64] = {};
        sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
        return std::string(s_str);
    }

    inline void ThrowIfFailed(HRESULT hr)
    {
#if ENABLE_THROW_FAILED_RESULT
        if (FAILED(hr))
        {
            throw std::runtime_error(ResultToString(hr));
        }
#endif
    }
    
    template<class BlotType>
    std::string ConvertBlobToString(BlotType* pBlob)
    {
        std::vector<char> infoLog(pBlob->GetBufferSize() + 1);
        memcpy(infoLog.data(), pBlob->GetBufferPointer(), pBlob->GetBufferSize());
        infoLog[pBlob->GetBufferSize()] = 0;
        return std::string(infoLog.data());
    }


    struct SDXMeshInstanceInfo
    {
        ID3D12ResourcePtr m_pPositionBuffer;
        ID3D12ResourcePtr m_pIndexBuffer;
        ID3D12ResourcePtr m_pUVBuffer;
        ID3D12ResourcePtr m_pNormalBuffer;

        uint32_t m_nIndexStride = 0;

        uint32_t m_nVertexCount = 0;
        uint32_t m_nIndexCount = 0;

        std::vector<SMeshInstanceInfo>instanes;
    };

    struct SAccelerationStructureBuffers
    {
        ID3D12ResourcePtr pScratch;
        ID3D12ResourcePtr pResult;
        ID3D12ResourcePtr pInstanceDesc;
    };

    class CRayTracingDX12
    {
    public:
        ID3D12Device5Ptr m_pDevice;;
        ID3D12CommandQueuePtr m_pCmdQueue;
        ID3D12CommandAllocatorPtr m_pCmdAllocator;
        ID3D12GraphicsCommandList4Ptr m_pCmdList;
        ID3D12FencePtr m_pFence;
        IDxcCompilerPtr m_pDxcCompiler;
        IDxcLibraryPtr m_pLibrary;

        HANDLE m_FenceEvent;

        uint64_t m_nFenceValue = 0;;

        ID3D12StateObjectPtr m_pRtPipelineState;
        std::vector<SDXMeshInstanceInfo>m_dxMeshInstanceInfos;
        std::vector<ID3D12ResourcePtr> m_uploadBuffers;

        void InitDxc();

        ID3D12ResourcePtr& AllocUploadBuffer()
        {
            m_uploadBuffers.push_back(ID3D12ResourcePtr());
            return m_uploadBuffers.back();
        }

    private:
        HMODULE m_dxcDll;
        DxcCreateInstanceProc m_createFn;
    };

    void hwrtl::CRayTracingDX12::InitDxc()
    {
        m_dxcDll = LoadLibraryW(L"dxcompiler.dll");
        if (m_dxcDll == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
        else
        {
            m_createFn = (DxcCreateInstanceProc)GetProcAddress(m_dxcDll, "DxcCreateInstance");

            if (m_createFn == nullptr) {
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
                FreeLibrary(m_dxcDll);
                m_dxcDll = nullptr;
            }

            ThrowIfFailed(m_createFn(CLSID_DxcCompiler, IID_PPV_ARGS(&m_pDxcCompiler)));
            ThrowIfFailed(m_createFn(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary)));
        }
    }
    static CRayTracingDX12* pRayTracingDX12 = nullptr;

    //https://github.com/NVIDIAGameWorks/DxrTutorials
    /***************************************************************************
    # Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
    #
    # Redistribution and use in source and binary forms, with or without
    # modification, are permitted provided that the following conditions
    # are met:
    #  * Redistributions of source code must retain the above copyright
    #    notice, this list of conditions and the following disclaimer.
    #  * Redistributions in binary form must reproduce the above copyright
    #    notice, this list of conditions and the following disclaimer in the
    #    documentation and/or other materials provided with the distribution.
    #  * Neither the name of NVIDIA CORPORATION nor the names of its
    #    contributors may be used to endorse or promote products derived
    #    from this software without specific prior written permission.
    #
    # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
    # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
    # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
    # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
    # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
    # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
    # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
    ***************************************************************************/

    std::wstring string_2_wstring(const std::string& str)
    {
        std::wstring_convert<std::codecvt_utf8<WCHAR>> cvt;
        std::wstring ws = cvt.from_bytes(str);
        return ws;
    }

    std::string wstring_2_string(const std::wstring& wstr)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
        std::string s = cvt.to_bytes(wstr);
        return s;
    }

    // init dxr
    ID3D12Device5Ptr CreateDevice(IDXGIFactory4Ptr pDxgiFactory)
    {
        IDXGIAdapter1Ptr pAdapter;

        for (uint32_t i = 0; DXGI_ERROR_NOT_FOUND != pDxgiFactory->EnumAdapters1(i, &pAdapter); i++)
        {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue;
            }

#ifdef ENABLE_DX12_DEBUG_LAYER
            ID3D12DebugPtr pDx12Debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDx12Debug))))
            {
                pDx12Debug->EnableDebugLayer();
            }
#endif
            ID3D12Device5Ptr pDevice;
            ThrowIfFailed(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice)));

            D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
            HRESULT hr = pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
            if (SUCCEEDED(hr) && features5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            {
                return pDevice;
            }
        }

        ThrowIfFailed(-1);
        return nullptr;
    }

    ID3D12CommandQueuePtr CreateCommandQueue(ID3D12Device5Ptr pDevice)
    {
        ID3D12CommandQueuePtr pQueue;
        D3D12_COMMAND_QUEUE_DESC cqDesc = {};
        cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(pDevice->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&pQueue)));
        return pQueue;
    }

    void SubmitCommandList()
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pRayTracingDX12->m_pCmdList;
        ID3D12CommandQueuePtr pCmdQueue = pRayTracingDX12->m_pCmdQueue;
        ID3D12CommandAllocatorPtr pCmdAllocator = pRayTracingDX12->m_pCmdAllocator;
        ID3D12FencePtr pFence = pRayTracingDX12->m_pFence;
        
        uint64_t& nfenceValue = pRayTracingDX12->m_nFenceValue;
        HANDLE& fenceEvent = pRayTracingDX12->m_FenceEvent;

        pCmdList->Close();
        ID3D12CommandList* pGraphicsList = pCmdList.GetInterfacePtr();
        pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
        nfenceValue++;
        pCmdQueue->Signal(pFence, nfenceValue);
        pFence->SetEventOnCompletion(nfenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
        pCmdList->Reset(pCmdAllocator, nullptr);
    }

	void Init()
	{
        pRayTracingDX12 = new CRayTracingDX12();

		IDXGIFactory4Ptr pDxgiFactory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));
        
        pRayTracingDX12->m_pDevice = CreateDevice(pDxgiFactory);
        pRayTracingDX12->m_pCmdQueue = CreateCommandQueue(pRayTracingDX12->m_pDevice);

        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pRayTracingDX12->m_pCmdAllocator)));
        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pRayTracingDX12->m_pCmdAllocator, nullptr, IID_PPV_ARGS(&pRayTracingDX12->m_pCmdList)));

        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pRayTracingDX12->m_pFence)));
        pRayTracingDX12->m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        pRayTracingDX12->InitDxc();
	}

    //https://github.com/d3dcoder/d3d12book
    static CD3DX12_HEAP_PROPERTIES defaultHeapProperies(D3D12_HEAP_TYPE_DEFAULT);
    static CD3DX12_HEAP_PROPERTIES uploadHeapProperies(D3D12_HEAP_TYPE_UPLOAD);

    ID3D12ResourcePtr CreateDefaultBuffer(const void* pInitData, UINT64 nByteSize, ID3D12ResourcePtr& pUploadBuffer)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pRayTracingDX12->m_pCmdList;

        ID3D12ResourcePtr defaultBuffer;

        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(nByteSize);
        ThrowIfFailed(pDevice->CreateCommittedResource(&defaultHeapProperies, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&defaultBuffer)));
        ThrowIfFailed(pDevice->CreateCommittedResource(&uploadHeapProperies, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pUploadBuffer)));

        D3D12_SUBRESOURCE_DATA subResourceData = {};
        subResourceData.pData = pInitData;
        subResourceData.RowPitch = nByteSize;
        subResourceData.SlicePitch = subResourceData.RowPitch;

        CD3DX12_RESOURCE_BARRIER resourceBarrierBefore = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        pCmdList->ResourceBarrier(1, &resourceBarrierBefore);

        UpdateSubresources<1>(pCmdList, defaultBuffer, pUploadBuffer, 0, 0, 1, &subResourceData);

        CD3DX12_RESOURCE_BARRIER resourceBarrierAfter = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        pCmdList->ResourceBarrier(1, &resourceBarrierAfter);
        return defaultBuffer;
    }

    ID3D12ResourcePtr CreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Alignment = 0;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Flags = flags;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.Height = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.SampleDesc.Quality = 0;
        bufDesc.Width = size;

        ID3D12ResourcePtr pBuffer;
        ThrowIfFailed(pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, initState, nullptr, IID_PPV_ARGS(&pBuffer)));
        return pBuffer;
    }

    EAddMeshInstancesResult AddMeshInstances(const SMeshInstancesDesc& meshInstancesDesc)
    {
        const uint32_t nVertexCount = meshInstancesDesc.m_nVertexCount;
        const uint32_t nIndexCount = meshInstancesDesc.m_nIndexCount;
        bool bHasIndexData = nIndexCount != 0;
        
        if (!bHasIndexData)
        {
            if (nVertexCount % 3 != 0)
            {
                return EAddMeshInstancesResult::INVALID_VERTEX_COUNT;
            }
        }
        else
        {
            if (nIndexCount % 3 != 0)
            {
                return EAddMeshInstancesResult::INVALID_INDEX_COUNT;
            }
        }

        if (meshInstancesDesc.instanes.size() == 0)
        {
            return EAddMeshInstancesResult::INVALID_INSTANCE_INFO_NUM;
        }

        SDXMeshInstanceInfo dxMeshInstanceInfo;
        dxMeshInstanceInfo.m_nIndexStride = meshInstancesDesc.m_nIndexStride;
        dxMeshInstanceInfo.m_nVertexCount = meshInstancesDesc.m_nVertexCount;
        dxMeshInstanceInfo.m_nIndexCount = meshInstancesDesc.m_nIndexCount;
        dxMeshInstanceInfo.instanes = meshInstancesDesc.instanes;

        // create vertex buffer
        dxMeshInstanceInfo.m_pPositionBuffer = CreateDefaultBuffer(meshInstancesDesc.m_pPositionData, nVertexCount * sizeof(Vec3), pRayTracingDX12->AllocUploadBuffer());
        pRayTracingDX12->m_dxMeshInstanceInfos.push_back(dxMeshInstanceInfo);
    }

    // build bottom level acceleration structure
    SAccelerationStructureBuffers BuildBottomLevelAccelerationStructure()
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pRayTracingDX12->m_pCmdList;

        const std::vector<SDXMeshInstanceInfo> dxMeshInstanceInfos = pRayTracingDX12->m_dxMeshInstanceInfos;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDesc;
        geomDesc.resize(dxMeshInstanceInfos.size());

        for (uint32_t i = 0; i < dxMeshInstanceInfos.size(); i++)
        {
            const SDXMeshInstanceInfo& dxMeshInstanceInfo = dxMeshInstanceInfos[i];
            geomDesc[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geomDesc[i].Triangles.VertexBuffer.StartAddress = dxMeshInstanceInfo.m_pPositionBuffer->GetGPUVirtualAddress();
            geomDesc[i].Triangles.VertexBuffer.StrideInBytes = sizeof(Vec3);
            geomDesc[i].Triangles.VertexCount = dxMeshInstanceInfo.m_nVertexCount;
            geomDesc[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geomDesc[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = dxMeshInstanceInfos.size();
        inputs.pGeometryDescs = geomDesc.data();
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
        pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

        ID3D12ResourcePtr pScratch = CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
        ID3D12ResourcePtr pResult = CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
        asDesc.Inputs = inputs;
        asDesc.DestAccelerationStructureData = pResult->GetGPUVirtualAddress();
        asDesc.ScratchAccelerationStructureData = pScratch->GetGPUVirtualAddress();

        pCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = pResult;
        pCmdList->ResourceBarrier(1, &uavBarrier);

        SAccelerationStructureBuffers resultBuffers;
        resultBuffers.pResult = pResult;
        resultBuffers.pScratch = pScratch;
        return resultBuffers;
    }

    D3D12_RAYTRACING_INSTANCE_FLAGS ConvertToDXInstanceFlag(EInstanceFlag instanceFlag)
    {
        switch (instanceFlag)
        {
        case EInstanceFlag::CULL_DISABLE:
            return D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        case EInstanceFlag::FRONTFACE_CCW:
            return D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
        default:
            return D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        }
    }

    SAccelerationStructureBuffers BuildTopLevelAccelerationStructure(ID3D12ResourcePtr pBottomLevelAS)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pRayTracingDX12->m_pCmdList;

        uint32_t totalInstanceNum = 0;
        const std::vector<SDXMeshInstanceInfo> dxMeshInstanceInfos = pRayTracingDX12->m_dxMeshInstanceInfos;
        for (uint32_t i = 0; i < dxMeshInstanceInfos.size(); i++)
        {
            totalInstanceNum += dxMeshInstanceInfos[i].instanes.size();
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = totalInstanceNum;
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
        pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

        ID3D12ResourcePtr pScratch = CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
        ID3D12ResourcePtr pResult = CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.resize(totalInstanceNum);
        for (uint32_t indexMesh = 0; indexMesh < dxMeshInstanceInfos.size(); indexMesh++)
        {
            for (uint32_t indexInstance = 0; indexInstance < dxMeshInstanceInfos[indexMesh].instanes.size(); indexInstance++)
            {
                const SMeshInstanceInfo& meshInstanceInfo = dxMeshInstanceInfos[indexMesh].instanes[indexInstance];
                instanceDescs[indexMesh].InstanceID = 0;                            // This value will be exposed to the shader via InstanceID()
                instanceDescs[indexMesh].InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
                instanceDescs[indexMesh].Flags = ConvertToDXInstanceFlag(meshInstanceInfo.m_instanceFlag);
                memcpy(instanceDescs[indexMesh].Transform, &meshInstanceInfo.m_transform, sizeof(instanceDescs[indexMesh].Transform));
                instanceDescs[indexMesh].AccelerationStructure = pBottomLevelAS->GetGPUVirtualAddress();
                instanceDescs[indexMesh].InstanceMask = 0xFF;
            }
        }

        ID3D12ResourcePtr pInstanceDescBuffer = CreateDefaultBuffer(instanceDescs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalInstanceNum, pRayTracingDX12->AllocUploadBuffer());

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
        asDesc.Inputs = inputs;
        asDesc.Inputs.InstanceDescs = pInstanceDescBuffer->GetGPUVirtualAddress();
        asDesc.DestAccelerationStructureData = pResult->GetGPUVirtualAddress();
        asDesc.ScratchAccelerationStructureData = pScratch->GetGPUVirtualAddress();

        pCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = pResult;
        pCmdList->ResourceBarrier(1, &uavBarrier);

        SAccelerationStructureBuffers resultBuffers;
        resultBuffers.pResult = pResult;
        resultBuffers.pScratch = pScratch;
        resultBuffers.pInstanceDesc = pInstanceDescBuffer;
        return resultBuffers;
    }


    void BuildAccelerationStructure()
    {
        SAccelerationStructureBuffers bottomLevelBuffers = BuildBottomLevelAccelerationStructure();
        SAccelerationStructureBuffers topLevelBuffers = BuildTopLevelAccelerationStructure(bottomLevelBuffers.pResult);
        SubmitCommandList();
        pRayTracingDX12->m_uploadBuffers.clear();
    }

    IDxcBlobPtr CompileLibrary(const std::wstring& filename)
    {
        static const std::wstring currentPath(string_2_wstring(__FILE__));
        std::size_t dirPos = currentPath.find(L"hwrtl_dx12.cpp");

        std::wstring shaderPath = currentPath.substr(0, dirPos) + filename;
        std::ifstream shaderFile(shaderPath);

        if (shaderFile.good() == false)
        {
            ThrowIfFailed(-1); // invalid shader path
        }

        std::stringstream strStream;
        strStream << shaderFile.rdbuf();
        std::string shader = strStream.str();

        IDxcBlobEncodingPtr pTextBlob;
        ThrowIfFailed(pRayTracingDX12->m_pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob));

        IDxcOperationResultPtr pResult;
        ThrowIfFailed(pRayTracingDX12->m_pDxcCompiler->Compile(pTextBlob, shaderPath.data(), L"", L"lib_6_3", nullptr, 0, nullptr, 0, nullptr, &pResult));

        HRESULT resultCode;
        ThrowIfFailed(pResult->GetStatus(&resultCode));
        if (FAILED(resultCode))
        {
            IDxcBlobEncodingPtr pError;
            ThrowIfFailed(pResult->GetErrorBuffer(&pError));
            std::string msg = ConvertBlobToString(pError.GetInterfacePtr());
            std::cerr << msg;
            ThrowIfFailed(-1);
        }

        IDxcBlobPtr pBlob;
        ThrowIfFailed(pResult->GetResult(&pBlob));
        return pBlob;
    }

    ID3D12RootSignaturePtr CreateRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
    {
        ID3DBlobPtr pSigBlob;
        ID3DBlobPtr pErrorBlob;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlob, &pErrorBlob);
        if (FAILED(hr))
        {
            std::string msg = ConvertBlobToString(pErrorBlob.GetInterfacePtr());
            std::cerr << msg;
            ThrowIfFailed(-1);
        }
        ID3D12RootSignaturePtr pRootSig;
        ThrowIfFailed(pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig)));
        return pRootSig;
    }
    
    D3D12_STATE_SUBOBJECT CreateDXILSubObject(ID3DBlobPtr pBlob, const std::vector<std::wstring>& entryPoint)
    {
        std::vector<D3D12_EXPORT_DESC> exportDesc;
        exportDesc.resize(entryPoint.size());
        for (uint32_t i = 0; i < entryPoint.size(); i++)
        {
            exportDesc[i].Name = entryPoint[i].c_str();
            exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
            exportDesc[i].ExportToRename = nullptr;
        }

        D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
        dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
        dxilLibDesc.DXILLibrary.BytecodeLength = pBlob->GetBufferSize();
        dxilLibDesc.NumExports = entryPoint.size();
        dxilLibDesc.pExports = exportDesc.data();

        D3D12_STATE_SUBOBJECT stateSubobject{};
        stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        stateSubobject.pDesc = &dxilLibDesc;
        return stateSubobject;
    }

    D3D12_STATE_SUBOBJECT CreateHitGroupSubObject(LPCWSTR ahsExport, LPCWSTR chsExport)
    {
        D3D12_HIT_GROUP_DESC hitGroupDesc = {};
        hitGroupDesc.AnyHitShaderImport = ahsExport;
        hitGroupDesc.ClosestHitShaderImport = chsExport;
        hitGroupDesc.HitGroupExport = ahsExport ? ahsExport : chsExport;

        D3D12_STATE_SUBOBJECT hitGroupSubObject;
        hitGroupSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitGroupSubObject.pDesc = &hitGroupDesc;
        return hitGroupSubObject;
    }

    D3D12_STATE_SUBOBJECT CreateRootSigSubObject()
    {

    }

    void CreateRTPipelineState(const std::wstring filename,
        std::vector<SShader>rtShaders)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;

        ID3DBlobPtr pDxilLib = CompileLibrary(filename);
        
        std::vector<D3D12_STATE_SUBOBJECT> stateSubObjects;

        std::vector<std::wstring> entryPoints;
        for (auto& rtShader : rtShaders)
        {
            entryPoints.push_back(rtShader.m_entryPoint);
        }

        stateSubObjects.push_back(CreateDXILSubObject(pDxilLib, entryPoints));

        for (auto& rtShader : rtShaders)
        {
            switch (rtShader.m_eShaderType)
            {
            case ERayShaderType::RAY_AHS:
                
                D3D12_STATE_SUBOBJECT hitGroupSubObject = CreateHitGroupSubObject(rtShader.m_entryPoint.data(), nullptr);
                stateSubObjects.push_back(hitGroupSubObject);

                uint32_t totalDesc = rtShader.m_nCBV + rtShader.m_nSRV + rtShader.m_nUAV + rtShader.m_nSampler;
                if (totalDesc > 0)
                {
                    std::vector<D3D12_DESCRIPTOR_RANGE> descRanges;
                    for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
                    {
                        uint32_t* descRangeNum = (uint32_t*)(&rtShader.m_nSRV) + descRangeIndex;
                        if (descRangeNum > 0)
                        {
                            D3D12_DESCRIPTOR_RANGE descRange;
                            descRange.BaseShaderRegister = 0;
                            descRange.NumDescriptors = *descRangeNum;
                            descRange.RegisterSpace = 0;
                            descRange.OffsetInDescriptorsFromTableStart = descRangeIndex;
                            descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                        }
                    }
                    
                    std::vector<D3D12_ROOT_PARAMETER> rootParams;
                    rootParams.resize(1);
                    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rootParams[0].DescriptorTable.NumDescriptorRanges = descRanges.size();
                    rootParams[0].DescriptorTable.pDescriptorRanges = descRanges.data();

                    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
                    rootSigDesc.NumParameters = 1;
                    rootSigDesc.pParameters = rootParams.data();
                    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

                    ID3D12RootSignaturePtr rootSigPtr = CreateRootSignature(pDevice, rootSigDesc);
                    ID3D12RootSignature* pInterface = rootSigPtr.GetInterfacePtr();

                    D3D12_STATE_SUBOBJECT rootSigSubobject = {};
                    rootSigSubobject.pDesc = &pInterface;
                    rootSigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
                    stateSubObjects.push_back(rootSigSubobject);

                    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association = {};
                    association.NumExports = exportCount;
                    association.pExports = rtShader.m_entryPoint.data();
                    association.pSubobjectToAssociate = pSubobjectToAssociate;

                }

                break;
            case ERayShaderType::RAY_CHS:
                D3D12_STATE_SUBOBJECT stateSubObject = CreateHitGroupSubObject(nullptr, rtShader.m_entryPoint.data());
                stateSubObjects.push_back(stateSubObject);
                break;
            }
        }


        D3D12_STATE_OBJECT_DESC desc;
        desc.NumSubobjects = stateSubObjects.size();
        desc.pSubobjects = stateSubObjects.data();
        desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pRayTracingDX12->m_pRtPipelineState)));
    }

    void DestroyScene()
    {

    }

	void Shutdown()
	{
        delete pRayTracingDX12;
	}
}

#endif


