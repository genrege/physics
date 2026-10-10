#include "cuda_runtime.h"
#include "device_launch_parameters.h"

#include <stdio.h>
#include "simulation.h"
#include "test.cuh"

__global__ void addKernel(int* c, const int* a, const int* b)
{
    int i = threadIdx.x;
    c[i] = a[i] + b[i];
}

__global__ void gravityForceKernel(int n, vector2* f,  const double* m, const vector2* p)
{
    constexpr double G = 6.6743E-11;

    const int i= blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;
    const int j = blockIdx.y * blockDim.y + threadIdx.y;
    if (j >= n)
        return;
    if (i == j)
        return;

    if (i == 0 && j != 0)
        printf("");

    const auto& position1 = p[i];

    if (i != j)
    {
        const auto& position2 = p[j];

        //Unit vector m1->m2
        const auto& u = (position2 - position1).unit_vector();

        const double r2 = position1.distance(position2) * position1.distance(position2);
        const double f_i = (G * m[i]) * (m[j] / r2);
        f[i] = f[i] + f_i * u;
    }
}

cudaError_t gravitational_force(std::vector<vector2>& f, const std::vector<mass>& masses, const std::vector<mass_state>& mass_states)
{
    double* dev_m = nullptr;
    vector2* dev_p = nullptr;
    vector2* dev_f = nullptr;
    
    cudaError_t cudaStatus;
    cudaStatus = cudaSetDevice(1);

    cudaStatus = cudaMalloc((void**)&dev_m, masses.size() * sizeof(double));
    cudaStatus = cudaMalloc((void**)&dev_p, masses.size() * sizeof(vector2));
    cudaStatus = cudaMalloc((void**)&dev_f, masses.size() * sizeof(vector2));

    double* m = new double[masses.size()];
    vector2* p = new vector2[masses.size()];

    for (size_t i = 0; i < masses.size(); ++i)
    {
        m[i] = masses[i].m();
        p[i] = mass_states[i].position_;
    }

    cudaStatus = cudaMemcpy(dev_m, m, masses.size() * sizeof(double), cudaMemcpyHostToDevice);
    cudaStatus = cudaMemcpy(dev_p, p, masses.size() * sizeof(vector2), cudaMemcpyHostToDevice);

    const int gs = 32;
    int extra = (masses.size() % gs == 0) ? 0 : 1;
    dim3 threads((unsigned int)masses.size(), (unsigned int)masses.size());
    dim3 blocks(masses.size() / gs + extra, masses.size() / gs + extra);    
    gravityForceKernel<<<threads, blocks>>>((int)masses.size(), dev_f, dev_m, dev_p);

    vector2* df = new vector2[masses.size()];
    cudaMemcpy((void*)df, dev_f, masses.size() * sizeof(vector2), cudaMemcpyDeviceToHost);

    f.resize(masses.size());
    for (size_t i = 0; i < masses.size(); ++i)
        f[i] = df[i];
        
    delete[] p;
    delete[] m;

    cudaFree(dev_f);
    cudaFree(dev_p);
    cudaFree(dev_m);

    return cudaStatus;
}

