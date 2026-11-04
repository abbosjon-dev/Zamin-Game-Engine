#include "Zamin/Renderer/Animation.h"

#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

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

    void EvaluatePose(const Skeleton& skeleton, const AnimationClip* clip, float time, std::vector<glm::mat4>& out)
    {
        LocalPose pose;
        SamplePose(skeleton, clip, time, pose);
        ComposePose(skeleton, pose, out);
    }

    void EvaluatePoseBlended(const Skeleton& skeleton, const AnimationClip* clipA, float timeA,
                             const AnimationClip* clipB, float timeB, float weight, std::vector<glm::mat4>& out)
    {
        weight = std::clamp(weight, 0.0f, 1.0f);
        LocalPose a, b;
        SamplePose(skeleton, clipA, timeA, a);
        if (weight <= 0.0f) { ComposePose(skeleton, a, out); return; }
        SamplePose(skeleton, clipB, timeB, b);
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
