#include "Zamin/Renderer/Animation.h"

#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace Zamin {

    static glm::vec4 Sample(const AnimationChannel& c, float t)
    {
        if (c.Times.empty()) return glm::vec4(0.0f);
        if (t <= c.Times.front()) return c.Values.front();
        if (t >= c.Times.back()) return c.Values.back();
        size_t i = (size_t)(std::upper_bound(c.Times.begin(), c.Times.end(), t) - c.Times.begin());
        size_t a = i - 1, b = i;
        if (c.Step) return c.Values[a];
        float span = c.Times[b] - c.Times[a];
        float f = span > 0.0f ? (t - c.Times[a]) / span : 0.0f;
        if (c.Target == AnimationChannel::Path::Rotation)
        {
            glm::quat qa(c.Values[a].w, c.Values[a].x, c.Values[a].y, c.Values[a].z);
            glm::quat qb(c.Values[b].w, c.Values[b].x, c.Values[b].y, c.Values[b].z);
            glm::quat q = glm::slerp(qa, qb, f);
            return { q.x, q.y, q.z, q.w };
        }
        return glm::mix(c.Values[a], c.Values[b], f);
    }

    struct LocalPose
    {
        std::vector<glm::vec3> T, S;
        std::vector<glm::quat> R;
    };

    // ---- root motion: where the joint is / which way it faces in the model space
    static float WrapAngle(float a)
    {
        constexpr float pi = glm::pi<float>();
        a = std::fmod(a + pi, 2.0f * pi);
        if (a < 0.0f) a += 2.0f * pi;
        return a - pi;
    }

    // The joint's parent's global transform in the rest pose (the root's: the skeleton's).
    static glm::mat4 RestParentGlobal(const Skeleton& skeleton, int joint)
    {
        std::vector<int> chain;
        for (int p = skeleton.Joints[joint].Parent; p >= 0 && (int)chain.size() < 256; p = skeleton.Joints[p].Parent)
            chain.push_back(p);
        glm::mat4 m = skeleton.RootTransform;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const Joint& j = skeleton.Joints[*it];
            m = m * glm::translate(glm::mat4(1.0f), j.RestTranslation) * glm::toMat4(j.RestRotation) * glm::scale(glm::mat4(1.0f), j.RestScale);
        }
        return m;
    }

    static glm::quat RotationOf(const glm::mat4& m)
    {
        glm::mat3 r(m);
        for (int c = 0; c < 3; c++)
        {
            float len = glm::length(r[c]);
            if (len > 1e-8f) r[c] /= len;
        }
        return glm::normalize(glm::quat_cast(r));
    }

    // The turn about +Y in a rotation (swing-twist).
    static float YawOf(const glm::quat& q)
    {
        if (std::abs(q.y) < 1e-8f && std::abs(q.w) < 1e-8f)
            return 0.0f;
        return WrapAngle(2.0f * std::atan2(q.y, q.w));
    }

    struct RootSpace
    {
        glm::mat4 Parent{ 1.0f };
        glm::mat4 ParentInverse{ 1.0f };
        glm::quat ParentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    static RootSpace GetRootSpace(const Skeleton& skeleton, int joint)
    {
        RootSpace space;
        space.Parent = RestParentGlobal(skeleton, joint);
        space.ParentInverse = glm::inverse(space.Parent);
        space.ParentRotation = RotationOf(space.Parent);
        return space;
    }

    // The joint's local translation / rotation at `time` (rest values where the clip has no channel).
    static void SampleJoint(const Skeleton& skeleton, const AnimationClip& clip, int joint, float time, glm::vec3& t, glm::quat& r)
    {
        t = skeleton.Joints[joint].RestTranslation;
        r = skeleton.Joints[joint].RestRotation;
        for (const auto& ch : clip.Channels)
        {
            if (ch.JointIndex != joint) continue;
            glm::vec4 v = Sample(ch, time);
            if (ch.Target == AnimationChannel::Path::Translation) t = glm::vec3(v);
            else if (ch.Target == AnimationChannel::Path::Rotation) r = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
        }
    }

    struct RootSample
    {
        glm::vec3 Position{ 0.0f }; // model space
        float Yaw = 0.0f;
    };

    static RootSample SampleRoot(const Skeleton& skeleton, const AnimationClip& clip, int joint, const RootSpace& space, float time)
    {
        glm::vec3 t;
        glm::quat r;
        SampleJoint(skeleton, clip, joint, time, t, r);
        return { glm::vec3(space.Parent * glm::vec4(t, 1.0f)), YawOf(space.ParentRotation * r) };
    }

    // Takes the root's XZ walk and Y turn out of a sampled pose (relative to the clip's start).
    static void StripRootMotion(const Skeleton& skeleton, const AnimationClip* clip, int joint, glm::vec3& t, glm::quat& r)
    {
        if (!clip || joint < 0 || joint >= (int)skeleton.Joints.size())
            return;
        const RootSpace space = GetRootSpace(skeleton, joint);
        const RootSample start = SampleRoot(skeleton, *clip, joint, space, 0.0f);
        glm::vec3 p = glm::vec3(space.Parent * glm::vec4(t, 1.0f));
        p.x = start.Position.x;
        p.z = start.Position.z;
        t = glm::vec3(space.ParentInverse * glm::vec4(p, 1.0f));
        glm::quat model = space.ParentRotation * r;
        const float turn = WrapAngle(YawOf(model) - start.Yaw);
        model = glm::angleAxis(-turn, glm::vec3(0.0f, 1.0f, 0.0f)) * model;
        r = glm::normalize(glm::inverse(space.ParentRotation) * model);
    }

    static void SamplePose(const Skeleton& skeleton, const AnimationClip* clip, float time, LocalPose& pose)
    {
        size_t n = skeleton.Joints.size();
        pose.T.resize(n); pose.S.resize(n); pose.R.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            pose.T[i] = skeleton.Joints[i].RestTranslation;
            pose.R[i] = skeleton.Joints[i].RestRotation;
            pose.S[i] = skeleton.Joints[i].RestScale;
        }
        if (!clip) return;
        for (const auto& ch : clip->Channels)
        {
            if (ch.JointIndex < 0 || ch.JointIndex >= (int)n) continue;
            glm::vec4 v = Sample(ch, time);
            switch (ch.Target)
            {
                case AnimationChannel::Path::Translation: pose.T[ch.JointIndex] = glm::vec3(v); break;
                case AnimationChannel::Path::Scale:       pose.S[ch.JointIndex] = glm::vec3(v); break;
                case AnimationChannel::Path::Rotation:    pose.R[ch.JointIndex] = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); break;
            }
        }
    }

    static void ComposePose(const Skeleton& skeleton, const LocalPose& pose, std::vector<glm::mat4>& out)
    {
        size_t n = skeleton.Joints.size();
        std::vector<glm::mat4> global(n);
        out.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.T[i]) * glm::toMat4(pose.R[i]) * glm::scale(glm::mat4(1.0f), pose.S[i]);
            int p = skeleton.Joints[i].Parent;
            global[i] = (p >= 0 ? global[p] : skeleton.RootTransform) * local;
            out[i] = global[i] * skeleton.Joints[i].InverseBind;
        }
    }

    void EvaluatePose(const Skeleton& skeleton, const AnimationClip* clip, float time, std::vector<glm::mat4>& out, int rootMotionJoint)
    {
        LocalPose pose;
        SamplePose(skeleton, clip, time, pose);
        if (rootMotionJoint >= 0 && rootMotionJoint < (int)pose.T.size())
            StripRootMotion(skeleton, clip, rootMotionJoint, pose.T[rootMotionJoint], pose.R[rootMotionJoint]);
        ComposePose(skeleton, pose, out);
    }

    int FindRootMotionJoint(const Skeleton& skeleton, const AnimationClip* clip, const std::string& name)
    {
        if (!name.empty())
        {
            for (size_t i = 0; i < skeleton.Joints.size(); i++)
                if (skeleton.Joints[i].Name == name)
                    return (int)i;
            return -1;
        }
        if (!clip)
            return -1;
        int best = -1;
        for (const auto& ch : clip->Channels)
            if (ch.Target == AnimationChannel::Path::Translation && ch.JointIndex >= 0 && ch.JointIndex < (int)skeleton.Joints.size() &&
                (best < 0 || ch.JointIndex < best))
                best = ch.JointIndex;
        return best;
    }

    // Root motion between two times inside one cycle.
    static RootMotion RootMotionSegment(const Skeleton& skeleton, const AnimationClip& clip, int joint, const RootSpace& space,
                                        float startYaw, float a, float b)
    {
        const RootSample sa = SampleRoot(skeleton, clip, joint, space, a);
        const RootSample sb = SampleRoot(skeleton, clip, joint, space, b);
        glm::vec3 d = sb.Position - sa.Position;
        d.y = 0.0f;
        const float facing = WrapAngle(sa.Yaw - startYaw); // how far it had turned since the clip's start
        RootMotion m;
        m.Translation = glm::angleAxis(-facing, glm::vec3(0.0f, 1.0f, 0.0f)) * d;
        m.Translation.y = 0.0f;
        m.Yaw = WrapAngle(sb.Yaw - sa.Yaw);
        return m;
    }

    RootMotion ExtractRootMotion(const Skeleton& skeleton, const AnimationClip& clip, int joint, float from, float to, bool loop)
    {
        RootMotion total;
        const float d = clip.Duration;
        if (joint < 0 || joint >= (int)skeleton.Joints.size() || d <= 0.0f || from == to || !std::isfinite(from) || !std::isfinite(to))
            return total;
        const RootSpace space = GetRootSpace(skeleton, joint);
        const float startYaw = SampleRoot(skeleton, clip, joint, space, 0.0f).Yaw;
        auto add = [&](const RootMotion& m) {
            total.Translation += glm::angleAxis(total.Yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * m.Translation;
            total.Yaw += m.Yaw;
        };
        if (!loop)
        {
            add(RootMotionSegment(skeleton, clip, joint, space, startYaw, std::clamp(from, 0.0f, d), std::clamp(to, 0.0f, d)));
            total.Translation.y = 0.0f;
            return total;
        }
        constexpr double maxCycles = 8.0;
        double k0 = std::floor((double)from / d), k1 = std::floor((double)to / d);
        if (to > from)
        {
            if (k1 - k0 > maxCycles) { k0 = k1 - maxCycles; from = (float)(k0 * d); }
            for (double k = k0; k <= k1; k += 1.0)
            {
                const float a = (float)(std::max((double)from, k * d) - k * d);
                const float b = (float)(std::min((double)to, (k + 1.0) * d) - k * d);
                if (b > a) add(RootMotionSegment(skeleton, clip, joint, space, startYaw, a, b));
            }
        }
        else
        {
            if (k0 - k1 > maxCycles) { k1 = k0 - maxCycles; to = (float)(k1 * d); }
            for (double k = k0; k >= k1; k -= 1.0)
            {
                const float a = (float)(std::min((double)from, (k + 1.0) * d) - k * d);
                const float b = (float)(std::max((double)to, k * d) - k * d);
                if (a > b) add(RootMotionSegment(skeleton, clip, joint, space, startYaw, a, b));
            }
        }
        total.Translation.y = 0.0f;
        total.Yaw = WrapAngle(total.Yaw);
        return total;
    }

    void GetJointTransforms(const Skeleton& skeleton, const std::vector<glm::mat4>& jointMatrices, std::vector<glm::mat4>& outGlobal)
    {
        const size_t n = skeleton.Joints.size();
        if (jointMatrices.size() != n)
        {
            // the rest pose, composed like ComposePose does
            outGlobal.resize(n);
            for (size_t i = 0; i < n; i++)
            {
                const Joint& j = skeleton.Joints[i];
                glm::mat4 local = glm::translate(glm::mat4(1.0f), j.RestTranslation) * glm::toMat4(j.RestRotation) *
                                  glm::scale(glm::mat4(1.0f), j.RestScale);
                outGlobal[i] = (j.Parent >= 0 && (size_t)j.Parent < i ? outGlobal[j.Parent] : skeleton.RootTransform) * local;
            }
            return;
        }
        outGlobal.resize(n);
        for (size_t i = 0; i < n; i++)
            outGlobal[i] = jointMatrices[i] * glm::inverse(skeleton.Joints[i].InverseBind);
    }

    void EvaluatePoseBlended(const Skeleton& skeleton, const AnimationClip* clipA, float timeA,
                             const AnimationClip* clipB, float timeB, float weight, std::vector<glm::mat4>& out, int rootMotionJoint)
    {
        weight = std::clamp(weight, 0.0f, 1.0f);
        const bool strip = rootMotionJoint >= 0 && rootMotionJoint < (int)skeleton.Joints.size();
        LocalPose a, b;
        SamplePose(skeleton, clipA, timeA, a);
        if (strip)
            StripRootMotion(skeleton, clipA, rootMotionJoint, a.T[rootMotionJoint], a.R[rootMotionJoint]);
        if (weight <= 0.0f) { ComposePose(skeleton, a, out); return; }
        SamplePose(skeleton, clipB, timeB, b);
        if (strip)
            StripRootMotion(skeleton, clipB, rootMotionJoint, b.T[rootMotionJoint], b.R[rootMotionJoint]);
        for (size_t i = 0; i < a.T.size(); i++)
        {
            a.T[i] = glm::mix(a.T[i], b.T[i], weight);
            a.S[i] = glm::mix(a.S[i], b.S[i], weight);
            glm::quat qb = glm::dot(a.R[i], b.R[i]) < 0.0f ? -b.R[i] : b.R[i];
            a.R[i] = glm::normalize(glm::slerp(a.R[i], qb, weight));
        }
        ComposePose(skeleton, a, out);
    }

}
