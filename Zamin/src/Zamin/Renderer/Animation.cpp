#include "Zamin/Renderer/Animation.h"

#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <atomic>
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

    void SampleMorphChannel(const MorphWeightChannel& c, float t, float* out)
    {
        const size_t n = c.Count;
        const size_t keys = n > 0 ? std::min(c.Times.size(), c.Values.size() / n) : 0;
        if (keys == 0)
        {
            std::fill(out, out + n, 0.0f);
            return;
        }
        auto copy = [&](size_t k) { std::copy(c.Values.begin() + (std::ptrdiff_t)(k * n), c.Values.begin() + (std::ptrdiff_t)((k + 1) * n), out); };
        if (t <= c.Times.front()) { copy(0); return; }
        if (t >= c.Times[keys - 1]) { copy(keys - 1); return; }
        size_t b = (size_t)(std::upper_bound(c.Times.begin(), c.Times.begin() + (std::ptrdiff_t)keys, t) - c.Times.begin());
        size_t a = b - 1;
        if (c.Step) { copy(a); return; }
        const float span = c.Times[b] - c.Times[a];
        const float f = span > 0.0f ? (t - c.Times[a]) / span : 0.0f;
        for (size_t i = 0; i < n; i++)
            out[i] = c.Values[a * n + i] + (c.Values[b * n + i] - c.Values[a * n + i]) * f;
    }

    AnimationClip TrimClip(const AnimationClip& clip, float start, float end, const std::string& name)
    {
        const float duration = std::max(0.0f, clip.Duration);
        if (!std::isfinite(start)) start = 0.0f;
        start = std::clamp(start, 0.0f, duration);
        end = !std::isfinite(end) || end < 0.0f || end > duration ? duration : end;
        end = std::max(end, start);
        AnimationClip out;
        out.Name = name;
        out.Duration = end - start;
        // Keys strictly inside (start, end), shifted; the sampled values at both ends.
        auto inside = [&](const std::vector<float>& times, size_t& first, size_t& last) {
            first = (size_t)(std::upper_bound(times.begin(), times.end(), start) - times.begin());
            last = (size_t)(std::lower_bound(times.begin(), times.end(), end) - times.begin());
            if (last < first) last = first;
        };
        for (const AnimationChannel& c : clip.Channels)
        {
            AnimationChannel t;
            t.JointIndex = c.JointIndex;
            t.Target = c.Target;
            t.Step = c.Step;
            if (!c.Times.empty())
            {
                size_t first = 0, last = 0;
                inside(c.Times, first, last);
                t.Times.push_back(0.0f);
                t.Values.push_back(Sample(c, start));
                for (size_t k = first; k < last && k < c.Values.size(); k++)
                {
                    t.Times.push_back(c.Times[k] - start);
                    t.Values.push_back(c.Values[k]);
                }
                if (end > start)
                {
                    t.Times.push_back(end - start);
                    t.Values.push_back(Sample(c, end));
                }
            }
            out.Channels.push_back(std::move(t));
        }
        for (const MorphWeightChannel& c : clip.MorphChannels)
        {
            MorphWeightChannel t;
            t.Group = c.Group;
            t.Count = c.Count;
            t.Step = c.Step;
            if (!c.Times.empty() && c.Count > 0)
            {
                std::vector<float> values(c.Count);
                auto push = [&](float time, float local) {
                    SampleMorphChannel(c, time, values.data());
                    t.Times.push_back(local);
                    t.Values.insert(t.Values.end(), values.begin(), values.end());
                };
                size_t first = 0, last = 0;
                inside(c.Times, first, last);
                push(start, 0.0f);
                for (size_t k = first; k < last && (k + 1) * c.Count <= c.Values.size(); k++)
                {
                    t.Times.push_back(c.Times[k] - start);
                    t.Values.insert(t.Values.end(), c.Values.begin() + (std::ptrdiff_t)(k * c.Count),
                                    c.Values.begin() + (std::ptrdiff_t)((k + 1) * c.Count));
                }
                if (end > start)
                    push(end, end - start);
            }
            out.MorphChannels.push_back(std::move(t));
        }
        return out;
    }

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

    void SampleLocalPose(const Skeleton& skeleton, const AnimationClip* clip, float time, LocalPose& out, int rootMotionJoint)
    {
        SamplePose(skeleton, clip, time, out);
        if (rootMotionJoint >= 0 && rootMotionJoint < (int)out.T.size())
            StripRootMotion(skeleton, clip, rootMotionJoint, out.T[rootMotionJoint], out.R[rootMotionJoint]);
    }

    void BlendLocalPose(LocalPose& pose, const LocalPose& other, float weight, const std::vector<float>* mask)
    {
        weight = std::clamp(weight, 0.0f, 1.0f);
        if (weight <= 0.0f)
            return;
        const size_t n = std::min(pose.T.size(), other.T.size());
        for (size_t i = 0; i < n; i++)
        {
            float w = weight;
            if (mask)
                w *= i < mask->size() ? std::clamp((*mask)[i], 0.0f, 1.0f) : 0.0f;
            if (w <= 0.0f)
                continue;
            pose.T[i] = glm::mix(pose.T[i], other.T[i], w);
            pose.S[i] = glm::mix(pose.S[i], other.S[i], w);
            glm::quat q = glm::dot(pose.R[i], other.R[i]) < 0.0f ? -other.R[i] : other.R[i];
            pose.R[i] = glm::normalize(glm::slerp(pose.R[i], q, w));
        }
    }

    void AddLocalPose(LocalPose& pose, const LocalPose& other, const LocalPose& reference, float weight, const std::vector<float>* mask)
    {
        weight = std::clamp(weight, 0.0f, 1.0f);
        if (weight <= 0.0f)
            return;
        const size_t n = std::min({ pose.T.size(), other.T.size(), reference.T.size() });
        const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < n; i++)
        {
            float w = weight;
            if (mask)
                w *= i < mask->size() ? std::clamp((*mask)[i], 0.0f, 1.0f) : 0.0f;
            if (w <= 0.0f)
                continue;
            pose.T[i] += (other.T[i] - reference.T[i]) * w;
            glm::vec3 ratio(1.0f);
            for (int c = 0; c < 3; c++)
                if (std::abs(reference.S[i][c]) > 1e-6f)
                    ratio[c] = other.S[i][c] / reference.S[i][c];
            pose.S[i] *= glm::mix(glm::vec3(1.0f), ratio, w);
            glm::quat delta = glm::normalize(other.R[i] * glm::inverse(reference.R[i]));
            if (delta.w < 0.0f)
                delta = -delta;
            pose.R[i] = glm::normalize(glm::slerp(identity, delta, w) * pose.R[i]);
        }
    }

    void ComposeJointMatrices(const Skeleton& skeleton, const LocalPose& pose, std::vector<glm::mat4>& out)
    {
        if (pose.T.size() != skeleton.Joints.size())
        {
            LocalPose rest;
            SamplePose(skeleton, nullptr, 0.0f, rest);
            ComposePose(skeleton, rest, out);
            return;
        }
        ComposePose(skeleton, pose, out);
    }

    // ---- pose cache
    static std::atomic<size_t> s_BakedBytes{ 0 };

    BakedClip::~BakedClip()
    {
        s_BakedBytes -= m_Bytes;
    }

    size_t GetBakedClipMemory()
    {
        return s_BakedBytes.load();
    }

    void BakeClip(const Skeleton& skeleton, const AnimationClip& clip, float rate, BakedClip& out)
    {
        s_BakedBytes -= out.m_Bytes;
        const size_t n = skeleton.Joints.size();
        if (!std::isfinite(rate) || rate < 1.0f)
            rate = 1.0f;
        out.Duration = std::max(0.0f, clip.Duration);
        out.Rate = rate;
        out.Frames = std::max<uint32_t>(2, (uint32_t)std::ceil(out.Duration * rate - 1e-4f) + 1);
        out.Step = out.Duration / (float)(out.Frames - 1);
        out.Joints = n;
        out.FromClip.assign(n, 0);
        for (const auto& ch : clip.Channels)
            if (ch.Step && ch.JointIndex >= 0 && ch.JointIndex < (int)n)
                out.FromClip[(size_t)ch.JointIndex] = 1;
        out.T.resize((size_t)out.Frames * n);
        out.S.resize((size_t)out.Frames * n);
        out.R.resize((size_t)out.Frames * n);
        LocalPose pose;
        for (uint32_t f = 0; f < out.Frames; f++)
        {
            const float t = f + 1 == out.Frames ? out.Duration : (float)f * out.Step;
            SamplePose(skeleton, &clip, t, pose);
            std::copy(pose.T.begin(), pose.T.end(), out.T.begin() + (std::ptrdiff_t)(f * n));
            std::copy(pose.S.begin(), pose.S.end(), out.S.begin() + (std::ptrdiff_t)(f * n));
            std::copy(pose.R.begin(), pose.R.end(), out.R.begin() + (std::ptrdiff_t)(f * n));
        }
        out.m_Bytes = out.T.size() * sizeof(glm::vec3) * 2 + out.R.size() * sizeof(glm::quat) + out.FromClip.size();
        s_BakedBytes += out.m_Bytes;
    }

    void SampleBakedPose(const Skeleton& skeleton, const AnimationClip& clip, const BakedClip& baked, float time, LocalPose& out,
                         int rootMotionJoint)
    {
        const size_t n = skeleton.Joints.size();
        if (baked.Joints != n || baked.Frames < 2 || baked.T.size() != (size_t)baked.Frames * n)
        {
            SampleLocalPose(skeleton, &clip, time, out, rootMotionJoint);
            return;
        }
        out.T.resize(n); out.S.resize(n); out.R.resize(n);
        if (!std::isfinite(time))
            time = 0.0f;
        const float x = baked.Step > 0.0f ? std::clamp(time, 0.0f, baked.Duration) / baked.Step : 0.0f;
        const uint32_t a = std::min((uint32_t)x, baked.Frames - 2);
        const float f = std::clamp(x - (float)a, 0.0f, 1.0f);
        const size_t ia = (size_t)a * n, ib = ia + n;
        for (size_t i = 0; i < n; i++)
        {
            out.T[i] = glm::mix(baked.T[ia + i], baked.T[ib + i], f);
            out.S[i] = glm::mix(baked.S[ia + i], baked.S[ib + i], f);
            const glm::quat& qa = baked.R[ia + i];
            glm::quat qb = baked.R[ib + i];
            if (glm::dot(qa, qb) < 0.0f)
                qb = -qb;
            // Frames are close: a normalized linear mix is as good as slerp there (and cheaper).
            out.R[i] = glm::normalize(glm::quat(qa.w + (qb.w - qa.w) * f, qa.x + (qb.x - qa.x) * f, qa.y + (qb.y - qa.y) * f,
                                                qa.z + (qb.z - qa.z) * f));
        }
        // Joints with step keys: from the clip itself.
        bool any = false;
        for (uint8_t v : baked.FromClip)
            any |= v != 0;
        if (any)
        {
            for (size_t i = 0; i < n; i++)
                if (baked.FromClip[i])
                {
                    out.T[i] = skeleton.Joints[i].RestTranslation;
                    out.R[i] = skeleton.Joints[i].RestRotation;
                    out.S[i] = skeleton.Joints[i].RestScale;
                }
            for (const auto& ch : clip.Channels)
            {
                if (ch.JointIndex < 0 || ch.JointIndex >= (int)n || !baked.FromClip[(size_t)ch.JointIndex])
                    continue;
                const glm::vec4 v = Sample(ch, time);
                switch (ch.Target)
                {
                    case AnimationChannel::Path::Translation: out.T[(size_t)ch.JointIndex] = glm::vec3(v); break;
                    case AnimationChannel::Path::Scale:       out.S[(size_t)ch.JointIndex] = glm::vec3(v); break;
                    case AnimationChannel::Path::Rotation:    out.R[(size_t)ch.JointIndex] = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); break;
                }
            }
        }
        if (rootMotionJoint >= 0 && rootMotionJoint < (int)n)
            StripRootMotion(skeleton, &clip, rootMotionJoint, out.T[(size_t)rootMotionJoint], out.R[(size_t)rootMotionJoint]);
    }

    void ComputeBlend1DWeights(const std::vector<float>& thresholds, float value, std::vector<float>& out)
    {
        const size_t n = thresholds.size();
        out.assign(n, 0.0f);
        if (n == 0)
            return;
        if (!std::isfinite(value))
            value = 0.0f;
        // The nearest motion at or below the value and the nearest above it.
        int below = -1, above = -1;
        for (size_t i = 0; i < n; i++)
        {
            const float t = thresholds[i];
            if (t <= value && (below < 0 || t > thresholds[below]))
                below = (int)i;
            if (t > value && (above < 0 || t < thresholds[above]))
                above = (int)i;
        }
        if (below < 0) { out[above] = 1.0f; return; }  // under the lowest
        if (above < 0) { out[below] = 1.0f; return; }  // at / over the highest
        const float span = thresholds[above] - thresholds[below];
        const float f = span > 0.0f ? std::clamp((value - thresholds[below]) / span, 0.0f, 1.0f) : 0.0f;
        out[below] = 1.0f - f;
        out[above] += f;
    }

    size_t BuildJointMask(const Skeleton& skeleton, const std::vector<std::string>& roots, std::vector<float>& out)
    {
        const size_t n = skeleton.Joints.size();
        bool any = false;
        for (const std::string& r : roots)
            any |= !r.empty();
        if (!any)
        {
            out.assign(n, 1.0f);
            return n;
        }
        out.assign(n, 0.0f);
        // Parents come before their children: one pass marks every subtree.
        for (size_t i = 0; i < n; i++)
        {
            const Joint& j = skeleton.Joints[i];
            bool in = std::find(roots.begin(), roots.end(), j.Name) != roots.end();
            if (!in && j.Parent >= 0 && (size_t)j.Parent < i)
                in = out[(size_t)j.Parent] > 0.0f;
            out[i] = in ? 1.0f : 0.0f;
        }
        size_t count = 0;
        for (float v : out)
            count += v > 0.0f ? 1 : 0;
        return count;
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

    glm::mat4 GetJointTransform(const Skeleton& skeleton, const std::vector<glm::mat4>& jointMatrices, int joint)
    {
        const size_t n = skeleton.Joints.size();
        if (joint < 0 || (size_t)joint >= n)
            return skeleton.RootTransform;
        if (jointMatrices.size() == n)
            return jointMatrices[(size_t)joint] * glm::inverse(skeleton.Joints[(size_t)joint].InverseBind);
        // the rest pose: the chain from the root down
        int chain[256];
        int count = 0;
        for (int j = joint; j >= 0 && count < 256; j = skeleton.Joints[(size_t)j].Parent)
            chain[count++] = j;
        glm::mat4 m = skeleton.RootTransform;
        for (int i = count - 1; i >= 0; i--)
        {
            const Joint& j = skeleton.Joints[(size_t)chain[i]];
            m = m * glm::translate(glm::mat4(1.0f), j.RestTranslation) * glm::toMat4(j.RestRotation) * glm::scale(glm::mat4(1.0f), j.RestScale);
        }
        return m;
    }

    int FindJoint(const Skeleton& skeleton, const std::string& name)
    {
        for (size_t i = 0; i < skeleton.Joints.size(); i++)
            if (skeleton.Joints[i].Name == name)
                return (int)i;
        return -1;
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
