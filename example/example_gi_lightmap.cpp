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
#include "../hwrtl_gi.h"

using namespace hwrtl;
using namespace hwrtl::gi;



int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(463);
    {
        uint32_t boxVertexCount = 36;
        uint32_t boxLightMapSizeX = 256 * 3 + 2 * 2;
        uint32_t boxLightMapSizeY = 256 * 2 + 2;

        std::vector<Vec3>boxGeoPositions;
        std::vector<Vec2>boxGeoLightMapUV;

        boxGeoPositions.resize(boxVertexCount);
        boxGeoLightMapUV.resize(boxVertexCount);

        // box light map layout
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
        boxGeoPositions[0] = Vec3(-1, -1, +1); boxGeoLightMapUV[0] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[1] = Vec3(+1, -1, +1); boxGeoLightMapUV[1] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[2] = Vec3(+1, -1, -1); boxGeoLightMapUV[2] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //front triangle 2
        boxGeoPositions[3] = boxGeoPositions[0]; boxGeoLightMapUV[3] = boxGeoLightMapUV[0];
        boxGeoPositions[4] = boxGeoPositions[2]; boxGeoLightMapUV[4] = boxGeoLightMapUV[2];
        boxGeoPositions[5] = Vec3(-1, -1, -1); boxGeoLightMapUV[5] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //top triangle 1
        boxGeoPositions[6] = Vec3(-1, +1, +1); boxGeoLightMapUV[6] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[7] = Vec3(+1, +1, +1); boxGeoLightMapUV[7] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[8] = Vec3(+1, -1, +1); boxGeoLightMapUV[8] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //top triangle 2
        boxGeoPositions[9] = boxGeoPositions[6]; boxGeoLightMapUV[9] = boxGeoLightMapUV[6];
        boxGeoPositions[10] = boxGeoPositions[8]; boxGeoLightMapUV[10] = boxGeoLightMapUV[8];
        boxGeoPositions[11] = Vec3(-1, -1, +1); boxGeoLightMapUV[11] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //right triangle 1
        boxGeoPositions[12] = Vec3(+1, -1, +1); boxGeoLightMapUV[12] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[13] = Vec3(+1, +1, +1); boxGeoLightMapUV[13] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
        boxGeoPositions[14] = Vec3(+1, +1, -1); boxGeoLightMapUV[14] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //right triangle 2
        boxGeoPositions[15] = boxGeoPositions[12]; boxGeoLightMapUV[15] = boxGeoLightMapUV[12];
        boxGeoPositions[16] = boxGeoPositions[14]; boxGeoLightMapUV[16] = boxGeoLightMapUV[14];
        boxGeoPositions[17] = Vec3(+1, -1, -1); boxGeoLightMapUV[17] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);

        //back triangle 1
        boxGeoPositions[18] = Vec3(+1, +1, +1); boxGeoLightMapUV[18] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[19] = Vec3(-1, +1, +1); boxGeoLightMapUV[19] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[20] = Vec3(-1, +1, -1); boxGeoLightMapUV[20] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        //back triangle 2
        boxGeoPositions[21] = boxGeoPositions[18]; boxGeoLightMapUV[21] = boxGeoLightMapUV[18];
        boxGeoPositions[22] = boxGeoPositions[20]; boxGeoLightMapUV[22] = boxGeoLightMapUV[20];
        boxGeoPositions[23] = Vec3(+1, +1, -1); boxGeoLightMapUV[23] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        //bottom triangle 1
        boxGeoPositions[24] = Vec3(+1, +1, -1); boxGeoLightMapUV[24] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[25] = Vec3(-1, +1, -1); boxGeoLightMapUV[25] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[26] = Vec3(-1, -1, -1); boxGeoLightMapUV[26] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        //bottom triangle 2
        boxGeoPositions[27] = boxGeoPositions[24]; boxGeoLightMapUV[27] = boxGeoLightMapUV[24];
        boxGeoPositions[28] = boxGeoPositions[26]; boxGeoLightMapUV[28] = boxGeoLightMapUV[26];
        boxGeoPositions[29] = Vec3(+1, -1, -1); boxGeoLightMapUV[29] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        //left triangle 1
        boxGeoPositions[30] = Vec3(-1, +1, +1); boxGeoLightMapUV[30] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[31] = Vec3(-1, -1, +1); boxGeoLightMapUV[31] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
        boxGeoPositions[32] = Vec3(-1, -1, -1); boxGeoLightMapUV[32] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        //left triangle 2
        boxGeoPositions[33] = boxGeoPositions[30]; boxGeoLightMapUV[33] = boxGeoLightMapUV[30];
        boxGeoPositions[34] = boxGeoPositions[32]; boxGeoLightMapUV[34] = boxGeoLightMapUV[32];
        boxGeoPositions[35] = Vec3(-1, +1, -1); boxGeoLightMapUV[35] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);

        uint32_t planeVertexCount = 6;
        uint32_t planeLightMapSizeX = 384;
        uint32_t planeLightMapSizeY = 384;

        std::vector<Vec3>planeGeoPositions;
        std::vector<Vec2>planeGeoLightMapUV;

        planeGeoPositions.resize(planeVertexCount);
        planeGeoLightMapUV.resize(planeVertexCount);

        planeGeoPositions[0] = Vec3(-2, +2, -1); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
        planeGeoPositions[1] = Vec3(+2, +2, -1); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
        planeGeoPositions[2] = Vec3(+2, -2, -1); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

        planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
        planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
        planeGeoPositions[5] = Vec3(-2, -2, -1); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);

        SBakeMeshDesc boxMesh1Desc;
        boxMesh1Desc.m_pPositionData = boxGeoPositions.data();
        boxMesh1Desc.m_pLightMapUVData = boxGeoLightMapUV.data();
        boxMesh1Desc.m_nVertexCount = boxVertexCount;
        boxMesh1Desc.m_nLightMapSize = Vec2i(boxLightMapSizeX, boxLightMapSizeY);
        boxMesh1Desc.m_meshInstanceInfo.m_transform[0][3] = -0.75;

        SBakeMeshDesc boxMesh2Desc = boxMesh1Desc;
        boxMesh2Desc.m_meshInstanceInfo.m_transform[0][3] = +0.75;

        SBakeMeshDesc planeMesh1Desc;
        planeMesh1Desc.m_pPositionData = planeGeoPositions.data();
        planeMesh1Desc.m_pLightMapUVData = planeGeoLightMapUV.data();
        planeMesh1Desc.m_nVertexCount = planeVertexCount;
        planeMesh1Desc.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);
        planeMesh1Desc.m_meshInstanceInfo.m_transform[2][3] = 0.1;

        std::vector<SBakeMeshDesc> bakeMeshDescs;
        bakeMeshDescs.push_back(boxMesh1Desc);
        bakeMeshDescs.push_back(boxMesh2Desc);
        bakeMeshDescs.push_back(planeMesh1Desc);

        SBakeConfig bakeConfig;
        bakeConfig.m_maxAtlasSize = 1024;
        InitGIBaker(bakeConfig);
        AddBakeMeshs(bakeMeshDescs);
        PrePareLightMapGBufferPass();
        ExecuteLightMapGBufferPass();
        PrePareVisualizeResultPass();
        ExecuteVisualizeResultPass();
        DeleteGIBaker();
    }
    _CrtDumpMemoryLeaks();
    return 0;
}
