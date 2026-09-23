/* Copyright (2021-2023) Bytedance Ltd. and/or its affiliates, All rights reserved. */
#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "mesh.h"
#include "shader.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "shader.h"

class Model {
public:
    Model() = delete;
    Model(const std::string& name, bool hasBoneInfo = false);
    ~Model();

    std::string& name();

    bool loadModel(const std::string& modelFileName);

    bool initialize() { return false; };

    bool bindMeshTexture(const std::string& meshName, const std::string& textureName);
    bool activeMeshTexture(const std::string& meshName, const std::string& textureName);

    void setColorTint(const glm::vec4& tint) { mColorTint = tint; }
    void setCullBackFaces(bool enabled) { mCullBackFaces = enabled; }
    void setDepthTest(bool enabled) { mDepthTest = enabled; }
    glm::vec3 boundsCenter() const { return (mBoundsMin + mBoundsMax) * 0.5f; }
    glm::vec3 boundsExtent() const { return mBoundsMax - mBoundsMin; }
    bool hasBounds() const { return mHasBounds; }

    bool render(const glm::mat4& p, const glm::mat4& v, const glm::mat4& m);

    int getBoneNodeIndexByName(const std::string& name) const;

    void setBoneNodeMatrices(const std::string& bone, const glm::mat4& m);
    void setBoneLocalRotation(const std::string& bone, const glm::mat4& rotation);
    glm::mat4 boneLocalRotationMatrix(const std::string& bone,
                                      const glm::mat4& rotation) const;

private:
    void initShader();
    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);
    std::vector<Texture> loadMaterialTextures_force(aiMaterial* mat, aiTextureType type, std::string typeName, std::string file);
    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    void processMeshBone(aiMesh* mesh, std::vector<Vertex>& vertices);
    void initializeBoneNode();
    void draw();

private:
    std::string mName;
    std::map<std::string, Mesh> mMeshes;
    bool mHasBoneInfo;
    
    struct boneInfo {
        int id;
        glm::mat4 offset;
        boneInfo(int count, const glm::mat4& boneOffset) : id(count), offset(boneOffset) {};
    };
    std::map<std::string, std::shared_ptr<boneInfo>> mBoneInfoMap;
    std::vector<glm::mat4> mBoneNodeMatrices;

    bool mIsGammaCorrection;

    std::vector<Texture> mTexturesLoaded;
    std::string mDirectory;

    std::map<std::string, std::vector<std::string>> mMeshTexturesMap;

    // RGB multiplies the selected diffuse texture. Alpha is reserved for
    // future material transparency and is currently not applied by Model.
    glm::vec4 mColorTint{1.0f, 1.0f, 1.0f, 0.0f};
    bool mCullBackFaces = true;
    bool mDepthTest = true;

    // Assimp keeps the sample FBX vertices in their authored coordinate
    // space. The left and right files use different, displaced origins, so
    // tracked models must normalize that displacement before being attached
    // to an OpenXR pose.
    glm::vec3 mBoundsMin{0.0f};
    glm::vec3 mBoundsMax{0.0f};
    bool mHasBounds = false;

    static Shader mShader;
};
