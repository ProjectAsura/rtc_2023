//-----------------------------------------------------------------------------
// File : ModelOBJ.cpp
// Desc : Wavefront Alias OBJ format.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <fstream>
#include <tuple>            // for tie
#include <algorithm>        // for stable_sort.
#include <ModelOBJ.h>
#include <fnd/asdxLogger.h>
#include <fnd/asdxMisc.h>


namespace {

///////////////////////////////////////////////////////////////////////////////
// SubsetOBJ structure
///////////////////////////////////////////////////////////////////////////////
struct SubsetOBJ 
{
    std::string     MeshName;
    std::string     Material;
    uint32_t        IndexStart;
    uint32_t        IndexCount;
};

///////////////////////////////////////////////////////////////////////////////
// IndexOBJ structure
///////////////////////////////////////////////////////////////////////////////
struct IndexOBJ
{
    uint32_t    P;      //!< 位置.
    uint32_t    N;      //!< 法線.
    uint32_t    U;      //!< テクスチャ座標.
};

//-----------------------------------------------------------------------------
//      法線ベクトルを計算します.
//-----------------------------------------------------------------------------
void CalcNormals(MeshOBJ& mesh)
{
    auto vertexCount = mesh.Positions.size();
    std::vector<asdx::Vector3> normals;
    normals.resize(vertexCount);

    // 法線データ初期化.
    for(size_t i=0; i<vertexCount; ++i)
    {
        normals[i] = asdx::Vector3(0.0f, 0.0f, 0.0f);
    }

    auto indexCount = mesh.Indices.size();
    for(size_t i=0; i<indexCount; i+=3)
    {
        auto i0 = mesh.Indices[i + 0];
        auto i1 = mesh.Indices[i + 1];
        auto i2 = mesh.Indices[i + 2];

        const auto& p0 = mesh.Positions[i0];
        const auto& p1 = mesh.Positions[i1];
        const auto& p2 = mesh.Positions[i2];

        // エッジ.
        auto e0 = p1 - p0;
        auto e1 = p2 - p0;

        // 面法線を算出.
        auto fn = asdx::Vector3::Cross(e0, e1);
        fn = asdx::Vector3::SafeNormalize(fn, fn);

        // 面法線を加算.
        normals[i0] += fn;
        normals[i1] += fn;
        normals[i2] += fn;
    }

    // 加算した法線を正規化し，頂点法線を求める.
    for(size_t i=0; i<vertexCount; ++i)
    {
        normals[i] = asdx::Vector3::SafeNormalize(normals[i], normals[i]);
    }

    const auto SMOOTHING_ANGLE = 59.7f;
    auto cosSmooth = cosf(asdx::ToDegree(SMOOTHING_ANGLE));

    // スムージング処理.
    for(size_t i=0; i<indexCount; i+=3)
    {
        auto i0 = mesh.Indices[i + 0];
        auto i1 = mesh.Indices[i + 1];
        auto i2 = mesh.Indices[i + 2];

        const auto& p0 = mesh.Positions[i0];
        const auto& p1 = mesh.Positions[i1];
        const auto& p2 = mesh.Positions[i2];

        // エッジ.
        auto e0 = p1 - p0;
        auto e1 = p2 - p0;

        // 面法線を算出.
        auto fn = asdx::Vector3::Cross(e0, e1);
        fn = asdx::Vector3::SafeNormalize(fn, fn);

        // 頂点法線と面法線のなす角度を算出.
        auto c0 = asdx::Vector3::Dot(normals[i0], fn);
        auto c1 = asdx::Vector3::Dot(normals[i1], fn);
        auto c2 = asdx::Vector3::Dot(normals[i2], fn);

        // スムージング処理.
        mesh.Normals[i0] = (c0 >= cosSmooth) ? normals[i0] : fn;
        mesh.Normals[i1] = (c1 >= cosSmooth) ? normals[i1] : fn;
        mesh.Normals[i2] = (c2 >= cosSmooth) ? normals[i2] : fn;
    }

    normals.clear();
}

} // namespace


///////////////////////////////////////////////////////////////////////////////
// LoaderOBJ class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      モデルを読み込みます.
//-----------------------------------------------------------------------------
bool LoaderOBJ::Load(const char* path, ModelOBJ& model)
{
    if (path == nullptr)
    {
        ELOG("Error : Inavlid Arguments.");
        return false;
    }

    m_DirectoryPath = asdx::GetDirectoryPathA(path);

    return LoadOBJ(path, model);
}

//-----------------------------------------------------------------------------
//      OBJファイルを読み込みます.
//-----------------------------------------------------------------------------
bool LoaderOBJ::LoadOBJ(const char* path, ModelOBJ& model)
{
    std::ifstream stream;
    stream.open(path, std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", path);
        return false;
    }

    const uint32_t BUFFER_LENGTH = 2048;
    char buf[BUFFER_LENGTH] = {};
    std::string group;

    uint32_t faceIndex = 0;
    uint32_t faceCount = 0;

    std::vector<asdx::Vector3>  positions;
    std::vector<asdx::Vector3>  normals;
    std::vector<asdx::Vector2>  texcoords;
    std::vector<IndexOBJ>       indices;
    std::vector<SubsetOBJ>      subsets;

    for(;;)
    {
        stream >> buf;
        if (!stream || stream.eof())
            break;

        if (0 == strcmp(buf, "#"))
        {
            /* DO_NOTHING */
        }
        else if (0 == strcmp(buf, "v"))
        {
            asdx::Vector3 v;
            stream >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (0 == strcmp(buf, "vt"))
        {
            asdx::Vector2 vt;
            stream >> vt.x >> vt.y;
            texcoords.push_back(vt);
        }
        else if (0 == strcmp(buf, "vn"))
        {
            asdx::Vector3 vn;
            stream >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (0 == strcmp(buf, "g"))
        {
            stream >> group;
        }
        else if (0 == strcmp(buf, "f"))
        {
            uint32_t ip, it, in;
            uint32_t p[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            uint32_t t[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            uint32_t n[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };

            uint32_t count = 0;
            uint32_t index = 0;

            faceIndex++;
            faceCount++;

            for(auto i=0; i<4; ++i)
            {
                count++;

                // 位置座標インデックス.
                stream >> ip;
                p[i] = ip - 1;

                if ('/' == stream.peek())
                {
                    stream.ignore();

                    // テクスチャ座標インデックス.
                    if ('/' != stream.peek())
                    {
                        stream >> it;
                        t[i] = it - 1;
                    }

                    // 法線インデックス.
                    if ('/' == stream.peek())
                    {
                        stream.ignore();

                        stream >> in;
                        n[i] = in - 1;
                    }
                }

                if (count <= 3)
                {
                    IndexOBJ f0 = { p[i], t[i], n[i] };
                    indices.push_back(f0);
                }

                if ('\n' == stream.peek() || '\r' == stream.peek())
                    break;
            }

            // 四角形.
            if (count > 3 && p[3] != UINT32_MAX)
            {
                assert(count == 4);

                faceIndex++;
                faceCount++;

                IndexOBJ f0 = { p[2], t[2], n[2] };
                IndexOBJ f1 = { p[3], t[3], n[3] };
                IndexOBJ f2 = { p[0], t[0], n[0] };

                indices.push_back(f0);
                indices.push_back(f1);
                indices.push_back(f2);
            }
        }
        else if (0 == strcmp(buf, "mtllib"))
        {
            char path[256] = {};
            stream >> path;
            if (strlen(path) > 0)
            {
                if (!LoadMTL(path, model))
                {
                    ELOGA("Error : Material Load Failed.");
                    return false;
                }
            }
        }
        else if (0 == strcmp(buf, "usemtl"))
        {
            SubsetOBJ subset = {};
            stream >> subset.Material;

            if (group.empty())
            { group = "group" + std::to_string(subsets.size()); }

            subset.MeshName   = group;
            subset.IndexStart = faceIndex * 3;

            auto index = subsets.size() - 1;
            subsets.push_back(subset);

            group.clear();

            if (subsets.size() > 1)
            {
                subsets[index].IndexCount = faceCount * 3;
                faceCount = 0;
            }
        }

        stream.ignore(BUFFER_LENGTH, '\n');
    }

    if (subsets.size() > 0)
    {
        auto index = subsets.size();
        subsets[index - 1].IndexCount = faceCount * 3;
    }

    stream.close();

    std::stable_sort(subsets.begin(), subsets.end(),
        [](const SubsetOBJ& lhs, const SubsetOBJ& rhs)
        {
            return std::tie(lhs.Material, lhs.IndexStart)
                 < std::tie(rhs.Material, rhs.IndexStart);
        });

    std::string matName;
    uint32_t    vertId = 0;
    uint32_t    meshId = 0;

    MeshOBJ dstMesh;

    for(size_t i=0; i<subsets.size(); ++i)
    {
        auto& subset = subsets[i];

        if (matName != subset.Material)
        {
            if (!matName.empty())
            {
                if (!normals.empty())
                { CalcNormals(dstMesh); }

                dstMesh.Positions.shrink_to_fit();
                dstMesh.Normals  .shrink_to_fit();
                dstMesh.TexCoords.shrink_to_fit();
                dstMesh.Indices  .shrink_to_fit();

                model.Meshes.emplace_back(dstMesh);

                dstMesh = MeshOBJ();
                vertId = 0;
            }

            dstMesh.Name     = "mesh";
            dstMesh.Name     += std::to_string(meshId);
            dstMesh.Material = subset.Material;

            meshId++;
            matName = subset.Material;
        }

        for(size_t j=0; j<subset.IndexCount; ++j)
        {
            auto id = subset.IndexStart + j;
            auto& index = indices[id];

            dstMesh.Positions[vertId] = positions[index.P];

            if (!normals.empty())
            { dstMesh.Normals[vertId] = normals[index.N]; }

            if (!texcoords.empty())
            { dstMesh.TexCoords[vertId] = texcoords[index.U]; }

            dstMesh.Indices.push_back(vertId);

            vertId++;
        }
    }

    if (!matName.empty())
    {
        if (normals.empty())
        { CalcNormals(dstMesh); }

        dstMesh.Positions.shrink_to_fit();
        dstMesh.Normals  .shrink_to_fit();
        dstMesh.TexCoords.shrink_to_fit();
        dstMesh.Indices  .shrink_to_fit();

        model.Meshes.emplace_back(dstMesh);
    }

    model.Meshes.shrink_to_fit();

    positions.clear();
    normals  .clear();
    texcoords.clear();
    indices  .clear();
    subsets  .clear();

    return true;
}

//-----------------------------------------------------------------------------
//      MTLファイルを読み込みます.
//-----------------------------------------------------------------------------
bool LoaderOBJ::LoadMTL(const char* path, ModelOBJ& model)
{
    std::ifstream stream;

    std::string filename = m_DirectoryPath + "/" + path;

    stream.open(filename.c_str(), std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", path);
        return false;
    }

    const uint32_t BUFFER_LENGTH= 2048;
    char buf[BUFFER_LENGTH] = {};
    int32_t index = -1;

    for(;;)
    {
        stream >> buf;

        if (!stream || stream.eof())
            break;

        if (0 == strcmp(buf, "newmtl"))
        {
            index++;
            MaterialOBJ mat;
            model.Materials.push_back(mat);
            stream >> model.Materials[index].Name;
        }
        else if (0 == strcmp(buf, "Ka"))
        {
            stream >> model.Materials[index].Ka.x 
                   >> model.Materials[index].Ka.y
                   >> model.Materials[index].Ka.z;
        }
        else if (0 == strcmp(buf, "Kd"))
        {
            stream >> model.Materials[index].Kd.x
                   >> model.Materials[index].Kd.y
                   >> model.Materials[index].Kd.z;
        }
        else if (0 == strcmp(buf, "Ks"))
        {
            stream >> model.Materials[index].Ks.x
                   >> model.Materials[index].Ks.y
                   >> model.Materials[index].Ks.z;
        }
        else if (0 == strcmp(buf, "Ke"))
        {
            stream >> model.Materials[index].Ke.x
                   >> model.Materials[index].Ke.y
                   >> model.Materials[index].Ke.z;
        }
        else if (0 == strcmp(buf, "d") || 0 == strcmp(buf, "Tr"))
        { stream >> model.Materials[index].Tr; }
        else if (0 == strcmp(buf, "Ns"))
        { stream >> model.Materials[index].Ns; }
        else if (0 == strcmp(buf, "map_Ka"))
        { stream >> model.Materials[index].map_Ka; }
        else if (0 == strcmp(buf, "map_Kd"))
        { stream >> model.Materials[index].map_Kd; }
        else if (0 == strcmp(buf, "map_Ks"))
        { stream >> model.Materials[index].map_Ks; }
        else if (0 == strcmp(buf, "map_Ke"))
        { stream >> model.Materials[index].map_Ke; }
        else if (0 == _stricmp(buf, "map_bump") || 0 == strcmp(buf, "bump"))
        { stream >> model.Materials[index].map_bump; }
        else if (0 == strcmp(buf, "disp"))
        { stream >> model.Materials[index].disp; }
        else if (0 == strcmp(buf, "norm"))
        { stream >> model.Materials[index].norm; }

        stream.ignore(BUFFER_LENGTH, '\n');
    }

    // ファイルを閉じる.
    stream.close();

    // メモリ最適化.
    model.Materials.shrink_to_fit();

    // 正常終了.
    return true;
}