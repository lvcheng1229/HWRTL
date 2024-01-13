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
// Enable pix gpu capture:
//      step 1. add WinPixEventRuntime to your application https://devblogs.microsoft.com/pix/programmatic-capture/ 
//      step 2. set ENABLE_PIX_FRAME_CAPTURE to 1 
//      step 3. run program
//      step 4. open the gpu capture located in HWRT/pix.wpix
// 
// TODO:
//      1. descriptor manager
//      2. resource state track
//      3. command cache
//      4. CDXDescManager SoA -> AoS
//      5. remove temp buffers
//      6. SMeshInstancesDesc: remove pPositionBufferData?
//      7. log sysytem
//      8. gpu memory manager: seg list and buddy allocator for cb and tex
//      9. split root parameter table to 4 tables
//      10. d3d view abstraction
//      11. release temporary upload buffer
//      12. reset command list
//      13. ERayShaderType to ERayShaderType and EShaderType
//      14. SetShaderResource support raster rization


#include "HWRTL.h"
#if ENABLE_DX12_WIN

#define ENABLE_THROW_FAILED_RESULT 1
#define ENABLE_DX12_DEBUG_LAYER 1
#define ENABLE_PIX_FRAME_CAPTURE 1

//dx12 headers
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>

// windows headers
#include <comdef.h>
#include <Windows.h>
#include <shlobj.h>
#include <strsafe.h>
#include <d3dcompiler.h>
#if ENABLE_PIX_FRAME_CAPTURE
#include "pix3.h"
#endif

// heaper function
#include "d3dx12.h"

// other headers
#include <iostream>
#include <vector>
#include <stdexcept>


#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxcompiler.lib")
#pragma comment(lib,"D3D12.lib")
#pragma comment(lib,"dxgi.lib")

#define MAKE_SMART_COM_PTR(_a) _COM_SMARTPTR_TYPEDEF(_a, __uuidof(_a))

MAKE_SMART_COM_PTR(IDXGIFactory4);
MAKE_SMART_COM_PTR(ID3D12Device5);
MAKE_SMART_COM_PTR(IDXGIAdapter1);
MAKE_SMART_COM_PTR(ID3D12Debug);
MAKE_SMART_COM_PTR(ID3D12CommandQueue);
MAKE_SMART_COM_PTR(ID3D12CommandAllocator);
MAKE_SMART_COM_PTR(ID3D12GraphicsCommandList4);
MAKE_SMART_COM_PTR(ID3D12Resource);
MAKE_SMART_COM_PTR(ID3D12Fence);
MAKE_SMART_COM_PTR(IDxcBlobEncoding);
MAKE_SMART_COM_PTR(IDxcCompiler);
MAKE_SMART_COM_PTR(IDxcLibrary);
MAKE_SMART_COM_PTR(IDxcOperationResult);
MAKE_SMART_COM_PTR(IDxcBlob);
MAKE_SMART_COM_PTR(ID3DBlob);
MAKE_SMART_COM_PTR(ID3D12StateObject);
MAKE_SMART_COM_PTR(ID3D12RootSignature);
MAKE_SMART_COM_PTR(IDxcValidator);
MAKE_SMART_COM_PTR(ID3D12StateObjectProperties);
MAKE_SMART_COM_PTR(ID3D12DescriptorHeap);
MAKE_SMART_COM_PTR(ID3D12PipelineState);



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

    struct SDXAccelerationStructureBuffers
    {
        ID3D12ResourcePtr pScratch;
        ID3D12ResourcePtr pResult;
        ID3D12ResourcePtr pInstanceDesc;
    };


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

    class CDXDescManager
    {
    public :
        CDXDescManager() {};

        void Init(ID3D12Device5Ptr pDevice, uint32_t size, D3D12_DESCRIPTOR_HEAP_TYPE descHeapType, bool shaderVisible)
        {
            m_pDevice = pDevice;
            m_pDescHeap = CreateDescriptorHeap(pDevice, size, descHeapType, shaderVisible);
            m_descHeapType = descHeapType;

            m_nextFreeDescIndex.resize(size);
            for (uint32_t index = 0; index < size; index++)
            {
                m_nextFreeDescIndex[index] = index + 1;
            }
        }

        const uint32_t GetNumdDesc()
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = m_pDescHeap->GetDesc();
            return heapDesc.NumDescriptors;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_pDescHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += m_pDevice->GetDescriptorHandleIncrementSize(m_descHeapType) * index;
            return cpuHandle;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_pDescHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += m_pDevice->GetDescriptorHandleIncrementSize(m_descHeapType) * index;
            return gpuHandle;
        }

        ID3D12DescriptorHeapPtr GetHeap()
        {
            return m_pDescHeap;
        }

        uint32_t AllocDesc()
        {
            if (m_nextFreeDescIndex.size() <= m_currFreeIndex)
            {
                m_nextFreeDescIndex.resize((((m_currFreeIndex + 1) / 1024) + 1) * 1024);
                for (uint32_t index = m_currFreeIndex; index < m_nextFreeDescIndex.size(); index++)
                {
                    m_nextFreeDescIndex[index] = index + 1;
                }
            }

            uint32_t allocIndex = m_currFreeIndex;
            m_currFreeIndex = m_nextFreeDescIndex[m_currFreeIndex];
            return allocIndex;
        }

        void FreeDesc(uint32_t freeIndex)
        {
            m_nextFreeDescIndex[freeIndex] = m_currFreeIndex;
            m_currFreeIndex = freeIndex;
        }

    private:
        uint32_t m_currFreeIndex;
        std::vector<uint32_t>m_nextFreeDescIndex;

        ID3D12DescriptorHeapPtr m_pDescHeap;
        D3D12_DESCRIPTOR_HEAP_TYPE m_descHeapType;

        ID3D12Device5Ptr m_pDevice;
    };

    class CDXPipelineDescManager
    {
    public:
    private:
        ID3D12DescriptorHeapPtr m_pDescHeap;
        D3D12_DESCRIPTOR_HEAP_TYPE m_descHeapType;
        ID3D12Device5Ptr m_pDevice;
    };

    class CDXResouceManager
    {
    public:
        CDXResouceManager()
        {
            m_resources.resize(1024);
            m_nextFreeResource.resize(1024);
            rtvIndices.resize(1024);
            csuIndices.resize(1024);
            m_vbViews.resize(1024);
            m_resouceStates.resize(1024);
            for (uint32_t index = 0; index < 1024; index++)
            {
                m_nextFreeResource[index] = index + 1;
                rtvIndices[index] = -1;
                csuIndices[index] = -1;
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
                uint32_t newSize = (((m_currFreeIndex + 1) / 1024) + 1) * 1024;
                m_nextFreeResource.resize(newSize);
                rtvIndices.resize(newSize);
                csuIndices.resize(newSize);
                m_vbViews.resize(newSize);
                m_resouceStates.resize(newSize);
                for (uint32_t index = m_currFreeIndex; index < m_nextFreeResource.size(); index++)
                {
                    m_nextFreeResource[index] = index + 1;
                    rtvIndices[index] = -1;
                    csuIndices[index] = -1;
                }
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

        D3D12_RESOURCE_STATES& GetResouceCurrentState(uint32_t index)
        {
            return m_resouceStates[index];
        }

        void SetResouceCurrentState(uint32_t index, D3D12_RESOURCE_STATES resouceState)
        {
            m_resouceStates[index] = resouceState;
        }

        void SetRTVIndex(uint32_t resouceIndex,uint32_t rtvIndex)
        {
            rtvIndices[resouceIndex] = rtvIndex;
        }

        uint32_t GetRTVIndex(uint32_t resouceIndex)
        {
            return rtvIndices[resouceIndex];
        }

        void SetCSUIndex(uint32_t resouceIndex, uint32_t csuIndex)
        {
            csuIndices[resouceIndex] = csuIndex;
        }

        uint32_t GetCSUIndex(uint32_t resouceIndex)
        {
            return csuIndices[resouceIndex];
        }

        void SetVertexBufferView(uint32_t resouceIndex, D3D12_VERTEX_BUFFER_VIEW vbView)
        {
            m_vbViews[resouceIndex] = vbView;
        }

        D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView(uint32_t resouceIndex)
        {
            return m_vbViews[resouceIndex];
        }
    private:
        uint32_t m_currFreeIndex;
        std::vector<D3D12_RESOURCE_STATES> m_resouceStates;
        std::vector<ID3D12ResourcePtr>m_resources;
        std::vector<uint32_t>m_nextFreeResource;
        std::vector<int>rtvIndices;
        std::vector<int>csuIndices;
        std::vector<D3D12_VERTEX_BUFFER_VIEW>m_vbViews;
    };

    class CDxGraphicsPipelineState : public CGraphicsPipelineState
    {
    public:
        ID3D12RootSignaturePtr m_pRsGlobalRootSig;
        ID3D12PipelineStatePtr m_pRSPipelinState;

        CDxGraphicsPipelineState(ID3D12RootSignaturePtr rsGlobalRootSig, ID3D12PipelineStatePtr rsPipelinState)
            :m_pRsGlobalRootSig(rsGlobalRootSig)
            , m_pRSPipelinState(rsPipelinState)
        {

        }
    };

    struct SRayTracingInitDesc
    {
        uint32_t m_nShaderNum[4];
    };

    class CDxRayTracingPipelineState : public CRayTracingPipelineState
    {
    public:

        uint32_t m_nShaderNum[4];

        CDxRayTracingPipelineState(SRayTracingInitDesc rtInitDesc)
        {
            m_nShaderNum[0] = rtInitDesc.m_nShaderNum[0];
            m_nShaderNum[1] = rtInitDesc.m_nShaderNum[1];
            m_nShaderNum[2] = rtInitDesc.m_nShaderNum[2];
            m_nShaderNum[3] = rtInitDesc.m_nShaderNum[3];
        }
    };

    class CDXDevice
    {
    public:
        ID3D12Device5Ptr m_pDevice;
        ID3D12GraphicsCommandList4Ptr m_pCmdList;
        ID3D12CommandQueuePtr m_pCmdQueue;
        ID3D12CommandAllocatorPtr m_pCmdAllocator;
        ID3D12FencePtr m_pFence;
        HANDLE m_FenceEvent;
        uint64_t m_nFenceValue = 0;;

        CDXResouceManager m_tempBuffers;

        CDXDescManager m_rtvDescManager;
        CDXDescManager m_dsvDescManager;
        CDXDescManager m_csuDescManager;//cbv srv uav 

        CDXResouceManager m_resouManager;
        std::vector<D3D12_RESOURCE_BARRIER> m_resouceBarriers;
    };

    class SHandleManager
    {
    public:
        void Set(D3D12_CPU_DESCRIPTOR_HANDLE handle, uint32_t index)
        {
            m_handles[index] = handle;
            m_nHandles = m_nHandles < index ? index : m_nHandles;
        }
        uint32_t GetNum()
        {
            return m_nHandles;
        }
    private:
        uint32_t m_nHandles = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE m_handles[16];
    };

    class CDXPassDescManager
    {
    public:
        CDXPassDescManager()
            :m_nCurrentStartSlotIndex(0)
        {
        }

        void Init(ID3D12Device5Ptr pDevice)
        {
            m_pDevice = pDevice;
            m_pDescHeap = CreateDescriptorHeap(pDevice, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

            m_hCpuBegin = m_pDescHeap->GetCPUDescriptorHandleForHeapStart();
            m_hGpuBegin = m_pDescHeap->GetGPUDescriptorHandleForHeapStart();

            m_nElemSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index)
        {
            return D3D12_CPU_DESCRIPTOR_HANDLE{ m_hCpuBegin.ptr + static_cast<UINT64>(index * m_nElemSize) };
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index)
        {
            return D3D12_GPU_DESCRIPTOR_HANDLE{ m_hGpuBegin.ptr + static_cast<UINT64>(index * m_nElemSize) };
        }

        uint32_t GetAndAddCurrentPassSlotStart(uint32_t numSlotUsed)
        {
            uint32_t returnValue = m_nCurrentStartSlotIndex;
            m_nCurrentStartSlotIndex += numSlotUsed;
            return returnValue;
        }

        void ResetPassSlotStartIndex()
        {
            m_nCurrentStartSlotIndex = 0;
        }

        ID3D12DescriptorHeapPtr GetHeapPtr()
        {
            return m_pDescHeap;
        }

    private:
        ID3D12DescriptorHeapPtr m_pDescHeap;
        ID3D12Device5Ptr m_pDevice;

        D3D12_CPU_DESCRIPTOR_HANDLE m_hCpuBegin;
        D3D12_GPU_DESCRIPTOR_HANDLE m_hGpuBegin;
        
        uint32_t m_nCurrentStartSlotIndex;
        uint32_t m_nElemSize;
    };

    class CDxPassHandleManager
    {
    public:
        CDxPassHandleManager()
        {
            m_nDescNum[0] = m_nDescNum[1] = m_nDescNum[2] = m_nDescNum[3] = 0;
        }

        void SetCPUHandle(uint32_t index, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, ESlotType slotType)
        {
            m_csuHandle[uint32_t(slotType)][index] = cpuHandle;
            m_nDescNum[uint32_t(slotType)] = m_nDescNum[uint32_t(slotType)] < (index + 1) ? (index + 1) : m_nDescNum[uint32_t(slotType)];
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index, ESlotType slotType)
        {
            return m_csuHandle[uint32_t(slotType)][index];
        }

        uint32_t GetSlotNum(ESlotType slotType)
        {
            return m_nDescNum[uint32_t(slotType)];
        }

        void Reset()
        {
            //TODO
        }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_csuHandle[4][32];    //TODO
        uint32_t m_nDescNum[4];    //TODO
    };


    struct SGraphicsContext
    {
        CDxPassHandleManager m_dxPassHandleManager;
        CDXPassDescManager m_dxPassDescManager;
    };

    class CDXRasterization
    {
    public:
        SGraphicsContext m_graphicsContext;

        std::vector<D3D12_VERTEX_BUFFER_VIEW>m_vbViews;

        D3D12_CPU_DESCRIPTOR_HANDLE m_renderTargetHandles[8];
        uint32_t m_nRenderTargetNum = 0;

        D3D12_VIEWPORT m_viewPort;
        D3D12_RECT m_scissorRect;
    };

    class CDXRayTracing
    {
    public:
        IDxcCompilerPtr m_pDxcCompiler;
        IDxcLibraryPtr m_pLibrary;
        IDxcValidatorPtr m_dxcValidator;
        ID3D12StateObjectPtr m_pRtPipelineState;
        ID3D12ResourcePtr m_pShaderTable;
        ID3D12RootSignaturePtr m_pGlobalRootSig;

        std::vector<SDXMeshInstanceInfo>m_dxMeshInstanceInfos;
        
        std::vector<ID3D12ResourcePtr>m_pBLASs;

        CDXDescManager m_rtDescManager;

        ID3D12ResourcePtr m_ptlas;

        SShaderResources m_rtResouces;
        uint32_t m_rangeIndex[4];

#if ENABLE_PIX_FRAME_CAPTURE
        HMODULE m_pixModule;
#endif
    };

    static CDXDevice* pDXDevice = nullptr;
    static CDXRasterization* pDXRasterization = nullptr;
    static CDXRayTracing* pDXRayTracing = nullptr;

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

    //void SubmitCommandList(bool bWaitAndReset = true)
    //{
    //    ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
    //    ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
    //    ID3D12CommandQueuePtr pCmdQueue = pDXDevice->m_pCmdQueue;
    //    ID3D12CommandAllocatorPtr pCmdAllocator = pDXDevice->m_pCmdAllocator;
    //    ID3D12FencePtr pFence = pDXDevice->m_pFence;
    //    
    //    uint64_t& nfenceValue = pDXDevice->m_nFenceValue;
    //    HANDLE& fenceEvent = pDXDevice->m_FenceEvent;
    //
    //    pCmdList->Close();
    //    ID3D12CommandList* pGraphicsList = pCmdList.GetInterfacePtr();
    //    pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
    //    nfenceValue++;
    //    pCmdQueue->Signal(pFence, nfenceValue);
    //    if (bWaitAndReset)
    //    {
    //        pFence->SetEventOnCompletion(nfenceValue, fenceEvent);
    //        WaitForSingleObject(fenceEvent, INFINITE);
    //        pCmdList->Reset(pCmdAllocator, nullptr);
    //    }
    //    pDXDevice->m_resouceBarriers.clear();
    //    pDXDevice->m_tempBuffers = CDXResouceManager();
    //}

	void hwrtl::Init()
	{
        pDXDevice = new CDXDevice();
        pDXRasterization = new CDXRasterization();
        pDXRayTracing = new CDXRayTracing();

#if ENABLE_PIX_FRAME_CAPTURE
        pDXRayTracing->m_pixModule = PIXLoadLatestWinPixGpuCapturerLibrary();
#endif

		IDXGIFactory4Ptr pDxgiFactory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));
        
        pDXDevice->m_pDevice = CreateDevice(pDxgiFactory);
        pDXDevice->m_pCmdQueue = CreateCommandQueue(pDXDevice->m_pDevice);

        // create desc heap
        pDXRayTracing->m_rtDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
        pDXDevice->m_rtvDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
        pDXDevice->m_csuDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
        pDXDevice->m_dsvDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
        
        pDXRasterization->m_graphicsContext.m_dxPassDescManager.Init(pDXDevice->m_pDevice);

        ThrowIfFailed(pDXDevice->m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pDXDevice->m_pCmdAllocator)));
        ThrowIfFailed(pDXDevice->m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pDXDevice->m_pCmdAllocator, nullptr, IID_PPV_ARGS(&pDXDevice->m_pCmdList)));

        ThrowIfFailed(pDXDevice->m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pDXDevice->m_pFence)));
        pDXDevice->m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        //https://github.com/Wumpf/nvidia-dxr-tutorial/issues/2
        //https://www.wihlidal.com/blog/pipeline/2018-09-16-dxil-signing-post-compile/
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(&pDXRayTracing->m_dxcValidator)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pDXRayTracing->m_pDxcCompiler)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pDXRayTracing->m_pLibrary)));

#if ENABLE_PIX_FRAME_CAPTURE
        std::size_t dirPos = WstringConverter().from_bytes(__FILE__).find(L"hwrtl_dx12.cpp");
        std::wstring pixPath = WstringConverter().from_bytes(__FILE__).substr(0, dirPos) + L"HWRT\\pix.wpix";

        PIXCaptureParameters pixCaptureParameters;
        pixCaptureParameters.GpuCaptureParameters.FileName = pixPath.c_str();
        PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParameters);
#endif
	}

    static CD3DX12_HEAP_PROPERTIES defaultHeapProperies(D3D12_HEAP_TYPE_DEFAULT);
    static CD3DX12_HEAP_PROPERTIES uploadHeapProperies(D3D12_HEAP_TYPE_UPLOAD);

    ID3D12ResourcePtr CreateDefaultBuffer(const void* pInitData, UINT64 nByteSize, ID3D12ResourcePtr& pUploadBuffer)
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;

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

    ID3D12ResourcePtr DXCreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;

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

    EAddMeshInstancesResult AddRayTracingMeshInstances(const SMeshInstancesDesc& meshInstancesDesc,SResourceHandle vbResouce)
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
        auto& vbBuffer = pDXDevice->m_resouManager.GetResource(vbResouce);

        dxMeshInstanceInfo.m_pPositionBuffer = vbBuffer;
        pDXRayTracing->m_dxMeshInstanceInfos.emplace_back(dxMeshInstanceInfo);

        return EAddMeshInstancesResult::SUCESS;
    }

    // build bottom level acceleration structure
    void BuildBottomLevelAccelerationStructure()
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));

        const std::vector<SDXMeshInstanceInfo> dxMeshInstanceInfos = pDXRayTracing->m_dxMeshInstanceInfos;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs ;
        std::vector<ID3D12ResourcePtr>pScratchSource;
        std::vector<ID3D12ResourcePtr>pResultSource;
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>buildDescs;

        for (uint32_t i = 0; i < dxMeshInstanceInfos.size(); i++)
        {
            D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
            const SDXMeshInstanceInfo& dxMeshInstanceInfo = dxMeshInstanceInfos[i];
            geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geomDesc.Triangles.VertexBuffer.StartAddress = dxMeshInstanceInfo.m_pPositionBuffer->GetGPUVirtualAddress();
            geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vec3);
            geomDesc.Triangles.VertexCount = dxMeshInstanceInfo.m_nVertexCount;
            geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geomDesc.Triangles.IndexCount = 0;
            geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            geomDescs.push_back(geomDesc);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = &geomDescs[i];
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
            pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

            ID3D12ResourcePtr pScratch = DXCreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
            ID3D12ResourcePtr pResult = DXCreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
            asDesc.Inputs = inputs;
            asDesc.DestAccelerationStructureData = pResult->GetGPUVirtualAddress();
            asDesc.ScratchAccelerationStructureData = pScratch->GetGPUVirtualAddress();

            buildDescs.push_back(asDesc);
            pDXRayTracing->m_pBLASs.push_back(pResult);
            pScratchSource.push_back(pScratch);
            pResultSource.push_back(pResult);

            pCmdList->BuildRaytracingAccelerationStructure(&buildDescs[i], 0, nullptr);
        }

        std::vector<D3D12_RESOURCE_BARRIER>uavBarriers;
        uavBarriers.resize(pDXRayTracing->m_pBLASs.size());
        for (uint32_t index = 0; index < pDXRayTracing->m_pBLASs.size(); index++)
        {
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = pDXRayTracing->m_pBLASs[index];
            uavBarriers[index] = uavBarrier;
        }
        
        pCmdList->ResourceBarrier(uavBarriers.size(), uavBarriers.data());

        SubmitCommandlist();
        WaitForPreviousFrame();
        ResetCmdList();
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

    SDXAccelerationStructureBuffers BuildTopLevelAccelerationStructure()
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));

        uint32_t totalInstanceNum = 0;
        const std::vector<SDXMeshInstanceInfo> dxMeshInstanceInfos = pDXRayTracing->m_dxMeshInstanceInfos;
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

        ID3D12ResourcePtr pScratch = DXCreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
        ID3D12ResourcePtr pResult = DXCreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.resize(totalInstanceNum);
        for (uint32_t indexMesh = 0; indexMesh < dxMeshInstanceInfos.size(); indexMesh++)
        {
            for (uint32_t indexInstance = 0; indexInstance < dxMeshInstanceInfos[indexMesh].instanes.size(); indexInstance++)
            {
                assert(dxMeshInstanceInfos[indexMesh].instanes.size() == 1);//TODO:

                const SMeshInstanceInfo& meshInstanceInfo = dxMeshInstanceInfos[indexMesh].instanes[indexInstance];
                instanceDescs[indexMesh].InstanceID = 0;                            // This value will be exposed to the shader via InstanceID()
                instanceDescs[indexMesh].InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
                instanceDescs[indexMesh].Flags = ConvertToDXInstanceFlag(meshInstanceInfo.m_instanceFlag);
                memcpy(instanceDescs[indexMesh].Transform, &meshInstanceInfo.m_transform, sizeof(instanceDescs[indexMesh].Transform));
                
                //TODO:
                instanceDescs[indexMesh].AccelerationStructure = pDXRayTracing->m_pBLASs[indexMesh]->GetGPUVirtualAddress();
                
                instanceDescs[indexMesh].InstanceMask = 0xFF;
            }
        }

        ID3D12ResourcePtr pInstanceDescBuffer = CreateDefaultBuffer(instanceDescs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalInstanceNum, pDXDevice->m_tempBuffers.AllocResource());

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

        SDXAccelerationStructureBuffers resultBuffers;
        resultBuffers.pResult = pResult;
        resultBuffers.pScratch = pScratch;
        resultBuffers.pInstanceDesc = pInstanceDescBuffer;

        SubmitCommandlist();
        WaitForPreviousFrame();
        ResetCmdList();

        return resultBuffers;
    }


    void hwrtl::BuildAccelerationStructure()
    {
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        

        BuildBottomLevelAccelerationStructure();
        SDXAccelerationStructureBuffers topLevelBuffers = BuildTopLevelAccelerationStructure();
        pDXRayTracing->m_ptlas = topLevelBuffers.pResult;

        
    }

    IDxcBlobPtr CompileLibraryDXC(const std::wstring& shaderPath, LPCWSTR pEntryPoint, LPCWSTR pTargetProfile)
    {
        std::ifstream shaderFile(shaderPath);

        if (shaderFile.good() == false)
        {
            ThrowIfFailed(-1); // invalid shader path
        }

        std::stringstream strStream;
        strStream << shaderFile.rdbuf();
        std::string shader = strStream.str();

        IDxcBlobEncodingPtr pTextBlob;
        ThrowIfFailed(pDXRayTracing->m_pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob));

        IDxcOperationResultPtr pResult;
        ThrowIfFailed(pDXRayTracing->m_pDxcCompiler->Compile(pTextBlob, shaderPath.data(), pEntryPoint, pTargetProfile, nullptr, 0, nullptr, 0, nullptr, &pResult));

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
        pDXRayTracing->m_dxcValidator->Validate(pBlob, DxcValidatorFlags_InPlaceEdit, &pValidResult);

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

    std::shared_ptr<CRayTracingPipelineState> hwrtl::CreateRTPipelineStateAndShaderTable(const std::wstring shaderPath,
        std::vector<SShader>rtShaders,
        uint32_t maxTraceRecursionDepth,
        SShaderResources rayTracingResources)
    {
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));

        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        pDXRayTracing->m_rtResouces = rayTracingResources;

        pDXRayTracing->m_rangeIndex[0] = 0;
        for (uint32_t index = 1; index < 4; index++)
        {
            pDXRayTracing->m_rangeIndex[index] = pDXRayTracing->m_rangeIndex[index - 1] + pDXRayTracing->m_rtResouces[index - 1];
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
        ID3DBlobPtr pDxilLib = CompileLibraryDXC(shaderPath, L"", L"lib_6_3");
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
        //TODO:
        uint32_t totalIndex = 0;
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
                    descRange.OffsetInDescriptorsFromTableStart = totalIndex;
                    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                    totalIndex += rayTracingResources[descRangeIndex];
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
        
        pDXRayTracing->m_pGlobalRootSig = globalRootSignature.m_pRootSig;
        
        ThrowIfFailed(pDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pDXRayTracing->m_pRtPipelineState)));

        SRayTracingInitDesc rtInitDesc = { {0,0,0,0} };

        // create shader table
        uint32_t shaderTableSize = ShaderTableEntrySize * rtShaders.size();

        std::vector<char>shaderTableData;
        shaderTableData.resize(shaderTableSize);

        ID3D12StateObjectPropertiesPtr pRtsoProps;
        pDXRayTracing->m_pRtPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

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

            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = pDXRayTracing->m_rtDescManager.GetGPUHandle(0);
            memcpy(shaderTableData.data() + index * ShaderTableEntrySize + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, &gpuHandle, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
            
            rtInitDesc.m_nShaderNum[uint32_t(rtShader.m_eShaderType)]++;
        }

        pDXRayTracing->m_pShaderTable = CreateDefaultBuffer(shaderTableData.data(), shaderTableSize, pDXDevice->m_tempBuffers.AllocResource());

        SubmitCommandlist();
        WaitForPreviousFrame();
        ResetCmdList();

        return std::make_shared<CDxRayTracingPipelineState>(rtInitDesc);
    }

#define CHECK_AND_ADD_FLAG(eTexUsage,eCheckUsage,eAddedFlag,nullFlag) (((uint32_t(eTexUsage) & uint32_t(eCheckUsage)) != 0) ? eAddedFlag : nullFlag)

    static D3D12_RESOURCE_FLAGS ConvertUsageToDxFlag(ETexUsage eTexUsage)
    {
        D3D12_RESOURCE_FLAGS resouceFlags = D3D12_RESOURCE_FLAG_NONE;
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_UAV, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_NONE);
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_RTV, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_FLAG_NONE);
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_DSV, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_FLAG_NONE);
        return resouceFlags;
    }

    static DXGI_FORMAT ConvertTexFormatToDxFormat(ETexFormat eTexFormat)
    {
        switch (eTexFormat)
        {
        case ETexFormat::FT_RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case ETexFormat::FT_RGBA32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        }
        ThrowIfFailed(-1);
        return DXGI_FORMAT_UNKNOWN;
    }

    bool ValidateResource()
    {
        return true;
    }

    SResourceHandle hwrtl::CreateTexture2D(STextureCreateDesc texCreateDesc)
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;

        CDXResouceManager& resManager = pDXDevice->m_resouManager;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.DepthOrArraySize = 1;
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Format = ConvertTexFormatToDxFormat(texCreateDesc.m_eTexFormat);
        resDesc.Flags = ConvertUsageToDxFlag(texCreateDesc.m_eTexUsage);
        resDesc.Width = texCreateDesc.m_width;
        resDesc.Height = texCreateDesc.m_height;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;

        D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t allocIndex;
        ThrowIfFailed(pDevice->CreateCommittedResource(&defaultHeapProperies, D3D12_HEAP_FLAG_NONE, &resDesc, InitialResourceState, nullptr, IID_PPV_ARGS(&resManager.AllocResource(allocIndex))));
        resManager.SetResouceCurrentState(allocIndex, InitialResourceState);

        CDXDescManager& csuDescManager = pDXDevice->m_csuDescManager;
        CDXDescManager& rtvDescManager = pDXDevice->m_rtvDescManager;

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_SRV))
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = resDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            uint32_t srvIndex = csuDescManager.AllocDesc();

            pDevice->CreateShaderResourceView(resManager.GetResource(allocIndex), &srvDesc, csuDescManager.GetCPUHandle(srvIndex));
            resManager.SetCSUIndex(allocIndex, srvIndex);
        }

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_RTV))
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
            rtvDesc.Format = resDesc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            uint32_t rtvIndex = rtvDescManager.AllocDesc();

            pDevice->CreateRenderTargetView(resManager.GetResource(allocIndex), nullptr, rtvDescManager.GetCPUHandle(rtvIndex));
            resManager.SetRTVIndex(allocIndex, rtvIndex);
        }

        return SResourceHandle(allocIndex);
    }

    SResourceHandle hwrtl::CreateBuffer(const void* pInitData, uint64_t nByteSize, uint64_t nStride, EBufferUsage bufferUsage)
    {
        uint32_t allocIndex = 0;
        ID3D12ResourcePtr& resource = pDXDevice->m_resouManager.AllocResource(allocIndex);
        
        if (bufferUsage == EBufferUsage::USAGE_VB || bufferUsage == EBufferUsage::USAGE_IB)
        {
            resource = CreateDefaultBuffer(pInitData, nByteSize, pDXDevice->m_tempBuffers.AllocResource());

            D3D12_VERTEX_BUFFER_VIEW vbView = {};
            vbView.BufferLocation = resource->GetGPUVirtualAddress();
            vbView.SizeInBytes = nByteSize;
            vbView.StrideInBytes = nStride;

            pDXDevice->m_resouManager.SetVertexBufferView(allocIndex, vbView);
        }
        else if (bufferUsage == EBufferUsage::USAGE_CB)
        {
            auto pDevice = pDXDevice->m_pDevice;
            CDXResouceManager& resManager = pDXDevice->m_resouManager;
            CDXDescManager& csuDescManager = pDXDevice->m_csuDescManager;

            //temporary code
            if (pInitData != nullptr)
            {
                resource = CreateDefaultBuffer(pInitData, nByteSize, pDXDevice->m_tempBuffers.AllocResource());

            }
            else
            {
                resource = DXCreateBuffer(nByteSize, D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeapProperies);
            }
            

            uint32_t cbvIndex = csuDescManager.AllocDesc();

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = resource->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = nByteSize;
            pDevice->CreateConstantBufferView(&cbvDesc, csuDescManager.GetCPUHandle(cbvIndex));
            resManager.SetCSUIndex(allocIndex, cbvIndex);

            //if (pInitData != nullptr)
            //{
            //    void* cbvDataPtr;
            //    D3D12_RANGE readRange{ 0, 0 };
            //    ThrowIfFailed(resource->Map(0, &readRange, reinterpret_cast<void**>(&cbvDataPtr)));
            //    memcpy(cbvDataPtr, &pInitData, sizeof(nByteSize));
            //    //resource->Unmap(0, &readRange);
            //}
        }
        
        return allocIndex;
    }

    void hwrtl::UpdateConstantBuffer(SResourceHandle resourceHandle, uint64_t nByteSize, const void* pData)
    {
        ID3D12ResourcePtr& resource = pDXDevice->m_resouManager.GetResource(resourceHandle);
        void* cbvDataPtr;
        D3D12_RANGE readRange{ 0, 0 };
        ThrowIfFailed(resource->Map(0, &readRange, reinterpret_cast<void**>(&cbvDataPtr)));
        memcpy(cbvDataPtr, &pData, sizeof(nByteSize));
        resource->Unmap(0, nullptr);
    }

    void hwrtl::SubmitCommandlist()
    {
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandQueuePtr pCmdQueue = pDXDevice->m_pCmdQueue;

        pCmdList->Close();
        ID3D12CommandList* pGraphicsList = pCmdList.GetInterfacePtr();
        pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);

        pDXDevice->m_resouceBarriers.clear();
    }

    void hwrtl::SetShaderResource(SResourceHandle resource, ESlotType slotType, uint32_t bindIndex)
    {
        ID3D12ResourcePtr pResouce = pDXDevice->m_resouManager.GetResource(resource);
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        auto pCommandList = pDXDevice->m_pCmdList;
        
        //TODO: MoveToCreate Texture2D
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        switch (slotType)
        {
        case ESlotType::ST_U:
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pDXRayTracing->m_rtDescManager.GetCPUHandle(bindIndex + pDXRayTracing->m_rangeIndex[uint32_t(ESlotType::ST_U)]);
            pDevice->CreateUnorderedAccessView(pResouce, nullptr, &uavDesc, cpuHandle);
            break;
        case ESlotType::ST_T:
            //TODO: MoveToCreate Texture2D
            UINT destStart = bindIndex + pDXRayTracing->m_rangeIndex[uint32_t(ESlotType::ST_T)];
        
            D3D12_CPU_DESCRIPTOR_HANDLE destPos = pDXRayTracing->m_rtDescManager.GetCPUHandle(bindIndex);
            uint32_t indexHandle = pDXDevice->m_resouManager.GetCSUIndex(resource);
            D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDXDevice->m_csuDescManager.GetCPUHandle(indexHandle);
            
            UINT destRangeSize = 1;
            pDXDevice->m_pDevice->CopyDescriptors(1, &destPos, &destRangeSize, 1, &srcHandle, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        
            break;
        }
        
        D3D12_RESOURCE_STATES stateAfter;
        switch (slotType)
        {
        case ESlotType::ST_U:
            stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            break;
        case ESlotType::ST_T:
            stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            break;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = pResouce;
        barrier.Transition.StateBefore = pDXDevice->m_resouManager.GetResouceCurrentState(resource);
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        pDXDevice->m_resouManager.SetResouceCurrentState(resource, stateAfter);
        pDXDevice->m_resouceBarriers.push_back(barrier);

        ValidateResource();
    }

    void hwrtl::SetTLAS(uint32_t bindIndex)
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = pDXRayTracing->m_ptlas->GetGPUVirtualAddress();
        pDevice->CreateShaderResourceView(nullptr, &srvDesc, pDXRayTracing->m_rtDescManager.GetCPUHandle(bindIndex + pDXRayTracing->m_rangeIndex[uint32_t(ESlotType::ST_T)]));
        ValidateResource();
    }

    void hwrtl::BeginRayTracing()
    {
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));

        ID3D12DescriptorHeap* heaps[] = { pDXRayTracing->m_rtDescManager.GetHeap()};
        pDXDevice->m_pCmdList->SetDescriptorHeaps(1, heaps);
    }

    void hwrtl::DispatchRayTracicing(std::shared_ptr<CRayTracingPipelineState>rtPipelineState, uint32_t width, uint32_t height)
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;

        CDxRayTracingPipelineState* dxRtPipelineState = static_cast<CDxRayTracingPipelineState*>(rtPipelineState.get());

        D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
        raytraceDesc.Width = width;
        raytraceDesc.Height = height;
        raytraceDesc.Depth = 1;

        // RayGen is the first entry in the shader-table
        raytraceDesc.RayGenerationShaderRecord.StartAddress = pDXRayTracing->m_pShaderTable->GetGPUVirtualAddress() + 0 * ShaderTableEntrySize;
        raytraceDesc.RayGenerationShaderRecord.SizeInBytes = ShaderTableEntrySize;

        uint32_t offset = 1 * ShaderTableEntrySize;;
        
        uint32_t rMissShaderNum = dxRtPipelineState->m_nShaderNum[uint32_t(ERayShaderType::RAY_MIH)];
        if (rMissShaderNum > 0)
        {
            raytraceDesc.MissShaderTable.StartAddress = pDXRayTracing->m_pShaderTable->GetGPUVirtualAddress() + offset;
            raytraceDesc.MissShaderTable.StrideInBytes = ShaderTableEntrySize;
            raytraceDesc.MissShaderTable.SizeInBytes = ShaderTableEntrySize * rMissShaderNum;   // Only a s single miss-entry

            offset += rMissShaderNum * ShaderTableEntrySize;
        }

        uint32_t rHitShaderNum = dxRtPipelineState->m_nShaderNum[uint32_t(ERayShaderType::RAY_AHS)] + dxRtPipelineState->m_nShaderNum[uint32_t(ERayShaderType::RAY_CHS)];

        if (rHitShaderNum > 0)
        {
            raytraceDesc.HitGroupTable.StartAddress = pDXRayTracing->m_pShaderTable->GetGPUVirtualAddress() + offset;
            raytraceDesc.HitGroupTable.StrideInBytes = ShaderTableEntrySize;
            raytraceDesc.HitGroupTable.SizeInBytes = ShaderTableEntrySize * rHitShaderNum; //todo: multipie number
        }

        pCmdList->SetComputeRootSignature(pDXRayTracing->m_pGlobalRootSig);
        pCmdList->SetComputeRootDescriptorTable(0, pDXRayTracing->m_rtDescManager.GetGPUHandle(0));

        if (pDXDevice->m_resouceBarriers.size() > 0)
        {
            pCmdList->ResourceBarrier(pDXDevice->m_resouceBarriers.size(), pDXDevice->m_resouceBarriers.data());
        }

        // Dispatch
        pCmdList->SetPipelineState1(pDXRayTracing->m_pRtPipelineState);
        pCmdList->DispatchRays(&raytraceDesc);
    }

    static DXGI_FORMAT ConvertToDXVertexFormat(EVertexFormat vertexFormat)
    {
        switch (vertexFormat)
        {
        case EVertexFormat::FT_FLOAT3:
            return DXGI_FORMAT_R32G32B32_FLOAT;
            break;
        case EVertexFormat::FT_FLOAT2:
            return DXGI_FORMAT_R32G32_FLOAT;
            break;
        }
        ThrowIfFailed(-1);
        return DXGI_FORMAT_UNKNOWN;
    }

    std::shared_ptr<CGraphicsPipelineState> hwrtl::CreateRSPipelineState(const std::wstring shaderPath, std::vector<SShader> rtShaders, SShaderResources rasterizationResources, std::vector<EVertexFormat>vertexLayouts, std::vector<ETexFormat>rtFormats)
    {
        auto pDevice = pDXDevice->m_pDevice;
        ID3D12RootSignaturePtr pRsGlobalRootSig;
        ID3D12PipelineStatePtr pRSPipelinState;

        // create root signature
        {
            uint32_t noEmptySignatureNum = 0;

            D3D12_DESCRIPTOR_RANGE descRanges[4];
            for (uint32_t index = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; index <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; index++)
            {
                uint32_t numDesc = rasterizationResources[index];
                if (numDesc > 0)
                {
                    descRanges[noEmptySignatureNum].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(index);
                    descRanges[noEmptySignatureNum].NumDescriptors = rasterizationResources[index];
                    descRanges[noEmptySignatureNum].BaseShaderRegister = rasterizationResources.GetPreSumNum(index);
                    descRanges[noEmptySignatureNum].RegisterSpace = 0;
                    descRanges[noEmptySignatureNum].OffsetInDescriptorsFromTableStart = noEmptySignatureNum;
                    noEmptySignatureNum++;
                }
            }

            D3D12_ROOT_DESCRIPTOR_TABLE rootDescTable;
            rootDescTable.NumDescriptorRanges = noEmptySignatureNum;
            rootDescTable.pDescriptorRanges = descRanges;

            D3D12_ROOT_PARAMETER rootParameter;
            rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rootParameter.DescriptorTable = rootDescTable;

            D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
            rootSigDesc.NumParameters = 1;
            rootSigDesc.pParameters = &rootParameter;
            rootSigDesc.NumStaticSamplers = 0;
            rootSigDesc.pStaticSamplers = 0;
            rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ID3DBlobPtr signature;
            ID3DBlobPtr error;
            ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
            ThrowIfFailed(pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRsGlobalRootSig)));
        }

        // create pipeline state
        {
            ID3DBlobPtr shaders[2];

#if ENABLE_PIX_FRAME_CAPTURE
            UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT compileFlags = 0;
#endif

            for (uint32_t index = 0; index < rtShaders.size(); index++)
            {
                bool bVS = rtShaders[index].m_eShaderType == ERayShaderType::RS_VS;
                LPCWSTR pTarget = bVS ? L"vs_6_1" : L"ps_6_1";
                uint32_t shaderIndex = bVS ? 0 : 1;
                
                shaders[shaderIndex] = CompileLibraryDXC(shaderPath.c_str(), rtShaders[index].m_entryPoint.c_str(), pTarget);
            }

            std::vector<D3D12_INPUT_ELEMENT_DESC>inputElementDescs;
            for (uint32_t index = 0; index < vertexLayouts.size(); index++)
            {
                inputElementDescs.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD" ,index,ConvertToDXVertexFormat(vertexLayouts[index]),index,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA ,0 });
            }

            D3D12_RASTERIZER_DESC rasterizerDesc = {};
            rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
            rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
            rasterizerDesc.FrontCounterClockwise = FALSE;
            rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            rasterizerDesc.DepthClipEnable = TRUE;
            rasterizerDesc.MultisampleEnable = FALSE;
            rasterizerDesc.AntialiasedLineEnable = FALSE;
            rasterizerDesc.ForcedSampleCount = 0;
            rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            D3D12_BLEND_DESC blendDesc = {};
            blendDesc.AlphaToCoverageEnable = FALSE;
            blendDesc.IndependentBlendEnable = FALSE;
            const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
            {
                FALSE,FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP,
                D3D12_COLOR_WRITE_ENABLE_ALL,
            };

            for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
                blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
            }
                
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = { inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size())};
            psoDesc.pRootSignature = pRsGlobalRootSig;
            psoDesc.VS.pShaderBytecode = shaders[0]->GetBufferPointer();
            psoDesc.VS.BytecodeLength = shaders[0]->GetBufferSize();
            psoDesc.PS.pShaderBytecode = shaders[1]->GetBufferPointer();
            psoDesc.PS.BytecodeLength = shaders[1]->GetBufferSize();
            psoDesc.RasterizerState = rasterizerDesc;
            psoDesc.BlendState = blendDesc;
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = rtFormats.size();
            for (uint32_t index = 0; index < rtFormats.size(); index++)
            {
                psoDesc.RTVFormats[index] = ConvertTexFormatToDxFormat(rtFormats[index]);
            }
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pRSPipelinState)));
        }
        return std::make_shared<CDxGraphicsPipelineState>(pRsGlobalRootSig, pRSPipelinState);
    }

    void hwrtl::WaitForPreviousFrame()
    {
        const UINT64 nFenceValue = pDXDevice->m_nFenceValue;
        ThrowIfFailed(pDXDevice->m_pCmdQueue->Signal(pDXDevice->m_pFence, nFenceValue));
        pDXDevice->m_nFenceValue++;

        // Wait until the previous frame is finished.
        if (pDXDevice->m_pFence->GetCompletedValue() < nFenceValue)
        {
            ThrowIfFailed(pDXDevice->m_pFence->SetEventOnCompletion(nFenceValue, pDXDevice->m_FenceEvent));
            WaitForSingleObject(pDXDevice->m_FenceEvent, INFINITE);
        }
    }

    void hwrtl::ResetCmdList()
    {
        auto pCommandList = pDXDevice->m_pCmdList;
        auto pCmdAllocator = pDXDevice->m_pCmdAllocator;
        // Command list allocators can only be reset when the associated 
        // command lists have finished execution on the GPU; apps should use 
        // fences to determine GPU execution progress.
        ThrowIfFailed(pCmdAllocator->Reset());
    }

    void hwrtl::BeginRasterization(std::shared_ptr<CGraphicsPipelineState> graphicsPipelineStata)
    {
        CDxGraphicsPipelineState* dxGraphicsPipelineStata = static_cast<CDxGraphicsPipelineState*>(graphicsPipelineStata.get());
        auto pCmdAllocator = pDXDevice->m_pCmdAllocator;
        auto pCommandList = pDXDevice->m_pCmdList;
        
        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        ThrowIfFailed(pCommandList->Reset(pCmdAllocator, dxGraphicsPipelineStata->m_pRSPipelinState));

        ID3D12DescriptorHeap* ppHeaps[] = { pDXRasterization->m_graphicsContext.m_dxPassDescManager.GetHeapPtr() };
        pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
        pCommandList->SetGraphicsRootSignature(dxGraphicsPipelineStata->m_pRsGlobalRootSig);
    }

    void hwrtl::SetTexture(SResourceHandle cbHandle, uint32_t offset)
    {
        //see SetShaderResource

        //TODO
        //TransitionResource
        //CopyDescriptors
        //SetComputeRootDescriptorTable
    }

    void hwrtl::SetConstantBuffer(SResourceHandle cbHandle, uint32_t offset)
    {
        auto pCommandList = pDXDevice->m_pCmdList;

        uint32_t cbvIndex = pDXDevice->m_resouManager.GetCSUIndex(cbHandle);        
        auto handle = pDXDevice->m_csuDescManager.GetCPUHandle(cbvIndex);

        pDXRasterization->m_graphicsContext.m_dxPassHandleManager.SetCPUHandle(offset, handle, ESlotType::ST_B);
    }

    void hwrtl::SetRenderTargets(SResourceHandle* renderTargets,uint32_t renderTargetNum, SResourceHandle depthStencil, bool bClearRT, bool bClearDs)
    {
        CDXResouceManager& resouManager = pDXDevice->m_resouManager;
        for (uint32_t index = 0; index < renderTargetNum; index++)
        {
            uint32_t rtvIndex = resouManager.GetRTVIndex(renderTargets[index]);
            pDXRasterization->m_renderTargetHandles[index] = pDXDevice->m_rtvDescManager.GetCPUHandle(rtvIndex);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = resouManager.GetResource(renderTargets[index]);
            barrier.Transition.StateBefore = resouManager.GetResouceCurrentState(renderTargets[index]);
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            pDXDevice->m_resouManager.SetResouceCurrentState(renderTargets[index], D3D12_RESOURCE_STATE_RENDER_TARGET);
            pDXDevice->m_resouceBarriers.push_back(barrier);
        }
        pDXRasterization->m_nRenderTargetNum = renderTargetNum;

        auto pCommandList = pDXDevice->m_pCmdList;
        if (pDXDevice->m_resouceBarriers.size() > 0)
        {
            pCommandList->ResourceBarrier(pDXDevice->m_resouceBarriers.size(), pDXDevice->m_resouceBarriers.data());
        }
    }

    void hwrtl::SetViewport(float width, float height)
    {
        pDXRasterization->m_viewPort = { 0,0,width ,height ,0,1 };
        pDXRasterization->m_scissorRect = { 0,0,static_cast<LONG>(width) ,static_cast<LONG>(height) };
    }

    void hwrtl::SetVertexBuffers(SResourceHandle* vertexBuffer, uint32_t slotNum)
    {
        pDXRasterization->m_vbViews.resize(slotNum);
        for (uint32_t index = 0; index < slotNum; index++)
        {
            D3D12_VERTEX_BUFFER_VIEW vbView = pDXDevice->m_resouManager.GetVertexBufferView(vertexBuffer[index]);
            pDXRasterization->m_vbViews[index] = vbView;
        }
    }

    static void ApplyCBV()
    {
        auto pCommandList = pDXDevice->m_pCmdList;
        //temporary code
        {
            auto& passHandleManager = pDXRasterization->m_graphicsContext.m_dxPassHandleManager;
            auto& passDescManager = pDXRasterization->m_graphicsContext.m_dxPassDescManager;
            uint32_t numCBVUsed = passHandleManager.GetSlotNum(ESlotType::ST_B);
            uint32_t startIndex = passDescManager.GetAndAddCurrentPassSlotStart(numCBVUsed);
            D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle = passDescManager.GetCPUHandle(startIndex);


            D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[16];
            for (uint32_t index = 0; index < numCBVUsed; index++)
            {
                srcHandles[index] = passHandleManager.GetCPUHandle(index, ESlotType::ST_B);
            }

            pDXDevice->m_pDevice->CopyDescriptors(1, &cpuDescHandle, &numCBVUsed, numCBVUsed, srcHandles, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle = passDescManager.GetGPUHandle(startIndex);
            pCommandList->SetGraphicsRootDescriptorTable(0/*TODO*/, gpuDescHandle);
        }
    }

    void hwrtl::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t InstanceCount, uint32_t StartVertexLocation, uint32_t StartInstanceLocation)
    {
        auto pCommandList = pDXDevice->m_pCmdList;
        
        ApplyCBV();

        pCommandList->RSSetViewports(1, &pDXRasterization->m_viewPort);
        pCommandList->RSSetScissorRects(1, &pDXRasterization->m_scissorRect);

        pCommandList->OMSetRenderTargets(pDXRasterization->m_nRenderTargetNum, pDXRasterization->m_renderTargetHandles, FALSE, nullptr);

        //const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        //pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pCommandList->IASetVertexBuffers(0, pDXRasterization->m_vbViews.size(), pDXRasterization->m_vbViews.data());
        pCommandList->DrawInstanced(vertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    }

    void hwrtl::DestroyScene()
    {
    }

	void hwrtl::Shutdown()
	{
#if ENABLE_PIX_FRAME_CAPTURE
        PIXEndCapture(false);
#endif
        delete pDXRayTracing;
        delete pDXRasterization;
        delete pDXDevice;
	}
}

#endif


