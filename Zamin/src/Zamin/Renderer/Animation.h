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
    // rootMotionJoint >= 0: that joint's walk (XZ) and turn (yaw about Y) are taken out of the pose -
    // it stays where it is at the clip's start (its height still moves); see ExtractRootMotion.
    void EvaluatePose(const Skeleton& skeleton, const AnimationClip* clip, float time, std::vector<glm::mat4>& outJointMatrices,
                      int rootMotionJoint = -1);

    // Blends two clips: weight 0 = clipA only, 1 = clipB only (crossfades).
    void EvaluatePoseBlended(const Skeleton& skeleton, const AnimationClip* clipA, float timeA,
                             const AnimationClip* clipB, float timeB, float weight, std::vector<glm::mat4>& outJointMatrices,
                             int rootMotionJoint = -1);

    // ---- root motion
    // The joint whose motion moves the character: the one named `name`, else ("") the first joint
    // the clip moves (translation channel) - parents come first, so the top-most one (hips / root).
    // -1 when there is none.
    int FindRootMotionJoint(const Skeleton& skeleton, const AnimationClip* clip, const std::string& name = {});

    // How far the root went: Translation in the character's own space (the model space turned so
    // the character faces where it faced at the clip's start; Y is always 0) and the turn about Y
    // (radians, counter-clockwise seen from above).
    struct RootMotion
    {
        glm::vec3 Translation{ 0.0f };
        float Yaw = 0.0f;
    };
    // Root motion of `clip` from clip time `from` to `to` (to < from: played backwards). loop: the
    // times go on past the clip's end (12.5 s of a 1 s clip = 12 cycles + 0.5), each cycle's motion
    // added on (at most 8 cycles: a hitch); not looping: clamped to the clip.
    RootMotion ExtractRootMotion(const Skeleton& skeleton, const AnimationClip& clip, int joint, float from, float to, bool loop);

    // Where the joints of a pose are: each joint's global transform in the model's space (the
    // space the skinned mesh is drawn in, before the entity's transform), from the skinning
    // matrices EvaluatePose made (joint matrix x inverse bind). `jointMatrices` that do not fit
    // the skeleton (empty, another count) give the rest pose.
    void GetJointTransforms(const Skeleton& skeleton, const std::vector<glm::mat4>& jointMatrices, std::vector<glm::mat4>& outGlobal);

}
