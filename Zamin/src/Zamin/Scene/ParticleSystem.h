#pragma once

#include "Zamin/Core/Base.h"
#include "Zamin/Scene/PropertyAnimation.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

// Particle systems without the scene: the particle, the over-lifetime curves and gradients, and the
// simulation (spawning from the emitter shapes, motion, noise, collisions, trails, ageing) and the
// look of a particle at its age (colour, size, flipbook frame). ParticleSystemComponent
// (Components.h) holds the settings; the scene (SceneParticles.cpp) runs it for every emitter,
// gives it what it needs from around it (mesh shapes, physics for collisions, the sub-emitters'
// systems) and draws it.
namespace Zamin {

    struct ParticleSystemComponent;

    struct Particle
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        float Rotation = 0.0f;
        float AngularVelocity = 0.0f;
        float Life = 0.0f;      // remaining
        float Lifetime = 1.0f;  // total
        uint32_t Seed = 0;      // its own random numbers (gradient / curve blend, start frame): ParticleSimulation::SeedRandom
        glm::vec4 Tint{ 1.0f }; // multiplies its colour (a sub-emitter's particles: the colour they inherit)
        float SizeScale = 1.0f; // multiplies its size (a sub-emitter's particles: the size they inherit)
        int32_t Trail = -1;     // its trail in the system's TrailPool (Trails module); -1: none
        uint32_t Flags = 0;     // ParticleFlag_*

        // 0 when born, 1 when it dies.
        float GetAge01() const { return Lifetime > 0.0f ? 1.0f - Life / Lifetime : 1.0f; }
        // Seconds since it was born.
        float GetAge() const { return Lifetime - Life; }
    };

    enum ParticleFlags : uint32_t
    {
        ParticleFlag_Contact = 1u << 0, // touching a collision surface (Collision module) since the last frame
    };

    // A colour over 0..1 (a particle's age): stops by time, blended linearly between neighbours
    // (Fixed: a stop's colour holds until the next one). Before the first / after the last stop
    // their colour; without stops white.
    struct ParticleGradient
    {
        struct Stop
        {
            float Time = 0.0f;
            glm::vec4 Color{ 1.0f };
            bool operator==(const Stop&) const = default;
        };
        std::vector<Stop> Stops;
        bool Fixed = false;

        glm::vec4 Evaluate(float t) const;
        // Stops back in time order (stable: equal times keep their order).
        void Sort();
        // A stop at `t` with the colour the gradient has there; returns its index.
        int AddStop(float t);
        bool operator==(const ParticleGradient&) const = default;

        static ParticleGradient TwoStops(const glm::vec4& from, const glm::vec4& to);
    };

    // A number over 0..1 (a particle's age): animation keys (AnimationKey: Time 0..1, Value.x) with
    // the same Step / Linear / Bezier interpolation as the animation clips (PropertyAnimation).
    // Without keys 1.
    struct ParticleCurve
    {
        std::vector<AnimationKey> Keys;

        float Evaluate(float t) const;
        // Keys back in time order, then the automatic tangents (call after editing keys).
        void Sort();
        // The smallest / largest value the curve takes over 0..1 (sampled between the keys: Bezier
        // keys with free tangents may overshoot them).
        float GetMin() const;
        float GetMax() const;
        // A key at `t` with the curve's value there (interpolation of the key before it); returns its index.
        int AddKey(float t);
        bool operator==(const ParticleCurve&) const = default;

        static ParticleCurve Constant(float value);
        static ParticleCurve Linear(float from, float to);
    };

    // How the Mesh shape picks a point: on a triangle (by area), on an edge (by length; the
    // diagonals inside flat faces left out) or at a vertex.
    enum class ParticleMeshMode { Triangle = 0, Edge, Vertex };

    // A mesh to emit from (the Mesh shape): its triangles in the mesh's own space with what picking
    // a random point needs. Built once per mesh (the scene caches it).
    struct ParticleEmitterMesh
    {
        std::vector<glm::vec3> Positions;
        std::vector<uint32_t> Indices;        // three per triangle
        std::vector<glm::vec3> VertexNormals; // the area-weighted normals of the triangles around each vertex
        std::vector<glm::vec3> FaceNormals;   // by triangle
        std::vector<float> AreaSums;          // running total of the triangles' areas
        std::vector<float> EdgeSums;          // running total of the edges' lengths (three per triangle)

        size_t GetTriangleCount() const { return Indices.size() / 3; }
        bool IsEmpty() const { return Positions.empty() || Indices.size() < 3; }
        float GetArea() const { return AreaSums.empty() ? 0.0f : AreaSums.back(); }

        // Triangles with indices out of range are left out.
        static Ref<ParticleEmitterMesh> Build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);
        // A random point of the mesh and the surface normal there (smooth: the vertex normals blended).
        void Sample(ParticleMeshMode mode, uint32_t& state, glm::vec3& position, glm::vec3& normal) const;
    };

    // The trails of a system's particles (Trails module): Capacity points per trail in one array,
    // each trail a ring of them (oldest first). A particle's Trail is its slot here.
    struct ParticleTrailPool
    {
        struct Point
        {
            glm::vec3 Position{ 0.0f }; // where the particle was (the particles' space)
            float Time = 0.0f;          // when (the system's Time)
        };
        int Capacity = 0;
        std::vector<Point> Points;      // slot * Capacity + ring index
        std::vector<uint16_t> First;    // by slot: the ring index of the oldest point
        std::vector<uint16_t> Count;    // by slot: how many points it has
        std::vector<int32_t> Free;      // slots not in use

        // Empty, `capacity` points per trail from now on.
        void Reset(int capacity);
        int Allocate();
        void Release(int slot);
        // A new point after the newest (the oldest goes when the ring is full).
        void Push(int slot, const glm::vec3& position, float time);
        // Point `i` of a trail: 0 the newest.
        const Point& Get(int slot, int i) const
        {
            const int ring = (First[(size_t)slot] + Count[(size_t)slot] - 1 - i) % Capacity;
            return Points[(size_t)slot * Capacity + ring];
        }
        int GetCount(int slot) const { return Count[(size_t)slot]; }
        // The oldest points older than `maxAge` at `time` go.
        void Trim(int slot, float time, float maxAge);
        size_t GetSlotCount() const { return Count.size(); }
        size_t GetUsedCount() const { return Count.size() - Free.size(); }
    };

    // Something that happened to a particle (for the sub-emitters), in the particles' space.
    struct ParticleEvent
    {
        enum class Type : uint8_t { Birth = 0, Death, Collision };
        Type Kind = Type::Death;
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        glm::vec4 Color{ 1.0f };
        float Size = 1.0f;
    };

    // What the simulation gets from the scene around it.
    struct ParticleEnvironment
    {
        // World collisions (Collision module, World 2D / 3D): the first surface a sphere of `radius`
        // moving from `from` to `to` (world space) touches - where its centre then is and the
        // surface's normal. Empty: none (outside play, no physics).
        std::function<bool(const glm::vec3& from, const glm::vec3& to, float radius, glm::vec3& center, glm::vec3& normal)> Cast;
        // The collision plane in world space (xyz: unit normal, w: its distance from the origin
        // along it), when the module's plane follows an entity; else the module's own.
        bool HasPlane = false;
        glm::vec4 Plane{ 0.0f, 1.0f, 0.0f, 0.0f };
        // Space Custom: the particles' space in the world (their positions -> world). The
        // emitter's matrix the simulation gets is then where the emitter is in that space.
        // Without it a Custom system is simulated in world space.
        bool HasSpace = false;
        glm::mat4 Space{ 1.0f };
    };

    // Where a sub-emitter's particles start (world space): its shape around Position instead of
    // around the emitter, Velocity added; their colour and size multiplied by Tint / SizeScale.
    struct ParticleSpawnOrigin
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Velocity{ 0.0f };
        glm::vec4 Tint{ 1.0f };
        float SizeScale = 1.0f;
    };

    namespace ParticleSimulation {

        // Xorshift: 0..1 (never 1); advances the state.
        float Random01(uint32_t& state);
        // A random unit vector within `spreadDegrees` of `direction` (180: anywhere).
        glm::vec3 RandomInCone(const glm::vec3& direction, float spreadDegrees, uint32_t& state);
        // A particle's own random number 0..1 for one purpose (`salt`): the same every frame.
        float SeedRandom(uint32_t seed, uint32_t salt);

        // Smooth gradient noise (Perlin) of a point: -1..1, 0 at whole coordinates; the same for the
        // same point and seed.
        float Noise(const glm::vec3& p, uint32_t seed);
        // The Noise module's field: three noises (one per axis) of `octaves` layers (each twice the
        // frequency and `octaveMultiplier` times as strong as the one before), -1..1 per axis.
        glm::vec3 NoiseField(const glm::vec3& p, uint32_t seed, int octaves, float octaveMultiplier, const glm::bvec3& axes = glm::bvec3(true));

        // A point and a direction of the emitter's shape in its local space (from `state`).
        void SampleShape(const ParticleSystemComponent& ps, uint32_t& state, glm::vec3& position, glm::vec3& direction);
        // `count` new particles (at most MaxParticles in all) from the emitter at `emitterWorld`
        // (a sub-emitter's: from `origin`).
        void Spawn(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, int count, const ParticleSpawnOrigin* origin = nullptr);
        // One frame of a playing or paused system, `dt` times its SimulationSpeed: the prewarm
        // (once), then while Playing the start burst, the bursts and the emission of its cycle
        // (Duration, after StartDelay), then Integrate. True in the frame a non-looping system has
        // finished - its cycle over and its last particle dead (Finished; in the editor it starts
        // over instead, to keep previewing).
        bool Update(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt, bool editor, const ParticleEnvironment* environment = nullptr);
        // The bursts of the list that fire between `t0` and `t1` seconds of the system's play time
        // (StartDelay included; t0 inclusive, t1 not): an index into ps.Bursts per firing (before
        // its chance). A looping system's cycles repeat them.
        void GetBurstFirings(const ParticleSystemComponent& ps, float t0, float t1, std::vector<int>& firings);
        // How many of `dt` seconds from play time `t0` the system emits (EmissionRate): not
        // before StartDelay, not after a non-looping system's cycle.
        float GetEmissionTime(const ParticleSystemComponent& ps, float t0, float dt);
        // The particles' space in the world (positions -> world): the emitter (Local), the
        // environment's (Custom); false for world space (identity).
        bool GetSpaceToWorld(const ParticleSystemComponent& ps, const glm::mat4& emitterWorld, const ParticleEnvironment* environment, glm::mat4& out);
        // Forces, velocity over lifetime, speed limit, noise, motion, collisions, trails and ageing;
        // the dead ones are removed. Events for the sub-emitters go to ps.Events.
        void Integrate(ParticleSystemComponent& ps, const glm::mat4& emitterWorld, float dt, const ParticleEnvironment* environment = nullptr);
        // The collision plane in the particles' space (normal, offset) for the emitter at `emitterWorld`.
        glm::vec4 GetCollisionPlane(const ParticleSystemComponent& ps, const glm::mat4& emitterWorld, const ParticleEnvironment* environment);

        // ---- the look of a particle at its age
        glm::vec4 GetColor(const ParticleSystemComponent& ps, const Particle& p);
        float GetSize(const ParticleSystemComponent& ps, const Particle& p);
        // The largest size a particle of the system can have (culling bounds).
        float GetMaxSize(const ParticleSystemComponent& ps);
        // The flipbook frame (0..frameCount-1) a particle shows (Texture Sheet module; 0 without it).
        int GetSheetFrame(const ParticleSystemComponent& ps, const Particle& p, int frameCount);
        // The frames of the Texture Sheet module's grid (Columns x Rows, at most FrameCount).
        int GetGridFrameCount(const ParticleSystemComponent& ps);
        // The order to draw the particles in (indices, the last one on top) for a camera at
        // `viewer` looking along `viewForward` (perspective: by distance to the viewer, else by
        // depth along the view), `toWorld` placing the particles (null: they are in the world).
        // Empty when they are drawn as they are (the Sorting mode says so, or there is nothing to
        // sort). Auto sorts alpha-blended systems by distance.
        void GetDrawOrder(const ParticleSystemComponent& ps, const glm::mat4* toWorld, const glm::vec3& viewer, const glm::vec3& viewForward,
                          bool perspective, std::vector<uint32_t>& order);

        // FollowCamera's wrap: the particles (and their trails) farther than `extents` from `center`
        // along an axis (0: not along it) are moved by twice that to the other side of the box.
        // Returns how many moved.
        int WrapAround(ParticleSystemComponent& ps, const glm::vec3& center, const glm::vec3& extents);
        // A stretched particle's quad (Stretched): its rotation in the plane of `right` / `up` (the
        // quad's Y along the velocity as seen there) and its size (width, length) for a particle of
        // `size` moving at `velocity` (world). False when it barely moves across the view (drawn
        // as it is).
        bool GetStretchedQuad(const ParticleSystemComponent& ps, const glm::vec3& velocity, const glm::vec3& right, const glm::vec3& up,
                              float size, float& rotation, glm::vec2& quad);

        // A trail's width / colour at `u` along it (0 at the particle, 1 at its oldest end) for a
        // particle of `size` and `color`.
        float GetTrailWidth(const ParticleSystemComponent& ps, float size, float u);
        glm::vec4 GetTrailColor(const ParticleSystemComponent& ps, const glm::vec4& color, float u);

    }

}
