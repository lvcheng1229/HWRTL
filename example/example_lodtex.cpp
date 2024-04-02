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

#include <iostream>
#include "../hwrtl_lodtex.h"

using namespace hwrtl;
using namespace hwrtl::hlod;

#pragma warning (disable: 4996)

std::vector<uint8_t> GenerateTextureData0(uint32_t texWidth, uint32_t texHeight, uint32_t texPixelSize)
{
    const uint32_t rowPitch = texWidth * texPixelSize;
    const uint32_t cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const uint32_t cellHeight = texWidth >> 3;    // The height of a cell in the checkerboard texture.
    const uint32_t textureSize = rowPitch * texHeight;

    std::vector<uint8_t> data(textureSize);
    uint8_t* pData = &data[0];

    for (uint32_t n = 0; n < textureSize; n += texPixelSize)
    {
        uint32_t x = n % rowPitch;
        uint32_t y = n / rowPitch;
        uint32_t i = x / cellPitch;
        uint32_t j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}

std::vector<uint8_t> GenerateTextureData1(uint32_t texWidth, uint32_t texHeight, uint32_t texPixelSize)
{
    const uint32_t rowPitch = texWidth * texPixelSize;
    const uint32_t cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const uint32_t cellHeight = texWidth >> 3;    // The height of a cell in the checkerboard texture.
    const uint32_t textureSize = rowPitch * texHeight;

    std::vector<uint8_t> data(textureSize);
    uint8_t* pData = &data[0];

    for (uint32_t n = 0; n < textureSize; n += texPixelSize)
    {
        uint32_t x = n % rowPitch;
        uint32_t y = n / rowPitch;
        uint32_t i = x / cellPitch;
        uint32_t j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}

void CreateBoxMesh(uint32_t boxVertexCount, uint32_t boxTexSizeX, uint32_t boxTexSizeY, std::vector<Vec3>& boxGeoPositions, std::vector<Vec2>& boxGeoUVs)
{
    boxGeoPositions.resize(boxVertexCount);
    boxGeoUVs.resize(boxVertexCount);

    // box uvlayout
    //----------------------------------------------------------------------------------------------------------------------------
    //                  front triangle 1 | padding |                top triangle 1 | padding |                  right triangle 1 |
    // front triangle 2                  | padding | top triangle 2                | padding | right triangle 2                  |
    // ----------------------------------|---------|------------------------------------------------------------------------------
    // padding
    //----------------------------------------------------------------------------------------------------------------------------
    //                   back triangle 1 | padding |              bottom triangle 1 | padding |                   left triangle 1 |
    // back triangle 2                   | padding | bottom triangle 2              | padding | left triangle 2                   |
    // ----------------------------------|---------|------------------------------------------------------------------------------

    //front triangle 1
    boxGeoPositions[0] = Vec3(-1, -1, +1); boxGeoUVs[0] = Vec2((256.0 * 0.0 + 0.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[1] = Vec3(+1, -1, +1); boxGeoUVs[1] = Vec2((256.0 * 1.0 + 0.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[2] = Vec3(+1, -1, -1); boxGeoUVs[2] = Vec2((256.0 * 1.0 + 0.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);

    //front triangle 2
    boxGeoPositions[3] = boxGeoPositions[0]; boxGeoUVs[3] = boxGeoUVs[0];
    boxGeoPositions[4] = boxGeoPositions[2]; boxGeoUVs[4] = boxGeoUVs[2];
    boxGeoPositions[5] = Vec3(-1, -1, -1); boxGeoUVs[5] = Vec2((256.0 * 0.0 + 0.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);
    
    //top triangle 1
    boxGeoPositions[6] = Vec3(-1, +1, +1); boxGeoUVs[6] = Vec2((256.0 * 1.0 + 2.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[7] = Vec3(+1, +1, +1); boxGeoUVs[7] = Vec2((256.0 * 2.0 + 2.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[8] = Vec3(+1, -1, +1); boxGeoUVs[8] = Vec2((256.0 * 2.0 + 2.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);

    //top triangle 2
    boxGeoPositions[9] = boxGeoPositions[6]; boxGeoUVs[9] = boxGeoUVs[6];
    boxGeoPositions[10] = boxGeoPositions[8]; boxGeoUVs[10] = boxGeoUVs[8];
    boxGeoPositions[11] = Vec3(-1, -1, +1); boxGeoUVs[11] = Vec2((256.0 * 1.0 + 2.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);

    //right triangle 1
    boxGeoPositions[12] = Vec3(+1, -1, +1); boxGeoUVs[12] = Vec2((256.0 * 2.0 + 4.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[13] = Vec3(+1, +1, +1); boxGeoUVs[13] = Vec2((256.0 * 3.0 + 4.0) / boxTexSizeX, (256.0 * 0.0 + 0.0) / boxTexSizeY);
    boxGeoPositions[14] = Vec3(+1, +1, -1); boxGeoUVs[14] = Vec2((256.0 * 3.0 + 4.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);

    //right triangle 2
    boxGeoPositions[15] = boxGeoPositions[12]; boxGeoUVs[15] = boxGeoUVs[12];
    boxGeoPositions[16] = boxGeoPositions[14]; boxGeoUVs[16] = boxGeoUVs[14];
    boxGeoPositions[17] = Vec3(+1, -1, -1); boxGeoUVs[17] = Vec2((256.0 * 2.0 + 4.0) / boxTexSizeX, (256.0 * 1.0 + 0.0) / boxTexSizeY);

    //back triangle 1
    boxGeoPositions[18] = Vec3(+1, +1, +1); boxGeoUVs[18] = Vec2((256.0 * 0.0 + 0.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[19] = Vec3(-1, +1, +1); boxGeoUVs[19] = Vec2((256.0 * 1.0 + 0.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[20] = Vec3(-1, +1, -1); boxGeoUVs[20] = Vec2((256.0 * 1.0 + 0.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);

    //back triangle 2
    boxGeoPositions[21] = boxGeoPositions[18]; boxGeoUVs[21] = boxGeoUVs[18];
    boxGeoPositions[22] = boxGeoPositions[20]; boxGeoUVs[22] = boxGeoUVs[20];
    boxGeoPositions[23] = Vec3(+1, +1, -1); boxGeoUVs[23] = Vec2((256.0 * 0.0 + 0.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);

    //bottom triangle 1
    boxGeoPositions[24] = Vec3(+1, +1, -1); boxGeoUVs[24] = Vec2((256.0 * 1.0 + 2.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[25] = Vec3(-1, +1, -1); boxGeoUVs[25] = Vec2((256.0 * 2.0 + 2.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[26] = Vec3(-1, -1, -1); boxGeoUVs[26] = Vec2((256.0 * 2.0 + 2.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);

    //bottom triangle 2
    boxGeoPositions[27] = boxGeoPositions[24]; boxGeoUVs[27] = boxGeoUVs[24];
    boxGeoPositions[28] = boxGeoPositions[26]; boxGeoUVs[28] = boxGeoUVs[26];
    boxGeoPositions[29] = Vec3(+1, -1, -1); boxGeoUVs[29] = Vec2((256.0 * 1.0 + 2.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);

    //left triangle 1
    boxGeoPositions[30] = Vec3(-1, +1, +1); boxGeoUVs[30] = Vec2((256.0 * 2.0 + 4.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[31] = Vec3(-1, -1, +1); boxGeoUVs[31] = Vec2((256.0 * 3.0 + 4.0) / boxTexSizeX, (256.0 * 1.0 + 2.0) / boxTexSizeY);
    boxGeoPositions[32] = Vec3(-1, -1, -1); boxGeoUVs[32] = Vec2((256.0 * 3.0 + 4.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);

    //left triangle 2
    boxGeoPositions[33] = boxGeoPositions[30]; boxGeoUVs[33] = boxGeoUVs[30];
    boxGeoPositions[34] = boxGeoPositions[32]; boxGeoUVs[34] = boxGeoUVs[32];
    boxGeoPositions[35] = Vec3(-1, +1, -1); boxGeoUVs[35] = Vec2((256.0 * 2.0 + 4.0) / boxTexSizeX, (256.0 * 2.0 + 2.0) / boxTexSizeY);
}

std::vector<Vec3>boxBakeGeoPositions;
std::vector<Vec2>boxBakeGeoUVs;

std::vector<Vec3>box1BakeHighPolyGeoPositions;
std::vector<Vec2>box1BakeHighPolyGeoUVs;

std::vector<Vec3>box2BakeHighPolyGeoPositions;
std::vector<Vec2>box2BakeHighPolyGeoUVs;

std::vector<uint8_t> tex0DataGlobal;
std::vector<uint8_t> tex1DataGlobal;

void CreateAndAddScene(SHLODBakerMeshDesc& bakeMeshDescs, std::vector<SHLODHighPolyMeshDesc>& highPolyMeshs)
{
    uint32_t boxVertexCount = 36;
    uint32_t boxTexSizeX = 256 * 3 + 2 * 2;
    uint32_t boxTexSizeY = 256 * 2 + 2;

    {
        CreateBoxMesh(boxVertexCount, boxTexSizeX, boxTexSizeY, boxBakeGeoPositions, boxBakeGeoUVs);
        bakeMeshDescs.m_pPositionData = boxBakeGeoPositions.data();
        bakeMeshDescs.m_pUVData = boxBakeGeoUVs.data();
        bakeMeshDescs.m_nVertexCount = boxVertexCount;
        bakeMeshDescs.m_bakedTextureSize = Vec2i(boxTexSizeX, boxTexSizeY);
        bakeMeshDescs.m_meshInstanceInfo = SMeshInstanceInfo();
        bakeMeshDescs.m_meshInstanceInfo.m_instanceFlag = EInstanceFlag::CULL_DISABLE;
    }
    
    //highPolyMeshs.resize(2);
    highPolyMeshs.resize(1);

    tex0DataGlobal = GenerateTextureData0(boxTexSizeX, boxTexSizeY, 4);
    tex1DataGlobal = GenerateTextureData1(boxTexSizeX, boxTexSizeY, 4);

    STextureCreateDesc tex0Desc;
    tex0Desc.m_width = boxTexSizeX;
    tex0Desc.m_height = boxTexSizeY;
    tex0Desc.m_eTexFormat = ETexFormat::FT_RGBA8_UNORM;
    tex0Desc.m_eTexUsage = ETexUsage::USAGE_SRV;
    tex0Desc.m_srcData = tex0DataGlobal.data();
    std::shared_ptr<CTexture2D> highPolyTexture2D0 = CreateHighPolyTexture2D(tex0Desc);

    STextureCreateDesc tex1Desc;
    tex1Desc.m_width = boxTexSizeX;
    tex1Desc.m_height = boxTexSizeY;
    tex1Desc.m_eTexFormat = ETexFormat::FT_RGBA8_UNORM;
    tex1Desc.m_eTexUsage = ETexUsage::USAGE_SRV;
    tex1Desc.m_srcData = tex1DataGlobal.data();
    std::shared_ptr<CTexture2D> highPolyTexture2D1 = CreateHighPolyTexture2D(tex1Desc);
    

    {
        CreateBoxMesh(boxVertexCount, boxTexSizeX, boxTexSizeY, box1BakeHighPolyGeoPositions, box1BakeHighPolyGeoUVs);
        SHLODHighPolyMeshDesc highPolyMesh1;
        highPolyMesh1.m_pPositionData = box1BakeHighPolyGeoPositions.data();
        highPolyMesh1.m_pUVData = box1BakeHighPolyGeoUVs.data();
        highPolyMesh1.m_nVertexCount = boxVertexCount;
        highPolyMesh1.m_meshInstanceInfo = SMeshInstanceInfo();
        highPolyMesh1.m_meshInstanceInfo.m_instanceFlag = EInstanceFlag::CULL_DISABLE;
        highPolyMesh1.m_pBaseColorTexture = highPolyTexture2D0;
        highPolyMesh1.m_pNormalTexture = highPolyTexture2D1;

        // Scale
        //highPolyMesh1.m_meshInstanceInfo.m_transform[0][0] = 1.5;
        //highPolyMesh1.m_meshInstanceInfo.m_transform[1][1] = 1.5;
        //highPolyMesh1.m_meshInstanceInfo.m_transform[2][2] = 1.5;
        highPolyMeshs[0] = highPolyMesh1;
    }

    //{
    //    CreateBoxMesh(boxVertexCount, boxTexSizeX, boxTexSizeY, box2BakeHighPolyGeoPositions, box2BakeHighPolyGeoUVs);
    //    SHLODHighPolyMeshDesc highPolyMesh2;
    //    highPolyMesh2.m_pPositionData = box1BakeHighPolyGeoPositions.data();
    //    highPolyMesh2.m_pUVData = box1BakeHighPolyGeoUVs.data();
    //    highPolyMesh2.m_nVertexCount = boxVertexCount;
    //    highPolyMesh2.m_meshInstanceInfo = SMeshInstanceInfo();
    //    highPolyMesh2.m_meshInstanceInfo.m_instanceFlag = EInstanceFlag::CULL_DISABLE;
    //    highPolyMesh2.m_pBaseColorTexture = highPolyTexture2D0;
    //    highPolyMesh2.m_pNormalTexture = highPolyTexture2D1;
    //
    //    // Scale
    //    highPolyMesh2.m_meshInstanceInfo.m_transform[0][0] = 0.5;
    //    highPolyMesh2.m_meshInstanceInfo.m_transform[1][1] = 0.5;
    //    highPolyMesh2.m_meshInstanceInfo.m_transform[2][2] = 0.5;
    //
    //    highPolyMesh2.m_meshInstanceInfo.m_transform[0][3] = 1.5;
    //
    //    highPolyMeshs[1] = highPolyMesh2;
    //}
   
}


void LodTexExampleDestroyScene()
{
    boxBakeGeoPositions.~vector();
    box1BakeHighPolyGeoPositions.~vector();
    box1BakeHighPolyGeoUVs.~vector();
    box2BakeHighPolyGeoPositions.~vector();
    box2BakeHighPolyGeoUVs.~vector();
    tex0DataGlobal.~vector();
    tex1DataGlobal.~vector();
}

int main()
//int LodTexExampleEntry()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetBreakAlloc(174);
    {
        SHLODConfig hlodConfig;
        hlodConfig.m_nHLODTextureSize = Vec2i(1024,1024);

        InitHLODTextureBaker(hlodConfig);
        SHLODBakerMeshDesc bakeMeshDescs; 
        std::vector<SHLODHighPolyMeshDesc> highPolyMeshs;
        CreateAndAddScene(bakeMeshDescs, highPolyMeshs);
        SetHLODMesh(bakeMeshDescs);
        SetHighPolyMesh(highPolyMeshs);
        BakeHLODTexture();
        LodTexExampleDestroyScene();
        DeleteHLODBaker();
    }
    _CrtDumpMemoryLeaks();
    return 0;
}