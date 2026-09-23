#include "hand.h"

HandBase::HandBase(std::string name) {
    mHand = std::make_shared<Model>(name, true/*hasBoneInfo*/);
}
HandBase::~HandBase() { 
}
bool HandBase::initialize() {
    mHand->initialize();
    return true;
}
void HandBase::setModelFile(const std::string& modelFile) {
    mModelFile = modelFile;
}
bool HandBase::loadModelFile() {
    return mHand->loadModel(mModelFile);
}
void HandBase::setModel(const glm::mat4& model) {
    mModel = model;
}
bool HandBase::render(const glm::mat4& p, const glm::mat4& v) {
    glm::mat4 model = glm::scale(mModel, glm::vec3(mDefaultScale));
    if (mHand->hasBounds()) {
        // Hand_L.fbx and Hand_R.fbx have different baked origins. Centering
        // their actual geometry makes the OpenXR aim pose land in the palm for
        // both hands instead of throwing one mesh outward and clipping the
        // other at the inner edge of the eye.
        model = glm::translate(model, -mHand->boundsCenter());
    }
    mHand->render(p, v, model);
    return true;
}
////////////////////////////////////////////////////////////////////////////////
Hand::Hand() {
    mRightHand = std::make_shared<HandBase>("right_hand");
    mLeftHand = std::make_shared<HandBase>("left_hand");
}

Hand::~Hand() {
}

bool Hand::initialize() {
    mLeftHand->initialize();
    mRightHand->initialize();

    mLeftHand->setModelFile("hand/Hand_L.fbx");
    mRightHand->setModelFile("hand/Hand_R.fbx");

    // 0.png from the SDK sample is an all-white placeholder. It made the
    // tracked meshes look like unlit paper silhouettes in the dungeon.
    mLeftHand->mHand->bindMeshTexture("l_handMesh", "hand/leather_glove_512.png");
    mRightHand->mHand->bindMeshTexture("r_handMesh", "hand/leather_glove_512.png");
    // The two mirrored hand exports do not share a reliable winding order.
    // Double-sided rendering keeps both the palm and back visible in each eye.
    mLeftHand->mHand->setCullBackFaces(false);
    mRightHand->mHand->setCullBackFaces(false);
    // Immersive presentation copies the matching Arx eye depth into the
    // OpenXR target.  Depth-testing the tracked mesh is what makes a held
    // shaft pass behind the front fingers but remain in front of the palm.
    mLeftHand->mHand->setDepthTest(true);
    mRightHand->mHand->setDepthTest(true);
    // Force a readable leather albedo even on runtimes that expose the FBX's
    // pale embedded material instead of the selected diffuse texture.
    // The bundled model's albedo is almost white on PICO, so a strong tint is
    // required for these to read as leather gloves rather than plastic hands.
    const glm::vec4 leatherTint(0.34f, 0.24f, 0.16f, 1.0f);
    mLeftHand->mHand->setColorTint(leatherTint);
    mRightHand->mHand->setColorTint(leatherTint);

    const bool leftLoaded = mLeftHand->loadModelFile();
    const bool rightLoaded = mRightHand->loadModelFile();

    const glm::vec3 leftCenter = mLeftHand->mHand->boundsCenter();
    const glm::vec3 leftExtent = mLeftHand->mHand->boundsExtent();
    const glm::vec3 rightCenter = mRightHand->mHand->boundsCenter();
    const glm::vec3 rightExtent = mRightHand->mHand->boundsExtent();
    infof("ArxVR hand bounds left loaded=%d center=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f)",
          leftLoaded, leftCenter.x, leftCenter.y, leftCenter.z,
          leftExtent.x, leftExtent.y, leftExtent.z);
    infof("ArxVR hand bounds right loaded=%d center=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f)",
          rightLoaded, rightCenter.x, rightCenter.y, rightCenter.z,
          rightExtent.x, rightExtent.y, rightExtent.z);

    mLeftHand->mHand->activeMeshTexture("l_handMesh", "hand/leather_glove_512.png");
    mRightHand->mHand->activeMeshTexture("r_handMesh", "hand/leather_glove_512.png");

    return true;
}

void Hand::setModel(int leftright, const glm::mat4& m) {
    // The mirrored FBX exports have their fingers along opposite local X axes,
    // while OpenXR aim points along -Z. Rotate each side around its own centred
    // palm so both hands follow their corresponding controller ray.
    const float correctionDegrees = leftright == HAND_LEFT ? 90.0f : -90.0f;
    glm::mat4 visualModel = glm::rotate(m, glm::radians(correctionDegrees),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
    if (leftright == HAND_RIGHT) {
        // The right export is upside-down around the finger/aim axis after the
        // yaw correction. Flip only that palm without reversing its pointing
        // direction.
        visualModel = glm::rotate(visualModel, glm::radians(180.0f),
                                  glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (leftright == HAND_LEFT) {
        mModel[HAND_LEFT] = visualModel;
        mLeftHand->setModel(visualModel);
    } else {
        mModel[HAND_RIGHT] = visualModel;
        mRightHand->setModel(visualModel);
    }
}

void Hand::render(const glm::mat4& p, const glm::mat4& v) {
    mLeftHand->render(p, v);
    mRightHand->render(p, v);
}

void Hand::render(int leftright, const glm::mat4& p, const glm::mat4& v) {
    leftright == HAND_RIGHT ? mRightHand->render(p, v) : mLeftHand->render(p, v);
}

void Hand::setBoneNodeMatrices(int leftright, const std::string& bone, const glm::mat4& m) {
    leftright == HAND_RIGHT ? mRightHand->mHand->setBoneNodeMatrices(bone, m) : mLeftHand->mHand->setBoneNodeMatrices(bone, m);
}

void Hand::setFingerCurl(int leftright, float trigger, float squeeze) {
    trigger = glm::clamp(trigger, 0.0f, 1.0f);
    squeeze = glm::clamp(squeeze, 0.0f, 1.0f);
    Model* model = leftright == HAND_RIGHT
                 ? mRightHand->mHand.get() : mLeftHand->mHand.get();
    const char side = leftright == HAND_RIGHT ? 'r' : 'l';

    auto setFinger = [&](const char* finger, int firstSegment, int lastSegment,
                         float curl, const std::vector<float>& maximumDegrees,
                         float direction = 1.0f) {
        glm::mat4 inherited(1.0f);
        for (int segment = firstSegment; segment <= lastSegment; ++segment) {
            const std::string bone = std::string("p_") + side + "_" + finger
                                   + std::to_string(segment);
            const float degrees = maximumDegrees[segment - firstSegment];
            const glm::mat4 bend = glm::rotate(glm::mat4(1.0f),
                                               glm::radians(direction * degrees * curl),
                                               glm::vec3(0.0f, 0.0f, 1.0f));
            // A distal phalanx must inherit every proximal joint. The shader
            // stores one final skinning matrix per bone, so accumulate the
            // bind-space joint transforms in parent-to-child order here.
            inherited *= model->boneLocalRotationMatrix(bone, bend);
            model->setBoneNodeMatrices(bone, inherited);
        }
    };

    // The index follows the index trigger. Squeeze also closes it so a full
    // grip forms a fist, while the other fingers follow the lower grip.
    const float indexCurl = std::max(trigger, squeeze);
    setFinger("index", 1, 3, indexCurl, {42.0f, 72.0f, 88.0f});
    setFinger("middle", 1, 3, squeeze, {42.0f, 72.0f, 88.0f});
    setFinger("ring", 1, 3, squeeze, {42.0f, 72.0f, 88.0f});
    setFinger("pinky", 0, 3, squeeze, {16.0f, 48.0f, 76.0f, 92.0f});
    // All authored flexion joints close toward the palm on positive local Z.
    // The previous negative default made the four fingers hyper-extend away
    // from the palm when either physical controller trigger was squeezed.
    setFinger("thumb", 0, 3, squeeze, {18.0f, 30.0f, 42.0f, 42.0f}, 1.0f);
}

void Hand::setGripProfile(int leftright, const float fingerCurls[5], float diameter) {
    Model* model = leftright == HAND_RIGHT
                 ? mRightHand->mHand.get() : mLeftHand->mHand.get();
    const char side = leftright == HAND_RIGHT ? 'r' : 'l';
    diameter = glm::clamp(diameter, 4.0f, 32.0f);
    const float largeObject = glm::clamp((diameter - 8.0f) / 24.0f, 0.0f, 1.0f);

    auto setFinger = [&](const char* finger, int firstSegment, int lastSegment,
                         float curl, const std::vector<float>& thinDegrees,
                         const std::vector<float>& largeDegrees) {
        curl = glm::clamp(curl, 0.0f, 1.0f);
        glm::mat4 inherited(1.0f);
        for (int segment = firstSegment; segment <= lastSegment; ++segment) {
            const std::string bone = std::string("p_") + side + "_" + finger
                                   + std::to_string(segment);
            const int index = segment - firstSegment;
            const float maximum = glm::mix(thinDegrees[index], largeDegrees[index],
                                           largeObject);
            const glm::mat4 bend = glm::rotate(glm::mat4(1.0f),
                                               glm::radians(maximum * curl),
                                               glm::vec3(0.0f, 0.0f, 1.0f));
            inherited *= model->boneLocalRotationMatrix(bone, bend);
            model->setBoneNodeMatrices(bone, inherited);
        }
    };

    // Narrow shafts need hooked distal phalanges; large rounded objects need a
    // cupped hand with more proximal closure and less fingertip folding. Keep
    // all five values independent so asymmetric/local mesh profiles remain
    // visible instead of collapsing back to a single fist animation.
    setFinger("index", 1, 3, fingerCurls[1],
              {48.0f, 88.0f, 82.0f}, {64.0f, 62.0f, 42.0f});
    setFinger("middle", 1, 3, fingerCurls[2],
              {50.0f, 90.0f, 84.0f}, {66.0f, 64.0f, 44.0f});
    setFinger("ring", 1, 3, fingerCurls[3],
              {52.0f, 91.0f, 86.0f}, {67.0f, 65.0f, 46.0f});
    setFinger("pinky", 0, 3, fingerCurls[4],
              {16.0f, 54.0f, 92.0f, 88.0f}, {12.0f, 68.0f, 66.0f, 48.0f});
    setFinger("thumb", 0, 3, fingerCurls[0],
              {20.0f, 36.0f, 52.0f, 48.0f}, {28.0f, 42.0f, 40.0f, 30.0f});
}
