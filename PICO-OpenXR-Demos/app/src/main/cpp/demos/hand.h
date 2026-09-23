/* Copyright (2021-2023) Bytedance Ltd. and/or its affiliates, All rights reserved. */
#pragma once
#include <memory>
#include "model.h"
#include "utils.h"
class Hand;
class HandBase {
public:
    HandBase(std::string name);
    ~HandBase();
    bool initialize();
    void setModelFile(const std::string& modelFile);
    bool loadModelFile();
    void setModel(const glm::mat4& model);
    bool render(const glm::mat4& p, const glm::mat4& v);
private:
    friend class Hand;
    std::shared_ptr<Model> mHand;
    std::string mModelFile;
    glm::mat4 mProjection;
    glm::mat4 mView;
    glm::mat4 mModel{};
    // Match a roughly 18-20 cm human hand in the OpenXR metre coordinate
    // system. The previous 0.0075 value made the hands look child-sized.
    float mDefaultScale = 0.0105f;
};

class Hand final {
public:
    Hand();
    ~Hand();

    bool initialize();
    void setModel(int leftright, const glm::mat4& m);
    void render(const glm::mat4& p, const glm::mat4& v);
    void render(int leftright, const glm::mat4& p, const glm::mat4& v);
    void setBoneNodeMatrices(int leftright, const std::string& bone, const glm::mat4& m);
    void setFingerCurl(int leftright, float trigger, float squeeze);
    void setGripProfile(int leftright, const float fingerCurls[5], float diameter);
private:    
    glm::mat4 mModel[HAND_COUNT];
    std::shared_ptr<HandBase> mRightHand;
    std::shared_ptr<HandBase> mLeftHand;
};
