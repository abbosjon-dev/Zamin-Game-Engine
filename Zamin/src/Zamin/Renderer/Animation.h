#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace Zamin {

    struct Joint
    {
        std::string Name;
        int Parent = -1;                       // index into Skeleton::Joints
        glm::mat4 InverseBind{ 1.0f };
        glm::vec3 RestTranslation{ 0.0f };
        glm::quat RestRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 RestScale{ 1.0f };
    };

    struct Skeleton
    {
        std::vector<Joint> Joints;              // parents always come before children
        glm::mat4 RootTransform{ 1.0f };        // transform of the non-joint ancestors
        bool Empty() const { return Joints.empty(); }
    };

    struct AnimationChannel
    {
        enum class Path { Translation, Rotation, Scale };
        int JointIndex = -1;
        Path Target = Path::Translation;
        bool Step = false;                      // STEP interpolation (else linear)
        std::vector<float> Times;
        std::vector<glm::vec4> Values;          // xyz or quaternion (x,y,z,w)
    };

    struct AnimationClip
    {
        std::string Name;
        float Duration = 0.0f;
        std::vector<AnimationChannel> Channels;
    };

    static constexpr int MaxJoints = 80; // fits the uniform budget of most GL 4.1 drivers

    // Computes skinning matrices (global * inverseBind) for `time` seconds into `clip`.
    void EvaluatePose(const Skeleton& skeleton, const AnimationClip* clip, float time, std::vector<glm::mat4>& outJointMatrices);

}
