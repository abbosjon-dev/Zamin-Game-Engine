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

    void EvaluatePose(const Skeleton& skeleton, const AnimationClip* clip, float time, std::vector<glm::mat4>& out)
    {
        size_t n = skeleton.Joints.size();
        std::vector<glm::vec3> T(n), S(n);
        std::vector<glm::quat> R(n);
        for (size_t i = 0; i < n; i++)
        {
            T[i] = skeleton.Joints[i].RestTranslation;
            R[i] = skeleton.Joints[i].RestRotation;
            S[i] = skeleton.Joints[i].RestScale;
        }
        if (clip)
        {
            for (const auto& ch : clip->Channels)
            {
                if (ch.JointIndex < 0 || ch.JointIndex >= (int)n) continue;
                glm::vec4 v = Sample(ch, time);
                switch (ch.Target)
                {
                    case AnimationChannel::Path::Translation: T[ch.JointIndex] = glm::vec3(v); break;
                    case AnimationChannel::Path::Scale:       S[ch.JointIndex] = glm::vec3(v); break;
                    case AnimationChannel::Path::Rotation:    R[ch.JointIndex] = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); break;
                }
            }
        }

        std::vector<glm::mat4> global(n);
        out.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            glm::mat4 local = glm::translate(glm::mat4(1.0f), T[i]) * glm::toMat4(R[i]) * glm::scale(glm::mat4(1.0f), S[i]);
            int p = skeleton.Joints[i].Parent;
            global[i] = (p >= 0 ? global[p] : skeleton.RootTransform) * local;
            out[i] = global[i] * skeleton.Joints[i].InverseBind;
        }
    }

}
