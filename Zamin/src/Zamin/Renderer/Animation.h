#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
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

    // Morph target (blend shape) weights a clip animates: the weights of every target of one morph
    // group (a glTF mesh node with blend shapes, Model::GetMorphGroups) at each key.
    struct MorphWeightChannel
    {
        int Group = -1;                         // index into Model::GetMorphGroups()
        uint32_t Count = 0;                     // weights per key (the group's targets)
        bool Step = false;                      // STEP interpolation (else linear)
        std::vector<float> Times;
        std::vector<float> Values;              // Times.size() x Count, key after key
    };

    struct AnimationClip
    {
        std::string Name;
        float Duration = 0.0f;
        std::vector<AnimationChannel> Channels;
        std::vector<MorphWeightChannel> MorphChannels;
    };

    // The weights of a morph channel at `time` seconds: channel.Count values into `out`.
    void SampleMorphChannel(const MorphWeightChannel& channel, float time, float* out);

    static constexpr int MaxJoints = 80; // fits the uniform budget of most GL 4.1 drivers

    // The part of `clip` from `start` to `end` seconds as a clip of its own (its time 0 = `start`):
    // every channel gets its value at both ends as keys plus the keys in between. start / end are
    // clamped to the clip (end < 0: its end; end before start: a one-pose clip of length 0).
    AnimationClip TrimClip(const AnimationClip& clip, float start, float end, const std::string& name);

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

    // ---- poses as joint-local transforms: blend trees and layers mix these, then compose them
    struct LocalPose
    {
        std::vector<glm::vec3> T, S;
        std::vector<glm::quat> R;
        size_t Size() const { return T.size(); }
    };

    // The pose of `clip` at `time` seconds as joint-local transforms (the rest values where the
    // clip has no channel; no clip: the rest pose). rootMotionJoint >= 0: that joint's walk and
    // turn are taken out, as EvaluatePose does.
    void SampleLocalPose(const Skeleton& skeleton, const AnimationClip* clip, float time, LocalPose& out, int rootMotionJoint = -1);
    // `pose` moved toward `other` by `weight` (0 = pose, 1 = other): positions and scales mixed,
    // rotations along the shortest arc. mask (one value per joint, 0..1): each joint's weight is
    // weight x its mask value (avatar masks).
    void BlendLocalPose(LocalPose& pose, const LocalPose& other, float weight, const std::vector<float>* mask = nullptr);
    // Additive: what `other` changes relative to `reference` (the same clip at its start) is added
    // onto `pose`, scaled by weight (and the mask): moves add, turns are applied on top, scales multiply.
    void AddLocalPose(LocalPose& pose, const LocalPose& other, const LocalPose& reference, float weight,
                      const std::vector<float>* mask = nullptr);
    // Skinning matrices (global x inverse bind) of a local pose.
    void ComposeJointMatrices(const Skeleton& skeleton, const LocalPose& pose, std::vector<glm::mat4>& outJointMatrices);

    // ---- pose cache: a clip sampled ahead of time at a fixed rate (Animator::CachePoses). Reading
    // a pose is then two cached frames mixed per joint instead of searching every channel's keys.
    // Joints with step keys are read from the clip itself (their jumps stay where they are).
    struct BakedClip
    {
        float Duration = 0.0f;
        float Rate = 0.0f;                      // frames per second asked for
        float Step = 0.0f;                      // seconds between frames (Duration / (Frames - 1); <= 1 / Rate)
        uint32_t Frames = 0;
        size_t Joints = 0;
        std::vector<glm::vec3> T, S;            // Frames x Joints, frame after frame
        std::vector<glm::quat> R;
        std::vector<uint8_t> FromClip;          // per joint: 1 = read from the clip (step keys)

        BakedClip() = default;
        BakedClip(const BakedClip&) = delete;
        BakedClip& operator=(const BakedClip&) = delete;
        ~BakedClip();
        size_t GetMemory() const { return m_Bytes; }

    private:
        size_t m_Bytes = 0;
        friend void BakeClip(const Skeleton&, const AnimationClip&, float, BakedClip&);
    };
    // Samples `clip` for `skeleton` at `rate` frames per second (at least 1; both ends exact).
    void BakeClip(const Skeleton& skeleton, const AnimationClip& clip, float rate, BakedClip& out);
    // The pose at `time` seconds from the cached frames (clamped to the clip; as SampleLocalPose
    // otherwise, root motion included). `baked` made for another skeleton: sampled from the clip.
    void SampleBakedPose(const Skeleton& skeleton, const AnimationClip& clip, const BakedClip& baked, float time, LocalPose& out,
                         int rootMotionJoint = -1);
    // The memory of every cached clip there is (bytes).
    size_t GetBakedClipMemory();

    // ---- 1D blend trees
    // The weights of motions placed at `thresholds` (in any order) for the parameter `value`: the
    // two nearest on either side share it linearly; at or below the lowest / at or above the
    // highest that one alone has it. They add up to 1 (an empty list: none). Equal thresholds: the
    // first of them.
    void ComputeBlend1DWeights(const std::vector<float>& thresholds, float value, std::vector<float>& outWeights);

    // ---- avatar masks
    // 1 for each named joint and every joint below it, 0 for the others (one value per joint).
    // No names: the whole skeleton (all 1). Names the skeleton does not have are skipped (only
    // unknown names: all 0). Returns how many joints are in the mask.
    size_t BuildJointMask(const Skeleton& skeleton, const std::vector<std::string>& roots, std::vector<float>& outMask);

    // Where the joints of a pose are: each joint's global transform in the model's space (the
    // space the skinned mesh is drawn in, before the entity's transform), from the skinning
    // matrices EvaluatePose made (joint matrix x inverse bind). `jointMatrices` that do not fit
    // the skeleton (empty, another count) give the rest pose.
    void GetJointTransforms(const Skeleton& skeleton, const std::vector<glm::mat4>& jointMatrices, std::vector<glm::mat4>& outGlobal);
    // The same for one joint (its rest pose when `jointMatrices` do not fit the skeleton).
    glm::mat4 GetJointTransform(const Skeleton& skeleton, const std::vector<glm::mat4>& jointMatrices, int joint);
    // The joint called `name` (-1: none).
    int FindJoint(const Skeleton& skeleton, const std::string& name);

}
