//-----------------------------------------------------------------------------
// File : ModelOBJ.h
// desc : Waveront Alias OBJ format.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <vector>
#include <string>
#include <fnd/asdxMath.h>


///////////////////////////////////////////////////////////////////////////////
// MaterialOBJ structure
///////////////////////////////////////////////////////////////////////////////
struct MaterialOBJ
{
    std::string     Name;
    asdx::Vector3   Ka;
    asdx::Vector3   Kd;
    asdx::Vector3   Ks;
    asdx::Vector3   Ke;
    float           Tr;
    float           Ns;
    std::string     map_Ka;
    std::string     map_Kd;
    std::string     map_Ks;
    std::string     map_Ke;
    std::string     map_bump;
    std::string     norm;
    std::string     disp;
};

///////////////////////////////////////////////////////////////////////////////
// MeshOBJ structure
///////////////////////////////////////////////////////////////////////////////
struct MeshOBJ
{
    std::string                 Name;
    std::string                 Material;
    std::vector<asdx::Vector3>  Positions;
    std::vector<asdx::Vector3>  Normals;
    std::vector<asdx::Vector2>  TexCoords;
    std::vector<uint32_t>       Indices;
};

///////////////////////////////////////////////////////////////////////////////
// ModelOBJ structure
///////////////////////////////////////////////////////////////////////////////
struct ModelOBJ
{
    std::vector<MaterialOBJ>    Materials;
    std::vector<MeshOBJ>        Meshes;
};

///////////////////////////////////////////////////////////////////////////////
// LoaderOBJ class
///////////////////////////////////////////////////////////////////////////////
class LoaderOBJ
{
public:
    LoaderOBJ () = default;
    ~LoaderOBJ() = default;
    bool Load(const char* path, ModelOBJ& model);
    const std::string& GetDirectory() const;

private:
    std::string m_DirectoryPath;
    bool LoadOBJ(const char* path, ModelOBJ& model);
    bool LoadMTL(const char* path, ModelOBJ& model);
};