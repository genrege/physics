#pragma once

#include "simulation.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#if defined(PHYSICS_CUDA_EXPORTS)
#define PHYSICS_CUDA_API __declspec(dllexport)
#elif defined(_WIN64)
#define PHYSICS_CUDA_API __declspec(dllimport)
#else
#define PHYSICS_CUDA_API   // 32-bit builds have no CUDA; engine_factory.cpp supplies a stub
#endif

enum class force_generator
{
    gravity,
    spring,
    damper,
    total
};

// One time-stepping implementation (CPU or CUDA). Each stage is public so engines can be compared stage by stage.
// Per step: update_broken, compute_* (each writes its own per-mass force array), sum_forces, integrate, collisions, walls.
class physics_engine
{
public:
    virtual ~physics_engine() = default;

    virtual const char* name() const = 0;

    // Replaces the engine state with the given host state.
    virtual void upload(const simulation& sim, const std::vector<mass_state>& masses, const std::vector<spring_state>& springs, const std::vector<damper_state>& dampers) = 0;

    // Copies the complete engine state back to the host.
    virtual void download(std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers) = 0;

    // Copies only what rendering needs (positions, velocities, broken flags).
    virtual void download_render(std::vector<mass_state>& masses, std::vector<spring_state>& springs) = 0;

    virtual void update_broken() = 0;
    virtual void compute_gravity() = 0;
    virtual void compute_springs() = 0;
    virtual void compute_dampers() = 0;
    virtual void sum_forces() = 0;           // total force and acceleration from the per-generator arrays
    virtual void integrate(double dt) = 0;
    virtual void collisions() = 0;
    virtual void walls() = 0;

    virtual void read_forces(force_generator generator, std::vector<vector2>& out) = 0;

    // Waits for any queued work; a no-op for synchronous engines.
    virtual void synchronize() {}

    virtual void step(size_t iterations, double dt)
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            update_broken();
            compute_gravity();
            compute_springs();
            compute_dampers();
            sum_forces();
            integrate(dt);
            collisions();
            walls();
        }
        synchronize();
    }
};

enum class engine_kind
{
    cpu,
    cuda
};

std::unique_ptr<physics_engine> make_cpu_engine();

// Returns nullptr (and fills error) if CUDA is unavailable.
PHYSICS_CUDA_API std::unique_ptr<physics_engine> make_cuda_engine(std::string& error);

// Device names reported by the CUDA runtime (empty if none).
PHYSICS_CUDA_API std::vector<std::string> cuda_device_names();

// Creates the requested engine, falling back to CPU if CUDA cannot be used.
std::unique_ptr<physics_engine> make_engine(engine_kind kind, std::string& error);
