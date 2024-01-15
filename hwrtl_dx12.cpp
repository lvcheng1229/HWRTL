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
//      15. state tracking: remove redundant barrier
//      16. constant buffer: static/dynamic
//      17. structure buffer: static/dynamic uav/srv
//      18. remove help header : d3dx12.h see : DeviceCreateStructBuffer in xengine
//      19. get immediate cmd list for create and upload buffer
//


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
#include <assert.h>

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
static constexpr uint32_t ShaderTableEntrySize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

namespace hwrtl
{
    /***************************************************************************
    * Common Helper Functions
    ***************************************************************************/
    
    static void ThrowIfFailed(HRESULT hr)
    {
#if ENABLE_THROW_FAILED_RESULT
        if (FAILED(hr))
        {
            assert(false);
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

    /***************************************************************************
    * Dx12 Helper Functions
    * 1. Create Dx Device
    * 2. Create Dx Descriptor Heap
    * 3. Create Dx CommandQueue
    ***************************************************************************/

    static ID3D12Device5Ptr Dx12CreateDevice(IDXGIFactory4Ptr pDxgiFactory)
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

    static ID3D12DescriptorHeapPtr Dx12CreateDescriptorHeap(ID3D12Device5Ptr pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = count;
        desc.Type = type;
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        ID3D12DescriptorHeapPtr pHeap;
        ThrowIfFailed(pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&pHeap)));
        return pHeap;
    }

    static ID3D12CommandQueuePtr Dx12CreateCommandQueue(ID3D12Device5Ptr pDevice)
    {
        ID3D12CommandQueuePtr pQueue;
        D3D12_COMMAND_QUEUE_DESC cqDesc = {};
        cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(pDevice->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&pQueue)));
        return pQueue;
    }

    /***************************************************************************
    * Convert RHI enum to Dx enum
    * 1. Convert Instance Flag To Dx Instance Flag
    * 2. Create Texture Usage To Dx Flag
    * 3. Create Texture Format To Dx Format
    ***************************************************************************/

    D3D12_RAYTRACING_INSTANCE_FLAGS Dx12ConvertInstanceFlag(EInstanceFlag instanceFlag)
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

#define CHECK_AND_ADD_FLAG(eTexUsage,eCheckUsage,eAddedFlag,nullFlag) (((uint32_t(eTexUsage) & uint32_t(eCheckUsage)) != 0) ? eAddedFlag : nullFlag)

    static D3D12_RESOURCE_FLAGS Dx12ConvertResouceFlags(ETexUsage eTexUsage)
    {
        D3D12_RESOURCE_FLAGS resouceFlags = D3D12_RESOURCE_FLAG_NONE;
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_UAV, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_NONE);
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_RTV, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_FLAG_NONE);
        resouceFlags |= CHECK_AND_ADD_FLAG(eTexUsage, ETexUsage::USAGE_DSV, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_FLAG_NONE);
        return resouceFlags;
    }

    static DXGI_FORMAT Dx12ConvertTextureFormat(ETexFormat eTexFormat)
    {
        switch (eTexFormat)
        {
        case ETexFormat::FT_RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case ETexFormat::FT_RGBA32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case ETexFormat::FT_DepthStencil:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
            break;
        }
        ThrowIfFailed(-1);
        return DXGI_FORMAT_UNKNOWN;
    }

    static DXGI_FORMAT Dx12ConvertToVertexFormat(EVertexFormat vertexFormat)
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

    static uint32_t Dx12TextureFormatSize(ETexFormat eTexFormat)
    {
        switch (eTexFormat)
        {
        case ETexFormat::FT_RGBA8_UNORM:
            return 4;
            break;
        case ETexFormat::FT_RGBA32_FLOAT:
            return 16;
            break;
        }
        ThrowIfFailed(-1);
        return 0;
    }



    struct SDx12MeshInstanceInfo
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

    struct SDx12AccelerationStructureBuffers
    {
        ID3D12ResourcePtr pScratch;
        ID3D12ResourcePtr pResult;
        ID3D12ResourcePtr pInstanceDesc;
    };

    class CDx12DescManager
    {
    public :
        CDx12DescManager() {};

        void Init(ID3D12Device5Ptr pDevice, uint32_t size, D3D12_DESCRIPTOR_HEAP_TYPE descHeapType, bool shaderVisible)
        {
            m_pDevice = pDevice;
            m_pDescHeap = Dx12CreateDescriptorHeap(pDevice, size, descHeapType, shaderVisible);
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
            dsvIndices.resize(1024);
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
                dsvIndices.resize(newSize);
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

        void SetDsvIndex(uint32_t resouceIndex, uint32_t dsvIndex)
        {
            dsvIndices[resouceIndex] = dsvIndex;
        }

        uint32_t GetDsvIndex(uint32_t resouceIndex)
        {
            return dsvIndices[resouceIndex];
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
            //TODO: Remove This Function
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
        std::vector<int>dsvIndices;
        std::vector<D3D12_VERTEX_BUFFER_VIEW>m_vbViews;
    };

    class CDXPassDescManager
    {
    public:
        void Init(ID3D12Device5Ptr pDevice)
        {
            m_pDescHeap = Dx12CreateDescriptorHeap(pDevice, 1024, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

            m_hCpuBegin = m_pDescHeap->GetCPUDescriptorHandleForHeapStart();
            m_hGpuBegin = m_pDescHeap->GetGPUDescriptorHandleForHeapStart();

            m_nCurrentStartSlotIndex = 0;
            m_nElemSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

    class CDxGraphicsPipelineState : public CGraphicsPipelineState
    {
    public:
        ID3D12RootSignaturePtr m_pRsGlobalRootSig;
        ID3D12PipelineStatePtr m_pRSPipelinState;

        uint32_t m_slotDescNum[4];

        CDxGraphicsPipelineState()
        {
            m_pRsGlobalRootSig = nullptr;
            m_pRSPipelinState = nullptr;
            m_slotDescNum[0] = m_slotDescNum[1] = m_slotDescNum[2] = m_slotDescNum[3] = 0;
        }
    };

    class CDxRayTracingPipelineState : public CRayTracingPipelineState
    {
    public:
        ID3D12RootSignaturePtr m_pGlobalRootSig;
        ID3D12StateObjectPtr m_pRtPipelineState;
        ID3D12ResourcePtr m_pShaderTable;
        uint32_t m_nShaderNum[4];
        uint32_t m_slotDescNum[4];

        CDxRayTracingPipelineState()
        {
            m_pGlobalRootSig = nullptr;
            m_pShaderTable = nullptr;
            m_pRtPipelineState = nullptr;
            m_nShaderNum[0] = m_nShaderNum[1] = m_nShaderNum[2] = m_nShaderNum[3] = 0;
            m_slotDescNum[0] = m_slotDescNum[1] = m_slotDescNum[2] = m_slotDescNum[3] = 0;
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

        IDxcCompilerPtr m_pDxcCompiler;
        IDxcLibraryPtr m_pLibrary;
        IDxcValidatorPtr m_dxcValidator;

#if ENABLE_PIX_FRAME_CAPTURE
        HMODULE m_pixModule;
#endif

        HANDLE m_FenceEvent;
        uint64_t m_nFenceValue = 0;;

        CDXResouceManager m_tempBuffers;

        CDx12DescManager m_rtvDescManager;
        CDx12DescManager m_dsvDescManager;
        CDx12DescManager m_csuDescManager;//cbv srv uav 

        CDXResouceManager m_resouManager;
        std::vector<D3D12_RESOURCE_BARRIER> m_resouceBarriers;
    };

    struct SGraphicsContext
    {
        //CDxPassHandleManager m_dxPassHandleManager;
        //CDXPassDescManager m_dxPassDescManager;
    };

    class CDXRasterization
    {
    public:
        //SGraphicsContext m_graphicsContext;

        //std::vector<D3D12_VERTEX_BUFFER_VIEW>m_vbViews;
        //
        //D3D12_CPU_DESCRIPTOR_HANDLE m_renderTargetHandles[8];
        //D3D12_CPU_DESCRIPTOR_HANDLE m_depthStencilHandle;
        //
        //uint32_t m_nRenderTargetNum = 0;
        //bool bUseDepthStencil = false;

        //D3D12_VIEWPORT m_viewPort;
        //D3D12_RECT m_scissorRect;
    };

    class CDXRayTracing
    {
    public:
        //ID3D12StateObjectPtr m_pRtPipelineState;

        std::vector<SDx12MeshInstanceInfo>m_dxMeshInstanceInfos;
        
        std::vector<ID3D12ResourcePtr>m_pBLASs;

        //CDx12DescManager m_rtDescManager;

        ID3D12ResourcePtr m_ptlas;

        SShaderResources m_rtResouces;
        uint32_t m_rangeIndex[4];

    };

    static CDXDevice* pDXDevice = nullptr;
    static CDXRasterization* pDXRasterization = nullptr;
    static CDXRayTracing* pDXRayTracing = nullptr;

	void hwrtl::Init()
	{
        pDXDevice = new CDXDevice();
        pDXRasterization = new CDXRasterization();
        pDXRayTracing = new CDXRayTracing();

#if ENABLE_PIX_FRAME_CAPTURE
        pDXDevice->m_pixModule = PIXLoadLatestWinPixGpuCapturerLibrary();
#endif

		IDXGIFactory4Ptr pDxgiFactory;
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));
        
        pDXDevice->m_pDevice = Dx12CreateDevice(pDxgiFactory);
        pDXDevice->m_pCmdQueue = Dx12CreateCommandQueue(pDXDevice->m_pDevice);

        // create desc heap
        pDXDevice->m_rtvDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
        pDXDevice->m_csuDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
        pDXDevice->m_dsvDescManager.Init(pDXDevice->m_pDevice, 512, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);
        
        ThrowIfFailed(pDXDevice->m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pDXDevice->m_pCmdAllocator)));
        ThrowIfFailed(pDXDevice->m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pDXDevice->m_pCmdAllocator, nullptr, IID_PPV_ARGS(&pDXDevice->m_pCmdList)));

        ThrowIfFailed(pDXDevice->m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pDXDevice->m_pFence)));
        pDXDevice->m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        ThrowIfFailed(DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(&pDXDevice->m_dxcValidator)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pDXDevice->m_pDxcCompiler)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&pDXDevice->m_pLibrary)));

#if ENABLE_PIX_FRAME_CAPTURE
        std::wstring pixPath = WstringConverter().from_bytes(__FILE__).substr(0, WstringConverter().from_bytes(__FILE__).find(L"hwrtl_dx12.cpp")) + L"HWRT\\pix.wpix";
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

        CD3DX12_RESOURCE_BARRIER resourceBarrierAfter = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        pCmdList->ResourceBarrier(1, &resourceBarrierAfter);
        return defaultBuffer;
    }

    ID3D12ResourcePtr Dx12CreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
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

    struct CDx12Resouce
    {
        ID3D12ResourcePtr m_pResource;
        D3D12_RESOURCE_STATES m_resourceState;
    };

    struct CDx12View
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_pCpuDescHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_pGpuDescHandle;
        uint32_t indexInDescManager;
    };

    class CDxTexture2D : public CTexture2D
    {
    public:
        ~CDxTexture2D()
        {
            // Release Desc In Desc Manager
        }

        CDx12Resouce m_dxResource;

        CDx12View m_uav;
        CDx12View m_srv;
        CDx12View m_rtv;
        CDx12View m_dsv;
    };

    class CDxBuffer : public CBuffer
    {
    public:
        ~CDxBuffer()
        {
            // Release Desc In Desc Manager
        }

        CDx12Resouce m_dxResource;

        CDx12View m_uav;
        CDx12View m_srv;
        CDx12View m_cbv;

        D3D12_VERTEX_BUFFER_VIEW m_vbv;
    };

    std::shared_ptr<CTexture2D> hwrtl::CreateTexture2D(STextureCreateDesc texCreateDesc)
    {
        std::shared_ptr<CDxTexture2D> retDxTexture2D = std::make_shared<CDxTexture2D>();

        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.DepthOrArraySize = 1;
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Format = Dx12ConvertTextureFormat(texCreateDesc.m_eTexFormat);
        resDesc.Flags = Dx12ConvertResouceFlags(texCreateDesc.m_eTexUsage);
        resDesc.Width = texCreateDesc.m_width;
        resDesc.Height = texCreateDesc.m_height;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;

        CDx12Resouce* pDxResouce = &retDxTexture2D->m_dxResource;
        ID3D12Resource** pResoucePtr = &(pDxResouce->m_pResource);
        
        D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON;
        ThrowIfFailed(pDevice->CreateCommittedResource(&defaultHeapProperies, D3D12_HEAP_FLAG_NONE, &resDesc, InitialResourceState, nullptr, IID_PPV_ARGS(pResoucePtr)));
        pDxResouce->m_resourceState = InitialResourceState;

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_SRV))
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = resDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            CDx12DescManager& csuDescManager = pDXDevice->m_csuDescManager;
            uint32_t srvIndex = csuDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = csuDescManager.GetCPUHandle(srvIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = csuDescManager.GetGPUHandle(srvIndex);
            pDevice->CreateShaderResourceView(pDxResouce->m_pResource, &srvDesc, cpuHandle);

            retDxTexture2D->m_srv = CDx12View{ cpuHandle ,gpuHandle ,srvIndex };
        }

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_RTV))
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
            rtvDesc.Format = resDesc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            CDx12DescManager& rtvDescManager = pDXDevice->m_rtvDescManager;
            uint32_t rtvIndex = rtvDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = rtvDescManager.GetCPUHandle(rtvIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = rtvDescManager.GetGPUHandle(rtvIndex);
            pDevice->CreateRenderTargetView(pDxResouce->m_pResource, nullptr, rtvDescManager.GetCPUHandle(rtvIndex));

            retDxTexture2D->m_rtv = CDx12View{ cpuHandle ,gpuHandle ,rtvIndex };
        }

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_UAV))
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
            uavDesc.Texture2D.PlaneSlice = 0;

            CDx12DescManager& csuDescManager = pDXDevice->m_csuDescManager;
            uint32_t uavIndex = csuDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = csuDescManager.GetCPUHandle(uavIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = csuDescManager.GetGPUHandle(uavIndex);
            pDevice->CreateUnorderedAccessView(pDxResouce->m_pResource, nullptr, &uavDesc, cpuHandle);

            retDxTexture2D->m_uav = CDx12View{ cpuHandle ,gpuHandle ,uavIndex };
        }

        if (uint32_t(texCreateDesc.m_eTexUsage & ETexUsage::USAGE_DSV))
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
            dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Format = Dx12ConvertTextureFormat(texCreateDesc.m_eTexFormat);
            dsvDesc.Texture2D.MipSlice = 0;

            CDx12DescManager& dsvDescManager = pDXDevice->m_dsvDescManager;
            uint32_t dsvIndex = dsvDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = dsvDescManager.GetCPUHandle(dsvIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dsvDescManager.GetGPUHandle(dsvIndex);
            pDevice->CreateDepthStencilView(pDxResouce->m_pResource, &dsvDesc, cpuHandle);

            retDxTexture2D->m_dsv = CDx12View{ cpuHandle ,gpuHandle ,dsvIndex };
        }

        return retDxTexture2D;
    }

    std::shared_ptr<CBuffer> CreateBuffer(const void* pInitData, uint64_t nByteSize, uint64_t nStride, EBufferUsage bufferUsage)
    {
        assert(pInitData != nullptr);

        auto dxBuffer = std::make_shared<CDxBuffer>();
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;

        dxBuffer->m_dxResource.m_pResource = CreateDefaultBuffer(pInitData, nByteSize, pDXDevice->m_tempBuffers.AllocResource());

        if (bufferUsage == EBufferUsage::USAGE_VB)
        {
            D3D12_VERTEX_BUFFER_VIEW vbView = {};
            vbView.BufferLocation = dxBuffer->m_dxResource.m_pResource->GetGPUVirtualAddress();
            vbView.SizeInBytes = nByteSize;
            vbView.StrideInBytes = nStride;
            dxBuffer->m_vbv = vbView;
        }

        if (bufferUsage == EBufferUsage::USAGE_IB)
        {
            assert(false);
        }

        if (bufferUsage == EBufferUsage::USAGE_CB)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = dxBuffer->m_dxResource.m_pResource->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = nByteSize;

            CDx12DescManager& csuDescManager = pDXDevice->m_csuDescManager;
            uint32_t cbvIndex = csuDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = csuDescManager.GetCPUHandle(cbvIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = csuDescManager.GetGPUHandle(cbvIndex);

            pDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);

            dxBuffer->m_dxResource.m_resourceState = D3D12_RESOURCE_STATE_COMMON;
            dxBuffer->m_cbv = CDx12View{ cpuHandle ,gpuHandle ,cbvIndex };
        }

        if (bufferUsage == EBufferUsage::USAGE_Structure)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = nByteSize / nStride;
            srvDesc.Buffer.StructureByteStride = nStride;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            CDx12DescManager& csuDescManager = pDXDevice->m_csuDescManager;
            uint32_t srvIndex = csuDescManager.AllocDesc();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = csuDescManager.GetCPUHandle(srvIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = csuDescManager.GetGPUHandle(srvIndex);

            pDevice->CreateShaderResourceView(dxBuffer->m_dxResource.m_pResource, &srvDesc, cpuHandle);

            dxBuffer->m_dxResource.m_resourceState = D3D12_RESOURCE_STATE_COMMON;
            dxBuffer->m_srv = CDx12View{ cpuHandle ,gpuHandle ,srvIndex };

        }

        return dxBuffer;
    }

    EAddMeshInstancesResult AddRayTracingMeshInstances(const SMeshInstancesDesc& meshInstancesDesc, std::shared_ptr<CBuffer> vertexBuffer)
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

        SDx12MeshInstanceInfo dxMeshInstanceInfo;
        dxMeshInstanceInfo.m_nIndexStride = meshInstancesDesc.m_nIndexStride;
        dxMeshInstanceInfo.m_nVertexCount = meshInstancesDesc.m_nVertexCount;
        dxMeshInstanceInfo.m_nIndexCount = meshInstancesDesc.m_nIndexCount;
        dxMeshInstanceInfo.instanes = meshInstancesDesc.instanes;

        // create vertex buffer
        CDxBuffer* dxVertexBuffer = static_cast<CDxBuffer*>(vertexBuffer.get());
        dxMeshInstanceInfo.m_pPositionBuffer = dxVertexBuffer->m_dxResource.m_pResource;
        
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

        const std::vector<SDx12MeshInstanceInfo> dxMeshInstanceInfos = pDXRayTracing->m_dxMeshInstanceInfos;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs ;
        std::vector<ID3D12ResourcePtr>pScratchSource;
        std::vector<ID3D12ResourcePtr>pResultSource;
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC>buildDescs;

        for (uint32_t i = 0; i < dxMeshInstanceInfos.size(); i++)
        {
            D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
            const SDx12MeshInstanceInfo& dxMeshInstanceInfo = dxMeshInstanceInfos[i];
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

            ID3D12ResourcePtr pScratch = Dx12CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
            ID3D12ResourcePtr pResult = Dx12CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

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

    SDx12AccelerationStructureBuffers BuildTopLevelAccelerationStructure()
    {
        ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));

        uint32_t totalInstanceNum = 0;
        const std::vector<SDx12MeshInstanceInfo> dxMeshInstanceInfos = pDXRayTracing->m_dxMeshInstanceInfos;
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

        ID3D12ResourcePtr pScratch = Dx12CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeapProperies);
        ID3D12ResourcePtr pResult = Dx12CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeapProperies);

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
                instanceDescs[indexMesh].Flags = Dx12ConvertInstanceFlag(meshInstanceInfo.m_instanceFlag);
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

        SDx12AccelerationStructureBuffers resultBuffers;
        resultBuffers.pResult = pResult;
        resultBuffers.pScratch = pScratch;
        resultBuffers.pInstanceDesc = pInstanceDescBuffer;

        SubmitCommandlist();
        WaitForPreviousFrame();
        ResetCmdList();

        return resultBuffers;
    }

    IDxcBlobPtr Dx12CompileRayTracingLibraryDXC(const std::wstring& shaderPath, LPCWSTR pEntryPoint, LPCWSTR pTargetProfile)
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
        ThrowIfFailed(pDXDevice->m_pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob));

        IDxcOperationResultPtr pResult;
        ThrowIfFailed(pDXDevice->m_pDxcCompiler->Compile(pTextBlob, shaderPath.data(), pEntryPoint, pTargetProfile, nullptr, 0, nullptr, 0, nullptr, &pResult));

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
        pDXDevice->m_dxcValidator->Validate(pBlob, DxcValidatorFlags_InPlaceEdit, &pValidResult);

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

        void Init(ID3D12Device5Ptr pDevice, SShaderResources rayTracingResources)
        {
            std::vector<D3D12_ROOT_PARAMETER> rootParams;
            std::vector<D3D12_DESCRIPTOR_RANGE> descRanges;

            uint32_t totalRootNum = 0;
            for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
            {
                uint32_t rangeNum = rayTracingResources[descRangeIndex];
                if (rangeNum > 0)
                {
                    totalRootNum++;
                }
            }

            rootParams.resize(totalRootNum);
            descRanges.resize(totalRootNum);

            uint32_t rootTabbleIndex = 0;

            for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
            {
                uint32_t rangeNum = rayTracingResources[descRangeIndex];
                if (rangeNum > 0)
                {
                    D3D12_DESCRIPTOR_RANGE descRange;
                    descRange.BaseShaderRegister = 0;
                    descRange.NumDescriptors = rangeNum;
                    descRange.RegisterSpace = 0;
                    descRange.OffsetInDescriptorsFromTableStart = 0;
                    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                    descRanges[rootTabbleIndex] = descRange;
                    
                    rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rootParams[rootTabbleIndex].DescriptorTable.NumDescriptorRanges = 1;
                    rootParams[rootTabbleIndex].DescriptorTable.pDescriptorRanges = &descRanges[rootTabbleIndex];

                    rootTabbleIndex++;
                }
            }

            D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
            rootSigDesc.NumParameters = rootParams.size();
            rootSigDesc.pParameters = rootParams.data();
            rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            m_pRootSig = CreateRootSignature(pDevice, rootSigDesc);
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
        std::shared_ptr<CDxRayTracingPipelineState> pDxRayTracingPipelineState = std::make_shared<CDxRayTracingPipelineState>();
        for (uint32_t index = 0; index < 4; index++)
        {
            pDxRayTracingPipelineState->m_slotDescNum[index] = rayTracingResources[index];
        }
        

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
        ID3DBlobPtr pDxilLib = Dx12CompileRayTracingLibraryDXC(shaderPath, L"", L"lib_6_3");
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
            globalRootSignature.Init(pDevice, rayTracingResources);
            stateSubObjects[subObjectsIndex] = globalRootSignature.m_subobject;
            subObjectsIndex++;
        }

        D3D12_STATE_OBJECT_DESC desc;
        desc.NumSubobjects = stateSubObjects.size();
        desc.pSubobjects = stateSubObjects.data();
        desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        
        pDxRayTracingPipelineState->m_pGlobalRootSig= globalRootSignature.m_pRootSig;
        
        ThrowIfFailed(pDevice->CreateStateObject(&desc, IID_PPV_ARGS(&pDxRayTracingPipelineState->m_pRtPipelineState)));

        // create shader table
        uint32_t shaderTableSize = ShaderTableEntrySize * rtShaders.size() + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE)/*?*/;

        std::vector<char>shaderTableData;
        shaderTableData.resize(shaderTableSize);

        ID3D12StateObjectPropertiesPtr pRtsoProps;
        pDxRayTracingPipelineState->m_pRtPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

        std::vector<void*>rayGenShaderIdentifiers;
        std::vector<void*>rayMissShaderIdentifiers;
        std::vector<void*>rayHitShaderIdentifiers;

        // ray gen shader identifier
        // global gpu handle
        // ray miss shader identifier
        // global gpu handle
        // ray hit shader identifier
        // global gpu handle

        uint32_t shaderTableHGindex = 0;
        for (uint32_t index = 0; index < rtShaders.size(); index++)
        {
            const SShader& rtShader = rtShaders[index];
            switch (rtShader.m_eShaderType)
            {
            case ERayShaderType::RAY_AHS:
            case ERayShaderType::RAY_CHS:
                rayHitShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(pHitGroupExports[shaderTableHGindex].c_str()));
                shaderTableHGindex++;
                break;
            case ERayShaderType::RAY_RGS:
                rayGenShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(rtShader.m_entryPoint.c_str()));
                break;
            case ERayShaderType::RAY_MIH:
                rayMissShaderIdentifiers.push_back(pRtsoProps->GetShaderIdentifier(rtShader.m_entryPoint.c_str()));
                break;
            };
            pDxRayTracingPipelineState->m_nShaderNum[uint32_t(rtShader.m_eShaderType)]++;
        }
        uint32_t offsetShaderTableIndex = 0;
        for (uint32_t index = 0; index < rayGenShaderIdentifiers.size(); index++)
        {
            memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayGenShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        offsetShaderTableIndex += rayGenShaderIdentifiers.size();

        for (uint32_t index = 0; index < rayMissShaderIdentifiers.size(); index++)
        {
            memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayMissShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        offsetShaderTableIndex += rayMissShaderIdentifiers.size();

        for (uint32_t index = 0; index < rayHitShaderIdentifiers.size(); index++)
        {
            memcpy(shaderTableData.data() + (offsetShaderTableIndex + index) * ShaderTableEntrySize, rayHitShaderIdentifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        offsetShaderTableIndex += rayHitShaderIdentifiers.size();

        pDxRayTracingPipelineState->m_pShaderTable = CreateDefaultBuffer(shaderTableData.data(), shaderTableSize, pDXDevice->m_tempBuffers.AllocResource());

        SubmitCommandlist();
        WaitForPreviousFrame();
        ResetCmdList();

        return pDxRayTracingPipelineState;
    }

    void hwrtl::ResetCommandList()
    {
        ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
        ID3D12CommandAllocatorPtr pCmdAlloc = pDXDevice->m_pCmdAllocator;
        ThrowIfFailed(pCmdList->Reset(pCmdAlloc, nullptr));
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

    static D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE wrapMode, uint32_t registerIndex, uint32_t space)
    {
        D3D12_STATIC_SAMPLER_DESC Result = {};

        Result.Filter = filter;
        Result.AddressU = wrapMode;
        Result.AddressV = wrapMode;
        Result.AddressW = wrapMode;
        Result.MipLODBias = 0.0f;
        Result.MaxAnisotropy = 1;
        Result.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        Result.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        Result.MinLOD = 0.0f;
        Result.MaxLOD = D3D12_FLOAT32_MAX;
        Result.ShaderRegister = registerIndex;
        Result.RegisterSpace = space;
        Result.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        return Result;
    }

    static const D3D12_STATIC_SAMPLER_DESC gStaticSamplerDescs[] =
    {
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  0, 1000),
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1, 1000),
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP,  2, 1000),
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 3, 1000),
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_WRAP,  4, 1000),
        MakeStaticSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 5, 1000),
    };

    std::shared_ptr<CGraphicsPipelineState> hwrtl::CreateRSPipelineState(const std::wstring shaderPath, std::vector<SShader> rtShaders, SShaderResources rasterizationResources, std::vector<EVertexFormat>vertexLayouts, std::vector<ETexFormat>rtFormats, ETexFormat dsFormat)
    {
        auto dxGaphicsPipelineState = std::make_shared<CDxGraphicsPipelineState>();
        auto pDevice = pDXDevice->m_pDevice;

        for (uint32_t index = 0; index < 4; index++)
        {
            dxGaphicsPipelineState->m_slotDescNum[index] = rasterizationResources[index];
        }

        // create root signature
        {
            std::vector<D3D12_ROOT_PARAMETER> rootParams;
            std::vector<D3D12_DESCRIPTOR_RANGE> descRanges;

            uint32_t totalRootNum = 0;
            for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
            {
                uint32_t rangeNum = rasterizationResources[descRangeIndex];
                if (rangeNum > 0)
                {
                    totalRootNum++;
                }
            }

            rootParams.resize(totalRootNum);
            descRanges.resize(totalRootNum);

            uint32_t rootTabbleIndex = 0;

            for (uint32_t descRangeIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; descRangeIndex <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; descRangeIndex++)
            {
                uint32_t rangeNum = rasterizationResources[descRangeIndex];
                if (rangeNum > 0)
                {
                    D3D12_DESCRIPTOR_RANGE descRange;
                    descRange.BaseShaderRegister = 0;
                    descRange.NumDescriptors = rangeNum;
                    descRange.RegisterSpace = 0;
                    descRange.OffsetInDescriptorsFromTableStart = 0;
                    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE(descRangeIndex);
                    descRanges[rootTabbleIndex] = descRange;

                    rootParams[rootTabbleIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rootParams[rootTabbleIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                    rootParams[rootTabbleIndex].DescriptorTable.NumDescriptorRanges = 1;
                    rootParams[rootTabbleIndex].DescriptorTable.pDescriptorRanges = &descRanges[rootTabbleIndex];

                    rootTabbleIndex++;
                }
            }

            D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
            rootSigDesc.NumParameters = rootParams.size();
            rootSigDesc.pParameters = rootParams.data();
            rootSigDesc.NumStaticSamplers = 6;
            rootSigDesc.pStaticSamplers = gStaticSamplerDescs;
            rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            dxGaphicsPipelineState->m_pRsGlobalRootSig = CreateRootSignature(pDevice, rootSigDesc);
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
                
                shaders[shaderIndex] = Dx12CompileRayTracingLibraryDXC(shaderPath.c_str(), rtShaders[index].m_entryPoint.c_str(), pTarget);
            }

            std::vector<D3D12_INPUT_ELEMENT_DESC>inputElementDescs;
            for (uint32_t index = 0; index < vertexLayouts.size(); index++)
            {
                inputElementDescs.push_back(D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD" ,index,Dx12ConvertToVertexFormat(vertexLayouts[index]),index,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA ,0 });
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
            psoDesc.pRootSignature = dxGaphicsPipelineState->m_pRsGlobalRootSig;
            psoDesc.VS.pShaderBytecode = shaders[0]->GetBufferPointer();
            psoDesc.VS.BytecodeLength = shaders[0]->GetBufferSize();
            psoDesc.PS.pShaderBytecode = shaders[1]->GetBufferPointer();
            psoDesc.PS.BytecodeLength = shaders[1]->GetBufferSize();
            psoDesc.RasterizerState = rasterizerDesc;
            psoDesc.BlendState = blendDesc;
            if (dsFormat == ETexFormat::FT_DepthStencil)
            {
                psoDesc.DepthStencilState.DepthEnable = TRUE;
                psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
                psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
                psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            }
            else
            {
                psoDesc.DepthStencilState.DepthEnable = FALSE;
                psoDesc.DepthStencilState.StencilEnable = FALSE;
            }

            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = rtFormats.size();
            for (uint32_t index = 0; index < rtFormats.size(); index++)
            {
                psoDesc.RTVFormats[index] = Dx12ConvertTextureFormat(rtFormats[index]);
            }
            psoDesc.SampleDesc.Count = 1;
            ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&dxGaphicsPipelineState->m_pRSPipelinState)));
        }
        return dxGaphicsPipelineState;
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

    /***************************************************************************
    * Device Command
    * 1. Create Ray Tracing Context
    ***************************************************************************/

    class CDxTopLevelAccelerationStructure : public CTopLevelAccelerationStructure
    {
    public:
        ID3D12ResourcePtr m_ptlas;
        CDx12View m_srv;
    };

    class CDxDeviceCommand : public CDeviceCommand
    {
    public:
        virtual void OpenCmdList()
        {
            auto pCommandList = pDXDevice->m_pCmdList;
            auto pCmdAllocator = pDXDevice->m_pCmdAllocator;
            ThrowIfFailed(pCommandList->Reset(pCmdAllocator, nullptr));
        }

        virtual void CloseAndExecuteCmdList()
        {
            ID3D12GraphicsCommandList4Ptr pCmdList = pDXDevice->m_pCmdList;
            ID3D12CommandQueuePtr pCmdQueue = pDXDevice->m_pCmdQueue;
            ID3D12CommandList* pGraphicsList = pCmdList.GetInterfacePtr();

            pCmdList->Close();
            pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);

            pDXDevice->m_resouceBarriers.clear();
        }

        virtual void WaitGPUCmdListFinish()
        {
            const UINT64 nFenceValue = pDXDevice->m_nFenceValue;
            ThrowIfFailed(pDXDevice->m_pCmdQueue->Signal(pDXDevice->m_pFence, nFenceValue));
            pDXDevice->m_nFenceValue++;

            if (pDXDevice->m_pFence->GetCompletedValue() < nFenceValue)
            {
                ThrowIfFailed(pDXDevice->m_pFence->SetEventOnCompletion(nFenceValue, pDXDevice->m_FenceEvent));
                WaitForSingleObject(pDXDevice->m_FenceEvent, INFINITE);
            }
        }

        virtual void ResetCmdAlloc()
        {
            auto pCmdAllocator = pDXDevice->m_pCmdAllocator;
            ThrowIfFailed(pCmdAllocator->Reset());
        }

        virtual std::shared_ptr<CTopLevelAccelerationStructure> BuildAccelerationStructure()override
        {
            auto dxTLAS = std::make_shared<CDxTopLevelAccelerationStructure>();
            BuildBottomLevelAccelerationStructure();
            SDx12AccelerationStructureBuffers topLevelBuffers = BuildTopLevelAccelerationStructure();
            dxTLAS->m_ptlas = topLevelBuffers.pResult;

            {
                ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.RaytracingAccelerationStructure.Location = dxTLAS->m_ptlas->GetGPUVirtualAddress();

                uint32_t srvIndex = pDXDevice->m_csuDescManager.AllocDesc();

                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pDXDevice->m_csuDescManager.GetCPUHandle(srvIndex);
                pDevice->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
                D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = pDXDevice->m_csuDescManager.GetGPUHandle(srvIndex);

                dxTLAS->m_srv.m_pCpuDescHandle = cpuHandle;
                dxTLAS->m_srv.m_pGpuDescHandle = gpuHandle;
            }

            return dxTLAS;
        }
    private:

    };

    std::shared_ptr <CDeviceCommand> hwrtl::CreateDeviceCommand()
    {
        return std::make_shared<CDxDeviceCommand>();
    }

    static constexpr int nHandlePerView = 32;

    /***************************************************************************
    * RayTracing Context
    * 1. Create Ray Tracing Context
    ***************************************************************************/

    void CheckBinding(D3D12_CPU_DESCRIPTOR_HANDLE* handles)
    {
#if ENABLE_DX12_DEBUG_LAYER
        for (uint32_t index = 0; index < nHandlePerView - 1; index++)
        {
            if (handles[index + 1].ptr != 0 && handles[index].ptr == 0)
            {
                assert(false && "shader binding must be continuous in hwrtl. for example: shader binding layout <t0, t1, t3> will cause error, you should bind resource like <t0, t1, t2>");
            }
        }
#endif
    }

    class CDx12RayTracingContext : public CRayTracingContext
    {
    public:
        CDx12RayTracingContext() 
        {
            ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
            m_rtPassDescManager.Init(pDevice);
            memset(m_viewHandles, 0, 4 * nHandlePerView * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
        }

        virtual ~CDx12RayTracingContext() 
        {

        }

        virtual void BeginRayTacingPasss()override
        {
            auto pCmdAlloc = pDXDevice->m_pCmdAllocator;
            auto pCommandList = pDXDevice->m_pCmdList;
            ThrowIfFailed(pCommandList->Reset(pCmdAlloc, nullptr));

            ID3D12DescriptorHeap* heaps[] = { m_rtPassDescManager.GetHeapPtr() };
            pCommandList->SetDescriptorHeaps(1, heaps);

            ResetContext();
        }

        virtual void EndRayTacingPasss()override
        {
            SubmitCommandlist();
            WaitForPreviousFrame();
            ResetCmdList();
            m_rtPassDescManager.ResetPassSlotStartIndex();
        }

        virtual void SetRayTracingPipelineState(std::shared_ptr<CRayTracingPipelineState>rtPipelineState)override
        {
            CDxRayTracingPipelineState* dxRayTracingPSO = static_cast<CDxRayTracingPipelineState*>(rtPipelineState.get());
            m_pGlobalRootSig = dxRayTracingPSO->m_pGlobalRootSig;
            m_pRtPipelineState = dxRayTracingPSO->m_pRtPipelineState;
            m_pShaderTable = dxRayTracingPSO->m_pShaderTable;

            for (uint32_t index = 0; index < 4; index++)
            {
                m_nShaderNum[index] = dxRayTracingPSO->m_nShaderNum[index];
                m_slotDescNum[index] = dxRayTracingPSO->m_slotDescNum[index];
            }

            m_viewSlotIndex[0] = 0;
            for (uint32_t index = 1; index < 4; index++)
            {
                m_viewSlotIndex[index] = m_viewSlotIndex[index - 1] + (m_slotDescNum[index - 1] > 0 ? 1 : 0);
            }

            m_bPipelineStateDirty = true;
        }

        virtual void SetTLAS(std::shared_ptr<CTopLevelAccelerationStructure> tlas,uint32_t bindIndex)override
        {
            CDxTopLevelAccelerationStructure* pDxTex2D = static_cast<CDxTopLevelAccelerationStructure*>(tlas.get());
            m_viewHandles[uint32_t(ESlotType::ST_T)][bindIndex] = pDxTex2D->m_srv.m_pCpuDescHandle;
            m_bViewTableDirty[uint32_t(ESlotType::ST_T)] = true;
        }

        virtual void SetShaderSRV(std::shared_ptr<CTexture2D>tex2D, uint32_t bindIndex) override
        {
            CDxTexture2D* pDxTex2D = static_cast<CDxTexture2D*>(tex2D.get());
            m_viewHandles[uint32_t(ESlotType::ST_T)][bindIndex] = pDxTex2D->m_srv.m_pCpuDescHandle;
            m_bViewTableDirty[uint32_t(ESlotType::ST_T)] = true;

            D3D12_RESOURCE_STATES stateAfter =  D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = pDxTex2D->m_dxResource.m_pResource;
            barrier.Transition.StateBefore = pDxTex2D->m_dxResource.m_resourceState;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            pDxTex2D->m_dxResource.m_resourceState = stateAfter;
            if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
            {
                pDXDevice->m_resouceBarriers.push_back(barrier);
            }
        }

        virtual void SetShaderUAV(std::shared_ptr<CTexture2D>tex2D, uint32_t bindIndex) override
        {
            CDxTexture2D* pDxTex2D = static_cast<CDxTexture2D*>(tex2D.get());
            m_viewHandles[uint32_t(ESlotType::ST_U)][bindIndex] = pDxTex2D->m_uav.m_pCpuDescHandle;
            m_bViewTableDirty[uint32_t(ESlotType::ST_U)] = true;

            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = pDxTex2D->m_dxResource.m_pResource;
            barrier.Transition.StateBefore = pDxTex2D->m_dxResource.m_resourceState;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            pDxTex2D->m_dxResource.m_resourceState = stateAfter;
            if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
            {
                pDXDevice->m_resouceBarriers.push_back(barrier);
            }
        }
        
        virtual void DispatchRayTracicing(uint32_t width, uint32_t height)
        {
            auto pCommandList = pDXDevice->m_pCmdList;

            ApplyPipelineStata();

            for (uint32_t index = 0; index < 4; index++)
            {
                ApplySlotViews(ESlotType(index));
            }

            if (pDXDevice->m_resouceBarriers.size() > 0)
            {
                pCommandList->ResourceBarrier(pDXDevice->m_resouceBarriers.size(), pDXDevice->m_resouceBarriers.data());
                pDXDevice->m_resouceBarriers.clear();
            }

            D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = CreateRayTracingDesc(width, height);
            
            pCommandList->DispatchRays(&rayDispatchDesc);
        }

    private:
        void ApplyPipelineStata()
        {
            if (m_bPipelineStateDirty)
            {
                auto pCommandList = pDXDevice->m_pCmdList;
                pCommandList->SetPipelineState1(m_pRtPipelineState);
                pCommandList->SetComputeRootSignature(m_pGlobalRootSig);
                m_bPipelineStateDirty = false;
            }
        }

        void ApplySlotViews(ESlotType slotType)
        {
            uint32_t slotIndex = uint32_t(slotType);
            if (m_bViewTableDirty[slotIndex])
            {
                CheckBinding(m_viewHandles[slotIndex]);
                auto pCommandList = pDXDevice->m_pCmdList;

                uint32_t numCopy = m_slotDescNum[slotIndex];
                uint32_t startIndex = m_rtPassDescManager.GetAndAddCurrentPassSlotStart(numCopy);
                D3D12_CPU_DESCRIPTOR_HANDLE destCPUHandle = m_rtPassDescManager.GetCPUHandle(startIndex);
                pDXDevice->m_pDevice->CopyDescriptors(1, &destCPUHandle, &numCopy, numCopy, m_viewHandles[slotIndex], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                
                D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle = m_rtPassDescManager.GetGPUHandle(startIndex);
                pCommandList->SetComputeRootDescriptorTable(m_viewSlotIndex[slotIndex], gpuDescHandle);

                m_bViewTableDirty[slotIndex] = false;
            }
        }

        D3D12_DISPATCH_RAYS_DESC CreateRayTracingDesc(uint32_t width, uint32_t height)
        {
            D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
            raytraceDesc.Width = width;
            raytraceDesc.Height = height;
            raytraceDesc.Depth = 1;

            raytraceDesc.RayGenerationShaderRecord.StartAddress = m_pShaderTable->GetGPUVirtualAddress() + 0 * ShaderTableEntrySize;
            raytraceDesc.RayGenerationShaderRecord.SizeInBytes = ShaderTableEntrySize;

            uint32_t offset = 1 * ShaderTableEntrySize;;

            uint32_t rMissShaderNum = m_nShaderNum[uint32_t(ERayShaderType::RAY_MIH)];
            if (rMissShaderNum > 0)
            {
                raytraceDesc.MissShaderTable.StartAddress = m_pShaderTable->GetGPUVirtualAddress() + offset;
                raytraceDesc.MissShaderTable.StrideInBytes = ShaderTableEntrySize;
                raytraceDesc.MissShaderTable.SizeInBytes = ShaderTableEntrySize * rMissShaderNum;

                offset += rMissShaderNum * ShaderTableEntrySize;
            }

            uint32_t rHitShaderNum = m_nShaderNum[uint32_t(ERayShaderType::RAY_AHS)] + m_nShaderNum[uint32_t(ERayShaderType::RAY_CHS)];
            if (rHitShaderNum > 0)
            {
                raytraceDesc.HitGroupTable.StartAddress = m_pShaderTable->GetGPUVirtualAddress() + offset;
                raytraceDesc.HitGroupTable.StrideInBytes = ShaderTableEntrySize;
                raytraceDesc.HitGroupTable.SizeInBytes = ShaderTableEntrySize * rHitShaderNum;
            }
            return raytraceDesc;
        }

        void ResetContext()
        {
            m_bViewTableDirty[0] = m_bViewTableDirty[1] = m_bViewTableDirty[2] = m_bViewTableDirty[3] = false;
            m_bPipelineStateDirty = false;
        }

    private:
        CDXPassDescManager m_rtPassDescManager;

        D3D12_CPU_DESCRIPTOR_HANDLE m_viewHandles[4][nHandlePerView];
        bool m_bViewTableDirty[4];
        bool m_bPipelineStateDirty;

        ID3D12RootSignaturePtr m_pGlobalRootSig;
        ID3D12StateObjectPtr m_pRtPipelineState;
        ID3D12ResourcePtr m_pShaderTable;

        uint32_t m_nShaderNum[4];
        uint32_t m_slotDescNum[4];
        uint32_t m_viewSlotIndex[4];
    };

    std::shared_ptr<CRayTracingContext> hwrtl::CreateRayTracingContext()
    {
        return std::make_shared<CDx12RayTracingContext>();
    }

    class CDxGraphicsContext : public CGraphicsContext
    {
    public:

        CDxGraphicsContext()
        {
            ID3D12Device5Ptr pDevice = pDXDevice->m_pDevice;
            m_rsPassDescManager.Init(pDevice);
            memset(m_viewHandles, 0, 4 * nHandlePerView * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
        }

        virtual void BeginRenderPasss()override
        {
            auto pCmdAlloc = pDXDevice->m_pCmdAllocator;
            auto pCommandList = pDXDevice->m_pCmdList;
            ThrowIfFailed(pCommandList->Reset(pCmdAlloc, nullptr));

            ID3D12DescriptorHeap* heaps[] = { m_rsPassDescManager.GetHeapPtr() };
            pCommandList->SetDescriptorHeaps(1, heaps);

            ResetContext();
        }

        virtual void EndRenderPasss()override
        {
            SubmitCommandlist();
            WaitForPreviousFrame();
            ResetCmdList();
            m_rsPassDescManager.ResetPassSlotStartIndex();
        }

        virtual void SetGraphicsPipelineState(std::shared_ptr<CGraphicsPipelineState>rtPipelineState)override
        {
            CDxGraphicsPipelineState* dxGraphicsPipelineState = static_cast<CDxGraphicsPipelineState*>(rtPipelineState.get());
            m_pRsGlobalRootSig = dxGraphicsPipelineState->m_pRsGlobalRootSig;
            m_pRSPipelinState = dxGraphicsPipelineState->m_pRSPipelinState;
            for (uint32_t index = 0; index < 4; index++)
            {
                m_slotDescNum[index] = dxGraphicsPipelineState->m_slotDescNum[index];
            }

            m_viewSlotIndex[0] = 0;
            for (uint32_t index = 1; index < 4; index++)
            {
                m_viewSlotIndex[index] = m_viewSlotIndex[index - 1] + m_slotDescNum[index - 1];
            }

            m_bPipelineStateDirty = true;
        }

        virtual void SetViewport(float width, float height)
        {
            m_viewPort = { 0,0,width ,height ,0,1 };
            m_scissorRect = { 0,0,static_cast<LONG>(width) ,static_cast<LONG>(height) };
            m_bViewportDirty = true;
        }

        virtual void SetRenderTargets(std::vector<std::shared_ptr<CTexture2D>> renderTargets, std::shared_ptr<CTexture2D> depthStencil = nullptr, bool bClearRT = true, bool bClearDs = true) override
        {
            for (uint32_t index = 0; index < renderTargets.size(); index++)
            {
                CDxTexture2D* dxTexture2D = static_cast<CDxTexture2D*>(renderTargets[index].get());

                m_hRenderTargets[index] = dxTexture2D->m_rtv.m_pCpuDescHandle;

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = dxTexture2D->m_dxResource.m_pResource;
                barrier.Transition.StateBefore = dxTexture2D->m_dxResource.m_resourceState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                
                dxTexture2D->m_dxResource.m_resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
                
                if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
                {
                    pDXDevice->m_resouceBarriers.push_back(barrier);
                }
            }
            m_nRenderTargetNum = renderTargets.size();
            m_bRenderTargetDirty = true;

            if (depthStencil != nullptr)
            {
                CDxTexture2D* dxDsTexture = static_cast<CDxTexture2D*>(depthStencil.get());

                m_hDepthStencil = dxDsTexture->m_dsv.m_pCpuDescHandle;

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = dxDsTexture->m_dxResource.m_pResource;
                barrier.Transition.StateBefore = dxDsTexture->m_dxResource.m_resourceState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                dxDsTexture->m_dxResource.m_resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
                {
                    pDXDevice->m_resouceBarriers.push_back(barrier);
                }
                m_bDepthStencil = true;
            }
        }
        virtual void SetShaderSRV(std::shared_ptr<CTexture2D>tex2D, uint32_t bindIndex) override
        {
            CDxTexture2D* pDxTex2D = static_cast<CDxTexture2D*>(tex2D.get());
            m_viewHandles[uint32_t(ESlotType::ST_T)][bindIndex] = pDxTex2D->m_srv.m_pCpuDescHandle;
            m_bViewTableDirty[uint32_t(ESlotType::ST_T)] = true;

            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = pDxTex2D->m_dxResource.m_pResource;
            barrier.Transition.StateBefore = pDxTex2D->m_dxResource.m_resourceState;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            pDxTex2D->m_dxResource.m_resourceState = stateAfter;
            if (barrier.Transition.StateBefore != barrier.Transition.StateAfter)
            {
                pDXDevice->m_resouceBarriers.push_back(barrier);
            }
        }

        virtual void SetConstantBuffer(std::shared_ptr<CBuffer> constantBuffer, uint32_t bindIndex)override
        {
            CDxBuffer* pDxBuffer = static_cast<CDxBuffer*>(constantBuffer.get());
            m_viewHandles[uint32_t(ESlotType::ST_B)][bindIndex] = pDxBuffer->m_cbv.m_pCpuDescHandle;
            m_bViewTableDirty[uint32_t(ESlotType::ST_B)] = true;
        }

        virtual void SetVertexBuffers(std::vector<std::shared_ptr<CBuffer>> vertexBuffers)
        {
            m_vbViews.resize(vertexBuffers.size());
            for (uint32_t index = 0; index < vertexBuffers.size(); index++)
            {
                CDxBuffer* dxVertexBuffer = static_cast<CDxBuffer*>(vertexBuffers[index].get());
                D3D12_VERTEX_BUFFER_VIEW vbView = dxVertexBuffer->m_vbv;
                m_vbViews[index] = vbView;
            }
            m_bVertexBufferDirty = true;
        }

        virtual void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t InstanceCount, uint32_t StartVertexLocation, uint32_t StartInstanceLocation) override
        {
            auto pCommandList = pDXDevice->m_pCmdList;

            ApplyPipelineState();

            for (uint32_t index = 0; index < 4; index++)
            {
                ApplySlotViews(ESlotType(index));
            }

            ApplyViewport();
            ApplyRenderTarget();

            if (pDXDevice->m_resouceBarriers.size() > 0)
            {
                pCommandList->ResourceBarrier(pDXDevice->m_resouceBarriers.size(), pDXDevice->m_resouceBarriers.data());
                pDXDevice->m_resouceBarriers.clear();
            }

            ApplyVertexBuffers();

            pCommandList->DrawInstanced(vertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
        }

    private:
        void ApplyPipelineState()
        {
            if (m_bPipelineStateDirty)
            {
                auto pCommandList = pDXDevice->m_pCmdList;
                pCommandList->SetPipelineState(m_pRSPipelinState);
                pCommandList->SetGraphicsRootSignature(m_pRsGlobalRootSig);
                m_bPipelineStateDirty = false;
            }
        }

        void ApplySlotViews(ESlotType slotType)
        {
            uint32_t slotIndex = uint32_t(slotType);
            if (m_bViewTableDirty[slotIndex])
            {
                CheckBinding(m_viewHandles[slotIndex]);
                auto pCommandList = pDXDevice->m_pCmdList;

                uint32_t numCopy = m_slotDescNum[slotIndex];
                uint32_t startIndex = m_rsPassDescManager.GetAndAddCurrentPassSlotStart(numCopy);
                D3D12_CPU_DESCRIPTOR_HANDLE destCPUHandle = m_rsPassDescManager.GetCPUHandle(startIndex);
                pDXDevice->m_pDevice->CopyDescriptors(1, &destCPUHandle, &numCopy, numCopy, m_viewHandles[slotIndex], nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle = m_rsPassDescManager.GetGPUHandle(startIndex);
                pCommandList->SetGraphicsRootDescriptorTable(m_viewSlotIndex[slotIndex], gpuDescHandle);

                m_bViewTableDirty[slotIndex] = false;
            }
        }

        void ApplyRenderTarget()
        {
            auto pCommandList = pDXDevice->m_pCmdList;
            if (m_bRenderTargetDirty || m_bDepthStencil)
            {
                if (m_bDepthStencil)
                {
                    pCommandList->OMSetRenderTargets(m_nRenderTargetNum, m_hRenderTargets, FALSE, &m_hDepthStencil);
                }
                else
                {
                    pCommandList->OMSetRenderTargets(m_nRenderTargetNum, m_hRenderTargets, FALSE, nullptr);
                }

                m_bRenderTargetDirty = false;
                m_bDepthStencil = false;
            }
        }

        void ApplyVertexBuffers()
        {
            if (m_bVertexBufferDirty)
            {
                auto pCommandList = pDXDevice->m_pCmdList;
                pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                pCommandList->IASetVertexBuffers(0, m_vbViews.size(), m_vbViews.data());
                m_bVertexBufferDirty = false;
            }
        }

        void ApplyViewport()
        {
            if (m_bViewportDirty)
            {
                auto pCommandList = pDXDevice->m_pCmdList;
                pCommandList->RSSetViewports(1, &m_viewPort);
                pCommandList->RSSetScissorRects(1, &m_scissorRect);
                m_bViewportDirty = false;
            }
        }

        void ResetContext()
        {
            m_nRenderTargetNum = 0;

            m_bViewTableDirty[0] = m_bViewTableDirty[1] = m_bViewTableDirty[2] = m_bViewTableDirty[3] = false;
            m_bPipelineStateDirty = false;
            m_bRenderTargetDirty = false;
            m_bDepthStencil = false;
            m_bVertexBufferDirty = false;
            m_bViewportDirty = false;
        }

    private:
        CDXPassDescManager m_rsPassDescManager;

        D3D12_VIEWPORT m_viewPort;
        D3D12_RECT m_scissorRect;
        ID3D12RootSignaturePtr m_pRsGlobalRootSig;
        ID3D12PipelineStatePtr m_pRSPipelinState;

        std::vector<D3D12_VERTEX_BUFFER_VIEW>m_vbViews;

        D3D12_CPU_DESCRIPTOR_HANDLE m_hRenderTargets[8];
        D3D12_CPU_DESCRIPTOR_HANDLE m_hDepthStencil;
        D3D12_CPU_DESCRIPTOR_HANDLE m_viewHandles[4][nHandlePerView];
        
        uint32_t m_nRenderTargetNum;
        uint32_t m_slotDescNum[4];
        uint32_t m_viewSlotIndex[4];
        
        bool m_bViewTableDirty[4];
        bool m_bPipelineStateDirty;
        bool m_bRenderTargetDirty;
        bool m_bDepthStencil;
        bool m_bVertexBufferDirty;
        bool m_bViewportDirty;
    };

    std::shared_ptr<CGraphicsContext> CreateGraphicsContext() 
    {
        return std::make_shared<CDxGraphicsContext>();
    }

}

#endif


