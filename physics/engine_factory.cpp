#include "cpu_engine.h"

std::unique_ptr<physics_engine> make_cpu_engine()
{
    return std::make_unique<cpu_engine>();
}

#ifndef _WIN64
std::unique_ptr<physics_engine> make_cuda_engine(std::string& error)
{
    error = "CUDA is not available in 32-bit builds";
    return nullptr;
}

std::vector<std::string> cuda_device_names()
{
    return {};
}
#endif

std::unique_ptr<physics_engine> make_engine(engine_kind kind, std::string& error)
{
    if (kind == engine_kind::cuda)
    {
        auto engine = make_cuda_engine(error);
        if (engine)
            return engine;
    }
    return make_cpu_engine();
}
