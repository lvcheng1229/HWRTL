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


void CreateBoxMesh(uint32_t boxVertexCount, uint32_t boxLightMapSizeX, uint32_t boxLightMapSizeY, std::vector<Vec3>& boxGeoPositions, std::vector<Vec2>&boxGeoLightMapUV, std::vector<Vec3>& normals)
{
    boxGeoPositions.resize(boxVertexCount);
    boxGeoLightMapUV.resize(boxVertexCount);
    normals.resize(boxVertexCount);

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
    normals[0] = normals[1] = normals[2] = Vec3(0, -1, 0);


    //front triangle 2
    boxGeoPositions[3] = boxGeoPositions[0]; boxGeoLightMapUV[3] = boxGeoLightMapUV[0];
    boxGeoPositions[4] = boxGeoPositions[2]; boxGeoLightMapUV[4] = boxGeoLightMapUV[2];
    boxGeoPositions[5] = Vec3(-1, -1, -1); boxGeoLightMapUV[5] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);
    normals[3] = normals[4] = normals[5] = Vec3(0, -1, 0);

    //top triangle 1
    boxGeoPositions[6] = Vec3(-1, +1, +1); boxGeoLightMapUV[6] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
    boxGeoPositions[7] = Vec3(+1, +1, +1); boxGeoLightMapUV[7] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
    boxGeoPositions[8] = Vec3(+1, -1, +1); boxGeoLightMapUV[8] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);
    normals[6] = normals[7] = normals[8] = Vec3(0, 0, 1);

    //top triangle 2
    boxGeoPositions[9] = boxGeoPositions[6]; boxGeoLightMapUV[9] = boxGeoLightMapUV[6];
    boxGeoPositions[10] = boxGeoPositions[8]; boxGeoLightMapUV[10] = boxGeoLightMapUV[8];
    boxGeoPositions[11] = Vec3(-1, -1, +1); boxGeoLightMapUV[11] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);
    normals[9] = normals[10] = normals[11] = Vec3(0, 0, 1);

    //right triangle 1
    boxGeoPositions[12] = Vec3(+1, -1, +1); boxGeoLightMapUV[12] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
    boxGeoPositions[13] = Vec3(+1, +1, +1); boxGeoLightMapUV[13] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 0.0 + 0.0) / boxLightMapSizeY);
    boxGeoPositions[14] = Vec3(+1, +1, -1); boxGeoLightMapUV[14] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);
    normals[12] = normals[13] = normals[14] = Vec3(1, 0, 0);

    //right triangle 2
    boxGeoPositions[15] = boxGeoPositions[12]; boxGeoLightMapUV[15] = boxGeoLightMapUV[12];
    boxGeoPositions[16] = boxGeoPositions[14]; boxGeoLightMapUV[16] = boxGeoLightMapUV[14];
    boxGeoPositions[17] = Vec3(+1, -1, -1); boxGeoLightMapUV[17] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 0.0) / boxLightMapSizeY);
    normals[15] = normals[16] = normals[17] = Vec3(1, 0, 0);

    //back triangle 1
    boxGeoPositions[18] = Vec3(+1, +1, +1); boxGeoLightMapUV[18] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[19] = Vec3(-1, +1, +1); boxGeoLightMapUV[19] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[20] = Vec3(-1, +1, -1); boxGeoLightMapUV[20] = Vec2((256.0 * 1.0 + 0.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[18] = normals[19] = normals[20] = Vec3(0, 1, 0);

    //back triangle 2
    boxGeoPositions[21] = boxGeoPositions[18]; boxGeoLightMapUV[21] = boxGeoLightMapUV[18];
    boxGeoPositions[22] = boxGeoPositions[20]; boxGeoLightMapUV[22] = boxGeoLightMapUV[20];
    boxGeoPositions[23] = Vec3(+1, +1, -1); boxGeoLightMapUV[23] = Vec2((256.0 * 0.0 + 0.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[21] = normals[22] = normals[23] = Vec3(0, 1, 0);

    //bottom triangle 1
    boxGeoPositions[24] = Vec3(+1, +1, -1); boxGeoLightMapUV[24] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[25] = Vec3(-1, +1, -1); boxGeoLightMapUV[25] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[26] = Vec3(-1, -1, -1); boxGeoLightMapUV[26] = Vec2((256.0 * 2.0 + 2.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[24] = normals[25] = normals[26] = Vec3(0, 0, -1);

    //bottom triangle 2
    boxGeoPositions[27] = boxGeoPositions[24]; boxGeoLightMapUV[27] = boxGeoLightMapUV[24];
    boxGeoPositions[28] = boxGeoPositions[26]; boxGeoLightMapUV[28] = boxGeoLightMapUV[26];
    boxGeoPositions[29] = Vec3(+1, -1, -1); boxGeoLightMapUV[29] = Vec2((256.0 * 1.0 + 2.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[27] = normals[28] = normals[29] = Vec3(0, 0, -1);

    //left triangle 1
    boxGeoPositions[30] = Vec3(-1, +1, +1); boxGeoLightMapUV[30] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[31] = Vec3(-1, -1, +1); boxGeoLightMapUV[31] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 1.0 + 2.0) / boxLightMapSizeY);
    boxGeoPositions[32] = Vec3(-1, -1, -1); boxGeoLightMapUV[32] = Vec2((256.0 * 3.0 + 4.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[30] = normals[31] = normals[32] = Vec3(-1, 0, 0);

    //left triangle 2
    boxGeoPositions[33] = boxGeoPositions[30]; boxGeoLightMapUV[33] = boxGeoLightMapUV[30];
    boxGeoPositions[34] = boxGeoPositions[32]; boxGeoLightMapUV[34] = boxGeoLightMapUV[32];
    boxGeoPositions[35] = Vec3(-1, +1, -1); boxGeoLightMapUV[35] = Vec2((256.0 * 2.0 + 4.0) / boxLightMapSizeX, (256.0 * 2.0 + 2.0) / boxLightMapSizeY);
    normals[33] = normals[34] = normals[35] = Vec3(-1, 0, 0);
}

void CreateBottomPlaneMesh(uint32_t planeVertexCount, uint32_t planeLightMapSizeX, uint32_t planeLightMapSizeY, std::vector<Vec3>& planeGeoPositions, std::vector<Vec2>&planeGeoLightMapUV, std::vector<Vec3>& normals)
{
    planeGeoPositions.resize(planeVertexCount);
    planeGeoLightMapUV.resize(planeVertexCount);
    normals.resize(planeVertexCount);

    for (uint32_t index = 0; index < 6; index++)
    {
        normals[index] = Vec3(0, 0, 1);
    }

    planeGeoPositions[0] = Vec3(-4, +4, 0); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
    planeGeoPositions[1] = Vec3(+4, +4, 0); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
    planeGeoPositions[2] = Vec3(+4, -4, 0); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

    planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
    planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
    planeGeoPositions[5] = Vec3(-4, -4, 0); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);
}

void CreateTopPlaneMesh(uint32_t planeVertexCount, uint32_t planeLightMapSizeX, uint32_t planeLightMapSizeY, std::vector<Vec3>& planeGeoPositions, std::vector<Vec2>& planeGeoLightMapUV, std::vector<Vec3>& normals)
{
    planeGeoPositions.resize(planeVertexCount);
    planeGeoLightMapUV.resize(planeVertexCount);

    normals.resize(planeVertexCount);

    for (uint32_t index = 0; index < 6; index++)
    {
        normals[index] = Vec3(0, 0, -1);
    }

    planeGeoPositions[0] = Vec3(-4, -4, 12); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
    planeGeoPositions[1] = Vec3(+4, -4, 12); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
    planeGeoPositions[2] = Vec3(+4,  4, 12); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

    planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
    planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
    planeGeoPositions[5] = Vec3(-4, 4, 12); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);
}

void CreateLeftPlaneMesh(uint32_t planeVertexCount, uint32_t planeLightMapSizeX, uint32_t planeLightMapSizeY, std::vector<Vec3>& planeGeoPositions, std::vector<Vec2>& planeGeoLightMapUV, std::vector<Vec3>& normals)
{
    planeGeoPositions.resize(planeVertexCount);
    planeGeoLightMapUV.resize(planeVertexCount);

    normals.resize(planeVertexCount);

    for (uint32_t index = 0; index < 6; index++)
    {
        normals[index] = Vec3(1, 0, 0);
    }

    planeGeoPositions[0] = Vec3(-4, -4, 12); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
    planeGeoPositions[1] = Vec3(-4, +4, 12); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
    planeGeoPositions[2] = Vec3(-4, +4, 0); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

    planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
    planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
    planeGeoPositions[5] = Vec3(-4, -4, 0); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);
}

void CreateRightPlaneMesh(uint32_t planeVertexCount, uint32_t planeLightMapSizeX, uint32_t planeLightMapSizeY, std::vector<Vec3>& planeGeoPositions, std::vector<Vec2>& planeGeoLightMapUV, std::vector<Vec3>& normals)
{
    planeGeoPositions.resize(planeVertexCount);
    planeGeoLightMapUV.resize(planeVertexCount);

    normals.resize(planeVertexCount);

    for (uint32_t index = 0; index < 6; index++)
    {
        normals[index] = Vec3(-1, 0, 0);
    }

    planeGeoPositions[0] = Vec3(+4, +4, 12); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
    planeGeoPositions[1] = Vec3(+4, -4, 12); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
    planeGeoPositions[2] = Vec3(+4, -4, 0); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

    planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
    planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
    planeGeoPositions[5] = Vec3(+4, +4, 0); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);
}

void CreateBackPlaneMesh(uint32_t planeVertexCount, uint32_t planeLightMapSizeX, uint32_t planeLightMapSizeY, std::vector<Vec3>& planeGeoPositions, std::vector<Vec2>& planeGeoLightMapUV, std::vector<Vec3>& normals)
{
    planeGeoPositions.resize(planeVertexCount);
    planeGeoLightMapUV.resize(planeVertexCount);
    normals.resize(planeVertexCount);

    for (uint32_t index = 0; index < 6; index++)
    {
        normals[index] = Vec3(0, -1, 0);
    }

    planeGeoPositions[0] = Vec3(-4, +4, 12); planeGeoLightMapUV[0] = Vec2(0.0, 0.0);
    planeGeoPositions[1] = Vec3(+4, +4, 12); planeGeoLightMapUV[1] = Vec2(1.0, 0.0);
    planeGeoPositions[2] = Vec3(+4, +4, 0); planeGeoLightMapUV[2] = Vec2(1.0, 1.0);

    planeGeoPositions[3] = planeGeoPositions[0]; planeGeoLightMapUV[3] = planeGeoLightMapUV[0];
    planeGeoPositions[4] = planeGeoPositions[2]; planeGeoLightMapUV[4] = planeGeoLightMapUV[2];
    planeGeoPositions[5] = Vec3(-4, +4, 0); planeGeoLightMapUV[5] = Vec2(0.0, 1.0);
}

std::vector<Vec3>boxGeoPositions;
std::vector<Vec2>boxGeoLightMapUV;
std::vector<Vec3>boxGeoNormals;

std::vector<Vec3>bottomPlaneGeoPositions;
std::vector<Vec2>bottomPlaneGeoLightMapUV;
std::vector<Vec3>bottomPlaneGeoNormals;

std::vector<Vec3>topPlaneGeoPositions;
std::vector<Vec2>topPlaneGeoLightMapUV;
std::vector<Vec3>topPlaneGeoNormals;

std::vector<Vec3>leftPlaneGeoPositions;
std::vector<Vec2>leftPlaneGeoLightMapUV;
std::vector<Vec3>leftPlaneGeoNormals;

std::vector<Vec3>rightPlaneGeoPositions;
std::vector<Vec2>rightPlaneGeoLightMapUV;
std::vector<Vec3>rightPlaneGeoNormals;

std::vector<Vec3>backPlaneGeoPositions;
std::vector<Vec2>backPlaneGeoLightMapUV;
std::vector<Vec3>backPlaneGeoNormals;

void CreateAndAddScene(std::vector<SBakeMeshDesc>& bakeMeshDescs)
{
    uint32_t boxVertexCount = 36;
    uint32_t boxLightMapSizeX = 256 * 3 + 2 * 2;
    uint32_t boxLightMapSizeY = 256 * 2 + 2;
    CreateBoxMesh(boxVertexCount, boxLightMapSizeX, boxLightMapSizeY, boxGeoPositions, boxGeoLightMapUV, boxGeoNormals);

    uint32_t planeVertexCount = 6;
    uint32_t planeLightMapSizeX = 256;
    uint32_t planeLightMapSizeY = 256;
    CreateBottomPlaneMesh(planeVertexCount, planeLightMapSizeX, planeLightMapSizeY, bottomPlaneGeoPositions, bottomPlaneGeoLightMapUV, bottomPlaneGeoNormals);
    CreateTopPlaneMesh(planeVertexCount, planeLightMapSizeX, planeLightMapSizeY, topPlaneGeoPositions, topPlaneGeoLightMapUV, topPlaneGeoNormals);
    CreateLeftPlaneMesh(planeVertexCount, planeLightMapSizeX, planeLightMapSizeY, leftPlaneGeoPositions, leftPlaneGeoLightMapUV, leftPlaneGeoNormals);
    CreateRightPlaneMesh(planeVertexCount, planeLightMapSizeX, planeLightMapSizeY, rightPlaneGeoPositions, rightPlaneGeoLightMapUV, rightPlaneGeoNormals);
    CreateBackPlaneMesh(planeVertexCount, planeLightMapSizeX, planeLightMapSizeY, backPlaneGeoPositions, backPlaneGeoLightMapUV, backPlaneGeoNormals);

    SBakeMeshDesc leftBox;
    leftBox.m_meshInstanceInfo = SMeshInstanceInfo();
    leftBox.m_pPositionData = boxGeoPositions.data();
    leftBox.m_pLightMapUVData = boxGeoLightMapUV.data();
    leftBox.m_pNormalData = boxGeoNormals.data();
    leftBox.m_nVertexCount = boxVertexCount;
    leftBox.m_nLightMapSize = Vec2i(boxLightMapSizeX, boxLightMapSizeY);
    //scale
    leftBox.m_meshInstanceInfo.m_transform[2][2] = 4; // size z = 2 * 4 
    //translate (-2,2,6)
    leftBox.m_meshInstanceInfo.m_transform[0][3] = -2;
    leftBox.m_meshInstanceInfo.m_transform[1][3] = 2;
    leftBox.m_meshInstanceInfo.m_transform[2][3] = 6;

    SBakeMeshDesc rightBox = leftBox;
    rightBox.m_meshInstanceInfo = SMeshInstanceInfo();
    //scale
    rightBox.m_meshInstanceInfo.m_transform[2][2] = 2; // size z = 2 * 2
    //translate (2,-2,2)
    rightBox.m_meshInstanceInfo.m_transform[0][3] = 2;
    rightBox.m_meshInstanceInfo.m_transform[1][3] = -2;
    rightBox.m_meshInstanceInfo.m_transform[2][3] = 2;

    SBakeMeshDesc bottomPlane;
    bottomPlane.m_meshInstanceInfo = SMeshInstanceInfo();
    bottomPlane.m_pPositionData = bottomPlaneGeoPositions.data();
    bottomPlane.m_pLightMapUVData = bottomPlaneGeoLightMapUV.data();
    bottomPlane.m_pNormalData = bottomPlaneGeoNormals.data();
    bottomPlane.m_nVertexCount = planeVertexCount;
    bottomPlane.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);

    SBakeMeshDesc topPlane;
    topPlane.m_meshInstanceInfo = SMeshInstanceInfo();
    topPlane.m_pPositionData = topPlaneGeoPositions.data();
    topPlane.m_pLightMapUVData = topPlaneGeoLightMapUV.data();
    topPlane.m_pNormalData = topPlaneGeoNormals.data();
    topPlane.m_nVertexCount = planeVertexCount;
    topPlane.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);

    SBakeMeshDesc leftPlane;
    leftPlane.m_meshInstanceInfo = SMeshInstanceInfo();
    leftPlane.m_pPositionData = leftPlaneGeoPositions.data();
    leftPlane.m_pLightMapUVData = leftPlaneGeoLightMapUV.data();
    leftPlane.m_pNormalData = leftPlaneGeoNormals.data();
    leftPlane.m_nVertexCount = planeVertexCount;
    leftPlane.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);

    SBakeMeshDesc rightPlane;
    rightPlane.m_meshInstanceInfo = SMeshInstanceInfo();
    rightPlane.m_pPositionData = rightPlaneGeoPositions.data();
    rightPlane.m_pLightMapUVData = rightPlaneGeoLightMapUV.data();
    rightPlane.m_pNormalData = rightPlaneGeoNormals.data();
    rightPlane.m_nVertexCount = planeVertexCount;
    rightPlane.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);

    SBakeMeshDesc backPlane;
    backPlane.m_meshInstanceInfo = SMeshInstanceInfo();
    backPlane.m_pPositionData = backPlaneGeoPositions.data();
    backPlane.m_pLightMapUVData = backPlaneGeoLightMapUV.data();
    backPlane.m_pNormalData = backPlaneGeoNormals.data();
    backPlane.m_nVertexCount = planeVertexCount;
    backPlane.m_nLightMapSize = Vec2i(planeLightMapSizeX, planeLightMapSizeY);

    bakeMeshDescs.push_back(leftBox);
    bakeMeshDescs.push_back(rightBox);
    bakeMeshDescs.push_back(bottomPlane);
    bakeMeshDescs.push_back(topPlane);
    bakeMeshDescs.push_back(leftPlane);
    bakeMeshDescs.push_back(rightPlane);
    bakeMeshDescs.push_back(backPlane);
}

void ExampleDestroyScene()
{
    boxGeoPositions.~vector();
    boxGeoLightMapUV.~vector();
    boxGeoNormals.~vector();

    bottomPlaneGeoPositions.~vector();
    bottomPlaneGeoLightMapUV.~vector();
    bottomPlaneGeoNormals.~vector();

    topPlaneGeoPositions.~vector();
    topPlaneGeoLightMapUV.~vector();
    topPlaneGeoNormals.~vector();

    leftPlaneGeoPositions.~vector();
    leftPlaneGeoLightMapUV.~vector();
    leftPlaneGeoNormals.~vector();

    rightPlaneGeoPositions.~vector();
    rightPlaneGeoLightMapUV.~vector();
    rightPlaneGeoNormals.~vector();

    backPlaneGeoPositions.~vector();
    backPlaneGeoLightMapUV.~vector();
    backPlaneGeoNormals.~vector();
}

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(194);
    {
        std::vector<SBakeMeshDesc> sceneMesh;
        CreateAndAddScene(sceneMesh);

        SBakeConfig bakeConfig;
        bakeConfig.m_maxAtlasSize = 1024;
        bakeConfig.m_bDebugRayTracing = false;
        bakeConfig.m_bAddVisualizePass = true;
        InitGIBaker(bakeConfig);
        AddBakeMeshsAndCreateVB(sceneMesh);

        // 93 lm / ( 4 * PI) 
        float intensity = 93.0f / (4.0f * 3.1415926535f);
        Vec3 lightIntensity(intensity, intensity, intensity);
        AddDirectionalLight(lightIntensity, Vec3(-1, -1, 1), false);

        Vec3 spherlight1Intensity(intensity * 0.5, intensity * 0.15, intensity * 0.15);
        AddSphereLight(spherlight1Intensity, Vec3(-3.5, 3.5, 11.5), false, 5.0, 1.0);

        Vec3 spherlight2Intensity(intensity * 0.15, intensity * 0.5, intensity * 0.5);
        AddSphereLight(spherlight2Intensity, Vec3(-3.0, -3.0, 0.5), false, 5.0, 1.0);

        Vec3 spherlight3Intensity(intensity * 0.5, intensity * 0.5, intensity * 0.15);
        AddSphereLight(spherlight3Intensity, Vec3(2.5, 0, 6.5), false, 5.0, 1.0);

        PrePareLightMapGBufferPass();
        ExecuteLightMapGBufferPass();
        PrePareLightMapRayTracingPass();
        ExecuteLightMapRayTracingPass();
        PrePareVisualizeResultPass();
        ExecuteVisualizeResultPass();
        DeleteGIBaker();
        ExampleDestroyScene();
    }
    _CrtDumpMemoryLeaks();
    return 0;
}
