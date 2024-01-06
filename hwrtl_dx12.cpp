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
#include <shlobj.h>
#include <strsafe.h>

#include <locale>
#include <codecvt>

#include <iostream>
#include <vector>
#include <stdexcept>

#define ENABLE_THROW_FAILED_RESULT 1
#define ENABLE_DX12_DEBUG_LAYER 1
#define ENABLE_PIX_FRAME_CAPTURE 1

#if ENABLE_PIX_FRAME_CAPTURE
#include "pix3.h"
#endif

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxcompiler.lib")
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
_COM_SMARTPTR_TYPEDEF(IDxcValidator, __uuidof(IDxcValidator));
_COM_SMARTPTR_TYPEDEF(ID3D12StateObjectProperties, __uuidof(ID3D12StateObjectProperties));
_COM_SMARTPTR_TYPEDEF(ID3D12DescriptorHeap, __uuidof(ID3D12DescriptorHeap));

#define align_to(_alignment, _val) (((_val + _alignment - 1) / _alignment) * _alignment)
static constexpr uint32_t MaxPayloadSizeInBytes = 32 * sizeof(float);
static constexpr uint32_t ShaderTableEntrySize = align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, (D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8));

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

    class CDescManager
    {
    public:
        //D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        ID3D12DescriptorHeapPtr m_pDescHeap;

        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index, ID3D12Device5Ptr pDevice)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_pDescHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * index;
            return cpuHandle;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index, ID3D12Device5Ptr pDevice)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_pDescHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * index;
            return gpuHandle;
        }
    };

    class CResouceManager
    {
    public:
        CResouceManager()
        {
            m_resources.resize(1024);
            m_nextFreeResource.resize(1024);
            for (uint32_t index = 0; index < 1024; index++)
            {
                m_nextFreeResource[index] = index + 1;
            }
            m_currFreeIndex = 0;
        }

        ID3D12ResourcePtr& AllocResource()
        {
            uint32_t unsed_index = 0;
            return AllocResource(unsed_index);
        }

        ID3D12ResourcePtr& AllocResource(uint32_t& allocIndex)
        {
            if (m_nextFreeResource.size() <= m_currFreeIndex)
            {
                m_nextFreeResource.resize((((m_currFreeIndex + 1) / 1024) + 1) * 1024);
            }

            allocIndex = m_currFreeIndex;
            m_currFreeIndex = m_nextFreeResource[m_currFreeIndex];
            return m_resources[allocIndex];
        }

        ID3D12ResourcePtr& GetResource(uint32_t index)
        {
            return m_resources[index];
        }

        void FreeResource(uint32_t index)
        {
            m_resources[index].Release();

            m_nextFreeResource[index] = m_currFreeIndex;
            m_currFreeIndex = index;
        }

    private:
        uint32_t m_currFreeIndex;
        std::vector<ID3D12ResourcePtr>m_resources;
        std::vector<uint32_t>m_nextFreeResource;
    };

    struct CRayTracingDX12
    {
        ID3D12Device5Ptr m_pDevice;;
        ID3D12CommandQueuePtr m_pCmdQueue;
        ID3D12CommandAllocatorPtr m_pCmdAllocator;
        ID3D12GraphicsCommandList4Ptr m_pCmdList;
        ID3D12FencePtr m_pFence;
        IDxcCompilerPtr m_pDxcCompiler;
        IDxcLibraryPtr m_pLibrary;
        IDxcValidatorPtr m_dxcValidator;
        ID3D12StateObjectPtr m_pRtPipelineState;
        ID3D12ResourcePtr m_pShaderTable;
        ID3D12RootSignaturePtr m_pGlobalRootSig;

        HANDLE m_FenceEvent;
        uint64_t m_nFenceValue = 0;;

        std::vector<SDXMeshInstanceInfo>m_dxMeshInstanceInfos;
        CResouceManager m_uploadBuffers;
        CResouceManager m_resources;
        CDescManager m_rtDescManager;

        ID3D12ResourcePtr m_ptlas;
        ID3D12ResourcePtr m_pblas;

        SRayTracingResources m_rtResouces;
        uint32_t m_preSum[4];

#if ENABLE_PIX_FRAME_CAPTURE
        HMODULE m_pixModule;
#endif
    };

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

    std::wstring String2Wstring(const std::string& str)
    {
        std::wstring_convert<std::codecvt_utf8<WCHAR>> cvt;
        std::wstring ws = cvt.from_bytes(str);
        return ws;
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

    ID3D12DescriptorHeapPtr CreateDescriptorHeap(ID3D12Device5Ptr pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = count;
        desc.Type = type;
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        ID3D12DescriptorHeapPtr pHeap;
        ThrowIfFailed(pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
        return pHeap;
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

    void SubmitCommandList(bool bWaitAndReset = true)
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
        if (bWaitAndReset)
        {
            pFence->SetEventOnCompletion(nfenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
            pCmdList->Reset(pCmdAllocator, nullptr);
        }

        pRayTracingDX12->m_uploadBuffers = CResouceManager();
    }

	void Init()
	{
        pRayTracingDX12 = new CRayTracingDX12();

#if ENABLE_PIX_FRAME_CAPTURE
        pRayTracingDX12->m_pixModule = PIXLoadLatestWinPixGpuCapturerLibrary();
#endif

		IDXGIFactory4Ptr pDxgiFactory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));
        
        pRayTracingDX12->m_pDevice = CreateDevice(pDxgiFactory);
        pRayTracingDX12->m_pCmdQueue = CreateCommandQueue(pRayTracingDX12->m_pDevice);

        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pRayTracingDX12->m_pCmdAllocator)));
        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pRayTracingDX12->m_pCmdAllocator, nullptr, IID_PPV_ARGS(&pRayTracingDX12->m_pCmdList)));

        ThrowIfFailed(pRayTracingDX12->m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pRayTracingDX12->m_pFence)));
        pRayTracingDX12->m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        //https://github.com/Wumpf/nvidia-dxr-tutorial/issues/2
        //https://www.wihlidal.com/blog/pipeline/2018-09-16-dxil-signing-post-compile/
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(&pRayTracingDX12->m_dxcValidator)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pRayTracingDX12->m_pDxcCompiler)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pRayTracingDX12->m_pLibrary)));

#if ENABLE_PIX_FRAME_CAPTURE
        PIXCaptureParameters pixCaptureParameters;
        pixCaptureParameters.GpuCaptureParameters.FileName = L"G:/HWRTL/HWRT/cap.wpix";
        PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParameters);
#endif
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
        dxMeshInstanceInfo.m_pPositionBuffer = CreateDefaultBuffer(meshInstancesDesc.m_pPositionData, nVertexCount * sizeof(Vec3), pRayTracingDX12->m_uploadBuffers.AllocResource());
        pRayTracingDX12->m_dxMeshInstanceInfos.emplace_back(dxMeshInstanceInfo);
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

        ID3D12ResourcePtr pInstanceDescBuffer = CreateDefaultBuffer(instanceDescs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalInstanceNum, pRayTracingDX12->m_uploadBuffers.AllocResource());

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
        pRayTracingDX12->m_pblas = bottomLevelBuffers.pResult;
        pRayTracingDX12->m_ptlas = topLevelBuffers.pResult;
        
        SubmitCommandList();
    }

    IDxcBlobPtr CompileLibrary(const std::wstring& filename)
    {
        std::size_t dirPos = String2Wstring(__FILE__).find(L"hwrtl_dx12.cpp");

        std::wstring shaderPath = String2Wstring(__FILE__).substr(0, dirPos) + filename;
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
            std::cout << msg;
            ThrowIfFailed(-1);
        }

        IDxcBlobPtr pBlob;
        ThrowIfFailed(pResult->GetResult(&pBlob));

        IDxcOperationResultPtr pValidResult;
        pRayTracingDX12->m_dxcValidator->Validate(pBlob, DxcValidatorFlags_InPlaceEdit, &pValidResult);

        HRESULT validateStatus;
        pValidResult->GetStatus(&validateStatus);
        if (FAILED(validateStatus))
        {
            ThrowIfFailed(-1);
        }

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
    
    struct SDxilLibrary
    {
        SDxilLibrary(ID3DBlobPtr pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount) : m_pShaderBlob(pBlob)
        {
            m_stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            m_stateSubobject.pDesc = &m_dxilLibDesc;

            m_dxilLibDesc = {};
            m_exportDesc.resize(entryPointCount);
            m_exportName.resize(entryPointCount);
            if (pBlob)
            {
                m_dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
                m_dxilLibDesc.DXILLibrary.BytecodeLength = pBlob->GetBufferSize();
                m_dxilLibDesc.NumExports = entryPointCount;
                m_dxilLibDesc.pExports = m_exportDesc.data();

                for (uint32_t i = 0; i < entryPointCount; i++)
                {
                    m_exportName[i] = entryPoint[i];
                    m_exportDesc[i].Name = m_exportName[i].c_str();
                    m_exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
                    m_exportDesc[i].ExportToRename = nullptr;
                }
            }
        };

        SDxilLibrary() : SDxilLibrary(nullptr, nullptr, 0) {}

        D3D12_DXIL_LIBRARY_DESC m_dxilLibDesc = {};
        D3D12_STATE_SUBOBJECT m_stateSubobject{};
        ID3DBlobPtr m_pShaderBlob;
        std::vector<D3D12_EXPORT_DESC> m_exportDesc;
        std::vector<std::wstring> m_exportName;
    };

    struct SHitProgram
    {
        SHitProgram() {};
        SHitProgram(LPCWSTR ahsExport, LPCWSTR chsExport, LPCWSTR hgExport)
        {
            m_desc = {};
            m_desc.AnyHitShaderImport = ahsExport;
            m_desc.ClosestHitShaderImport = chsExport;
            m_desc.HitGroupExport = hgExport;

            m_subObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            m_subObject.pDesc = &m_desc;
        }

        D3D12_HIT_GROUP_DESC m_desc = {};
        D3D12_STATE_SUBOBJECT m_subObject = {};
    };

    struct SExportAssociation
    {
        SExportAssociation() {};
        SExportAssociation(const WCHAR* exportNames[], uint32_t exportCount, const D3D12_STATE_SUBOBJECT* pSubobjectToAssociate)
        {
            m_association.NumExports = exportCount;
            m_association.pExports = exportNames;
            m_association.pSubobjectToAssociate = pSubobjectToAssociate;

            m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            m_subobject.pDesc = &m_association;
        }

        D3D12_STATE_SUBOBJECT m_subobject = {};
        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION m_association = {};
    };

    struct SPipelineConfig
    {
        SPipelineConfig() {};
        SPipelineConfig(uint32_t maxTraceRecursionDepth)
        {
            m_config.MaxTraceRecursionDepth = maxTraceRecursionDepth;

            m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            m_subobject.pDesc = &m_config;
        }

        D3D12_RAYTRACING_PIPELINE_CONFIG m_config = {};
        D3D12_STATE_SUBOBJECT m_subobject = {};
    };

    struct SGlobalRootSignature
    {
        SGlobalRootSignature() {};
        SGlobalRootSignature(ID3D12Device5Ptr pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
        {
            m_pRootSig = CreateRootSignature(pDevice, desc);
            m_pInterface = m_pRootSig.GetInterfacePtr();
            m_subobject.pDesc = &m_pInterface;
            m_subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        }
        ID3D12RootSignaturePtr m_pRootSig;
        ID3D12RootSignature* m_pInterface = nullptr;
        D3D12_STATE_SUBOBJECT m_subobject = {};
    };

    void CreateRTPipelineStateAndShaderTable(const std::wstring filename,
        std::vector<SShader>rtShaders,
        uint32_t maxTraceRecursionDepth,
        SRayTracingResources rayTracingResources)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        pRayTracingDX12->m_rtResouces = rayTracingResources;

        pRayTracingDX12->m_preSum[0] = 0;
        for (uint32_t index = 1; index < 4; index++)
        {
            pRayTracingDX12->m_preSum[index] = pRayTracingDX12->m_preSum[index - 1] + pRayTracingDX12->m_rtResouces[index];
        }

        std::vector<LPCWSTR>pEntryPoint;
        std::vector<std::wstring>pHitGroupExports;
        uint32_t hitProgramNum = 0;
        
        for (auto& rtShader : rtShaders)
        {
            pEntryPoint.emplace_back(rtShader.m_entryPoint.c_str());
            switch (rtShader.m_eShaderType)
            {
            case ERayShaderType::RAY_AHS:
            case ERayShaderType::RAY_CHS:
                hitProgramNum++;
                pHitGroupExports.emplace_back(std::wstring(rtShader.m_entryPoint + std::wstring(L"Export")));
                break;
            };
        }

        uint32_t subObjectsNum = 1 + hitProgramNum + 2 + 1 + 1; // dxil subobj + hit program number + shader config * 2 + pipeline config + global root signature * 1

        std::vector<D3D12_STATE_SUBOBJECT> stateSubObjects;
        std::vector<SHitProgram>hitProgramSubObjects;
        hitProgramSubObjects.resize(hitProgramNum);
        stateSubObjects.resize(subObjectsNum);

        uint32_t hitProgramIndex = 0;
        uint32_t subObjectsIndex = 0;

        // create dxil subobjects
        ID3DBlobPtr pDxilLib = CompileLibrary(filename);
        SDxilLibrary dxilLib(pDxilLib, pEntryPoint.data(), pEntryPoint.size());
        stateSubObjects[subObjectsIndex] = dxilLib.m_stateSubobject;
        subObjectsIndex++;

        // create root signatures and export asscociation
        std::vector<const WCHAR*> emptyRootSigEntryPoints;
        for (uint32_t index = 0; index < rtShaders.size(); index++)
        {
            const SShader& rtShader = rtShaders[index];
            switch (rtShader.m_eShaderType)
            {
            case ERayShaderType::RAY_AHS:
                hitProgramSubObjects[hitProgramIndex] = SHitProgram(rtShader.m_entryPoint.data(), nullptr, pHitGroupExports[hitProgramIndex].c_str());
                stateSubObjects[subObjectsIndex++] = hitProgramSubObjects[hitProgramIndex].m_subObject;
                hitProgramIndex++;
                break;
            case ERayShaderType::RAY_CHS:
                hitProgramSubObjects[hitProgramIndex] = SHitProgram(nullptr, rtShader.m_entryPoint.data(), pHitGroupExports[hitProgramIndex].c_str());
                stateSubObjects[subObjectsIndex++] = hitProgramSubObjects[hitProgramIndex].m_subObject;
                hitProgramIndex++;
                break;
            }
        }

        // shader config subobject and export associations
        SExportAssociation sgExportAssociation;
        {
            D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
            shaderConfig.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
            shaderConfig.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
            D3D12_STATE_SUBOBJECT configSubobject = {};
            configSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            configSubobject.pDesc = &shaderConfig;
            stateSubObjects[subObjectsIndex] = configSubobject;
            sgExportAssociation = SExportAssociation(pEntryPoint.data(), pEntryPoint.size(), &stateSubObjects[subObjectsIndex++]);
            stateSubObjects[subObjectsIndex++] = sgExportAssociation.m_subobject;
        }

        // pipeline config
        {
            SPipelineConfig pipelineConfig(maxTraceRecursionDepth);
            stateSubObjects[subObjectsIndex] = pipelineConfig.m_subobject;
            subObjectsIndex++;
        }

        // global root signature
        SGlobalRootSignature globalRootSignature;
        {
            std::vector<D3D12_DESCRIPTOR_RANGE> descRanges;
            for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
            {
                if (rayTracingResources[descRangeIndex] > 0)
                {
                    D3D12_DESCRIPTOR_RANGE descRange;
                    descRange.BaseShaderRegister = 0;
                    descRange.NumDescriptors = rayTracingResources[descRangeIndex];
                    descRange.RegisterSpace = 0;
                    descRange.OffsetInDescriptorsFromTableStart = descRangeIndex;
                    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                    descRanges.emplace_back(descRange);
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
            rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            globalRootSignature = SGlobalRootSignature(pDevice, rootSigDesc);
            stateSubObjects[subObjectsIndex] = globalRootSignature.m_subobject;
            subObjectsIndex++;
        }

        D3D12_STATE_OBJECT_DESC desc;
        desc.NumSubobjects = stateSubObjects.size();
        desc.pSubobjects = stateSubObjects.data();
        desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        
        pRayTracingDX12->m_pGlobalRootSig = globalRootSignature.m_pRootSig;
        
        ThrowIfFailed(pDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pRayTracingDX12->m_pRtPipelineState)));

        // create desc heap
        pRayTracingDX12->m_rtDescManager.m_pDescHeap = CreateDescriptorHeap(pDevice, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

        // create shader table
        uint32_t shaderTableSize = ShaderTableEntrySize * rtShaders.size();

        std::vector<char>shaderTableData;
        shaderTableData.resize(shaderTableSize);

        ID3D12StateObjectPropertiesPtr pRtsoProps;
        pRayTracingDX12->m_pRtPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

        uint32_t shaderTableHGindex = 0;
        for (uint32_t index = 0; index < rtShaders.size(); index++)
        {
            const SShader& rtShader = rtShaders[index];
            switch (rtShader.m_eShaderType)
            {
            case ERayShaderType::RAY_AHS:
            case ERayShaderType::RAY_CHS:
                memcpy(shaderTableData.data() + index * ShaderTableEntrySize, pRtsoProps->GetShaderIdentifier(pHitGroupExports[shaderTableHGindex].c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
                shaderTableHGindex++;
                break;
            default:
                memcpy(shaderTableData.data() + index * ShaderTableEntrySize, pRtsoProps->GetShaderIdentifier(rtShader.m_entryPoint.c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
                break;
            };

            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = pRayTracingDX12->m_rtDescManager.GetGPUHandle(0, pDevice);
            memcpy(shaderTableData.data() + index * ShaderTableEntrySize + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, &gpuHandle, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
        }

        pRayTracingDX12->m_pShaderTable = CreateDefaultBuffer(shaderTableData.data(), shaderTableSize, pRayTracingDX12->m_uploadBuffers.AllocResource());
        SubmitCommandList();
    }

    D3D12_RESOURCE_FLAGS ConvertUsageToDxFlag(ETexUsage eTexUsage)
    {
        switch (eTexUsage)
        {
        case ETexUsage::USAGE_SRV:
            return D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            break;
        case ETexUsage::USAGE_UAV:
            return D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            break;
        }
        return D3D12_RESOURCE_FLAG_NONE;
    }

    DXGI_FORMAT ConvertFormatToDxFormat(ETexFormat eTexFormat)
    {
        switch (eTexFormat)
        {
        case ETexFormat::FT_RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    bool ValidateResource()
    {
        return true;
    }

    SResourceHandle CreateTexture2D(STextureCreateDesc texCreateDesc)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.DepthOrArraySize = 1;
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Format = ConvertFormatToDxFormat(texCreateDesc.m_eTexFormat);
        resDesc.Flags = ConvertUsageToDxFlag(texCreateDesc.m_eTexUsage);
        resDesc.Width = texCreateDesc.m_width;
        resDesc.Height = texCreateDesc.m_height;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;

        //todo resouce state track

        uint32_t allocIndex;
        ThrowIfFailed(pDevice->CreateCommittedResource(&defaultHeapProperies, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pRayTracingDX12->m_resources.AllocResource(allocIndex))));
        return SResourceHandle(allocIndex);
    }

    void SetShaderResource(SResourceHandle resource, ESlotType slotType, uint32_t bindIndex)
    {
        ID3D12ResourcePtr pResouce = pRayTracingDX12->m_resources.GetResource(resource);
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;

        switch (slotType)
        {
        case ESlotType::ST_U:
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pRayTracingDX12->m_rtDescManager.GetCPUHandle(bindIndex + pRayTracingDX12->m_preSum[uint32_t(ESlotType::ST_U)], pDevice);
            pDevice->CreateUnorderedAccessView(pResouce, nullptr, &uavDesc, cpuHandle);
            break;
        }

        ValidateResource();
    }

    void SetTLAS(uint32_t bindIndex)
    {
        ID3D12Device5Ptr pDevice = pRayTracingDX12->m_pDevice;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = pRayTracingDX12->m_ptlas->GetGPUVirtualAddress();
        pDevice->CreateShaderResourceView(nullptr, &srvDesc, pRayTracingDX12->m_rtDescManager.GetCPUHandle(bindIndex + pRayTracingDX12->m_preSum[uint32_t(ESlotType::ST_T)], pDevice));
        ValidateResource();
    }

    void BeginRayTracing()
    {
        ID3D12DescriptorHeap* heaps[] = { pRayTracingDX12->m_rtDescManager.m_pDescHeap };
        pRayTracingDX12->m_pCmdList->SetDescriptorHeaps(1, heaps);
    }

    void DispatchRayTracicing(uint32_t width, uint32_t height)
    {

        ID3D12GraphicsCommandList4Ptr pCmdList = pRayTracingDX12->m_pCmdList;

        D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
        raytraceDesc.Width = width;
        raytraceDesc.Height = height;
        raytraceDesc.Depth = 1;

        // RayGen is the first entry in the shader-table
        raytraceDesc.RayGenerationShaderRecord.StartAddress = pRayTracingDX12->m_pShaderTable->GetGPUVirtualAddress() + 0 * ShaderTableEntrySize;
        raytraceDesc.RayGenerationShaderRecord.SizeInBytes = ShaderTableEntrySize;

        // Miss is the second entry in the shader-table
        size_t missOffset = 1 * ShaderTableEntrySize;
        raytraceDesc.MissShaderTable.StartAddress = pRayTracingDX12->m_pShaderTable->GetGPUVirtualAddress() + missOffset;
        raytraceDesc.MissShaderTable.StrideInBytes = ShaderTableEntrySize;
        raytraceDesc.MissShaderTable.SizeInBytes = ShaderTableEntrySize;   // Only a s single miss-entry

        // Hit is the third entry in the shader-table
        size_t hitOffset = 2 * ShaderTableEntrySize;
        raytraceDesc.HitGroupTable.StartAddress = pRayTracingDX12->m_pShaderTable->GetGPUVirtualAddress() + hitOffset;
        raytraceDesc.HitGroupTable.StrideInBytes = ShaderTableEntrySize;
        raytraceDesc.HitGroupTable.SizeInBytes = ShaderTableEntrySize; //todo: multipie number

        pCmdList->SetComputeRootSignature(pRayTracingDX12->m_pGlobalRootSig);

        // Dispatch
        pCmdList->SetPipelineState1(pRayTracingDX12->m_pRtPipelineState);
        pCmdList->DispatchRays(&raytraceDesc);
        SubmitCommandList();
    }

    void DestroyScene()
    {
    }

	void Shutdown()
	{
#if ENABLE_PIX_FRAME_CAPTURE
        PIXEndCapture(false);
#endif
        delete pRayTracingDX12;
	}
}

#endif


