#include "physics_engine.h"
#include "physics_math.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <thrust/device_ptr.h>
#include <thrust/sequence.h>
#include <thrust/sort.h>
#include <thrust/system/cuda/execution_policy.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#define CUDA_CHECK(call)                                                                        \
    do {                                                                                        \
        const cudaError_t cuda_check_status_ = (call);                                          \
        if (cuda_check_status_ != cudaSuccess)                                                  \
            throw std::runtime_error(std::string(#call) + ": " + cudaGetErrorString(cuda_check_status_)); \
    } while (0)

namespace
{
    constexpr int block_size = 128;

    int blocks_for(size_t n) { return static_cast<int>((n + block_size - 1) / block_size); }

    template<typename T>
    class dev_array
    {
    public:
        dev_array() = default;
        dev_array(const dev_array&) = delete;
        dev_array& operator=(const dev_array&) = delete;
        ~dev_array() { if (ptr_) cudaFree(ptr_); }

        void resize(size_t n)
        {
            if (n != count_)
            {
                if (ptr_)
                    cudaFree(ptr_);
                ptr_ = nullptr;
                if (n)
                    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&ptr_), n * sizeof(T)));
                count_ = n;
            }
        }

        void set(const std::vector<T>& host)
        {
            resize(host.size());
            if (!host.empty())
                CUDA_CHECK(cudaMemcpy(ptr_, host.data(), host.size() * sizeof(T), cudaMemcpyHostToDevice));
        }

        void get(std::vector<T>& host) const
        {
            host.resize(count_);
            if (count_)
                CUDA_CHECK(cudaMemcpy(host.data(), ptr_, count_ * sizeof(T), cudaMemcpyDeviceToHost));
        }

        void zero()
        {
            if (count_)
                CUDA_CHECK(cudaMemset(ptr_, 0, count_ * sizeof(T)));
        }

        // Asynchronous on the default stream; dst must be pinned for the copy to overlap.
        void copy_to_async(T* dst) const
        {
            if (count_)
                CUDA_CHECK(cudaMemcpyAsync(dst, ptr_, count_ * sizeof(T), cudaMemcpyDeviceToHost));
        }

        T* data() { return ptr_; }
        const T* data() const { return ptr_; }
        size_t size() const { return count_; }

    private:
        T* ptr_ = nullptr;
        size_t count_ = 0;
    };

    template<typename T>
    class pinned_array
    {
    public:
        pinned_array() = default;
        pinned_array(const pinned_array&) = delete;
        pinned_array& operator=(const pinned_array&) = delete;
        ~pinned_array() { if (ptr_) cudaFreeHost(ptr_); }

        void resize(size_t n)
        {
            if (n == count_)
                return;
            if (ptr_)
                cudaFreeHost(ptr_);
            ptr_ = nullptr;
            if (n)
                CUDA_CHECK(cudaMallocHost(reinterpret_cast<void**>(&ptr_), n * sizeof(T)));
            count_ = n;
        }

        T* data() { return ptr_; }
        const T& operator[](size_t i) const { return ptr_[i]; }

    private:
        T* ptr_ = nullptr;
        size_t count_ = 0;
    };

    // Thrust asks for temporary storage on every sort; keep one block alive instead of cudaMalloc/cudaFree per step.
    struct scratch_block
    {
        char* ptr = nullptr;
        size_t size = 0;
        ~scratch_block() { if (ptr) cudaFree(ptr); }
    };

    struct cached_allocator
    {
        using value_type = char;
        scratch_block* block;

        char* allocate(std::ptrdiff_t bytes)
        {
            if (static_cast<size_t>(bytes) > block->size)
            {
                if (block->ptr)
                    cudaFree(block->ptr);
                block->ptr = nullptr;
                block->size = 0;
                CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&block->ptr), static_cast<size_t>(bytes)));
                block->size = static_cast<size_t>(bytes);
            }
            return block->ptr;
        }

        void deallocate(char*, size_t) {}
    };

    // CSR rows are built so entries stay in ascending constraint order, matching the CPU summation order.
    struct csr
    {
        std::vector<int> offsets;
        std::vector<int> entries;
    };

    csr build_csr(size_t rows, const std::vector<std::pair<int, int>>& row_entry_pairs)
    {
        csr result;
        result.offsets.assign(rows + 1, 0);
        for (const auto& p : row_entry_pairs)
            ++result.offsets[p.first + 1];
        for (size_t i = 0; i < rows; ++i)
            result.offsets[i + 1] += result.offsets[i];

        result.entries.resize(row_entry_pairs.size());
        std::vector<int> cursor(result.offsets.begin(), result.offsets.end() - 1);
        for (const auto& p : row_entry_pairs)
            result.entries[cursor[p.first]++] = p.second;
        return result;
    }
}

__global__ void kernel_update_broken(int spring_count, const vector2* pos, const int* id1, const int* id2,
    const double* min_length, const double* max_length, unsigned char* spring_broken,
    const int* sd_offsets, const int* sd_entries, unsigned char* damper_broken)
{
    const int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= spring_count || spring_broken[s])
        return;

    const auto length = pos[id1[s]].distance(pos[id2[s]]);
    if (length < min_length[s] || length > max_length[s])
    {
        spring_broken[s] = 1;
        for (int e = sd_offsets[s]; e < sd_offsets[s + 1]; ++e)
            damper_broken[sd_entries[e]] = 1;
    }
}

__global__ void kernel_gravity(int n, const double* mass, const vector2* pos, double min_gravitational_mass, double softening, vector2* force)
{
    __shared__ double tile_m[block_size];
    __shared__ double tile_x[block_size];
    __shared__ double tile_y[block_size];

    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const bool active = i < n;
    const double mi = active ? mass[i] : 0.0;
    const vector2 pi = active ? pos[i] : vector2();
    vector2 sum;

    // Tiles are walked in ascending j so the sum matches the CPU's per-mass partner order.
    for (int tile = 0; tile < n; tile += block_size)
    {
        const int jl = tile + threadIdx.x;
        if (jl < n)
        {
            tile_m[threadIdx.x] = mass[jl];
            tile_x[threadIdx.x] = pos[jl].x();
            tile_y[threadIdx.x] = pos[jl].y();
        }
        __syncthreads();

        if (active)
        {
            const int count = min(block_size, n - tile);
            for (int t = 0; t < count; ++t)
            {
                const int j = tile + t;
                if (j == i)
                    continue;

                const vector2 pj(tile_x[t], tile_y[t]);
                vector2 f;
                if (j > i)
                {
                    if (physics_math::gravity_pair(mi, tile_m[t], pi, pj, min_gravitational_mass, softening, f))
                        sum += f;
                }
                else
                {
                    if (physics_math::gravity_pair(tile_m[t], mi, pj, pi, min_gravitational_mass, softening, f))
                        sum -= f;
                }
            }
        }
        __syncthreads();
    }

    if (active)
        force[i] = sum;
}

__global__ void kernel_springs(int n, const vector2* pos, const int* offsets, const int* entries,
    const int* id1, const int* id2, const double* k, const double* l0, const unsigned char* broken, vector2* force)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    vector2 sum;
    for (int e = offsets[i]; e < offsets[i + 1]; ++e)
    {
        const int s = entries[e] >> 1;
        if (broken[s])
            continue;

        const auto f = physics_math::spring_pair_force(pos[id1[s]], pos[id2[s]], k[s], l0[s]);
        if ((entries[e] & 1) == 0)
            sum += f;
        else
            sum -= f;
    }
    force[i] = sum;
}

__global__ void kernel_dampers(int n, const vector2* pos, const vector2* vel, const int* offsets, const int* entries,
    const int* id1, const int* id2, const double* k, const unsigned char* broken, vector2* force)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    vector2 sum;
    for (int e = offsets[i]; e < offsets[i + 1]; ++e)
    {
        const int d = entries[e] >> 1;
        if (broken[d])
            continue;

        const int a = id1[d];
        const int b = id2[d];
        const auto f = physics_math::damper_pair_force(pos[a], pos[b], vel[a], vel[b], k[d]);
        if ((entries[e] & 1) == 0)
            sum += f;
        else
            sum -= f;
    }
    force[i] = sum;
}

__global__ void kernel_sum_forces(int n, const double* mass, const vector2* f_gravity, const vector2* f_spring,
    const vector2* f_damper, vector2* f_total, vector2* acceleration)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    const auto total = (f_gravity[i] + f_spring[i]) + f_damper[i];
    f_total[i] = total;
    acceleration[i] = total / mass[i];
}

__global__ void kernel_integrate(int n, double dt, const unsigned char* fixed, const vector2* acceleration, vector2* vel, vector2* pos)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || fixed[i])
        return;

    vel[i] += acceleration[i] * dt;
    pos[i] += vel[i] * dt;
}

__global__ void kernel_walls(int n, double xmin, double xmax, double ymin, double ymax, bool left, bool right, bool top, bool bottom,
    const double* radius, const vector2* pos, vector2* vel)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    vel[i] = physics_math::wall_bounce(pos[i], vel[i], radius[i], xmin, xmax, ymin, ymax, left, right, top, bottom);
}

__global__ void kernel_cell_keys(int n, double cell_size, const vector2* pos, long long* keys, int* index)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    keys[i] = physics_math::cell_key(physics_math::cell_coordinate(pos[i].x(), cell_size), physics_math::cell_coordinate(pos[i].y(), cell_size));
    index[i] = i;
}

__device__ int lower_bound_key(const long long* keys, int n, long long key)
{
    int lo = 0;
    int hi = n;
    while (lo < hi)
    {
        const int mid = (lo + hi) >> 1;
        if (keys[mid] < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

__global__ void kernel_collide(int n, double cell_size, const vector2* pos, const vector2* vel, const double* mass,
    const double* radius, const double* elasticity, const long long* sorted_keys, const int* sorted_index,
    vector2* dpos, vector2* dvel)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    const auto cx = physics_math::cell_coordinate(pos[i].x(), cell_size);
    const auto cy = physics_math::cell_coordinate(pos[i].y(), cell_size);

    vector2 sum_dpos;
    vector2 sum_dvel;

    // Each neighbouring cell is a run of ascending indices in the sorted arrays; merge the nine runs so
    // contacts are summed in ascending partner index, matching the CPU engine bit for bit.
    int cursor[9];
    int run_end[9];
    for (int c = 0; c < 9; ++c)
    {
        const auto key = physics_math::cell_key(cx + (c / 3 - 1), cy + (c % 3 - 1));
        cursor[c] = lower_bound_key(sorted_keys, n, key);
        run_end[c] = lower_bound_key(sorted_keys, n, key + 1);
    }

    while (true)
    {
        int best = -1;
        int j = 0x7fffffff;
        for (int c = 0; c < 9; ++c)
        {
            if (cursor[c] < run_end[c])
            {
                const int candidate = sorted_index[cursor[c]];
                if (candidate < j)
                {
                    j = candidate;
                    best = c;
                }
            }
        }
        if (best < 0)
            break;
        ++cursor[best];

        if (j == i)
            continue;

        // Evaluate with the lower index first so both masses see the same arithmetic as the CPU pair.
        physics_math::collision_delta d;
        if (i < j)
        {
            if (!physics_math::collision_pair(pos[i], pos[j], vel[i], vel[j], mass[i], mass[j], radius[i], radius[j], elasticity[i], elasticity[j], d))
                continue;
            sum_dpos += d.dpos_1;
            sum_dvel += d.dvel_1;
        }
        else
        {
            if (!physics_math::collision_pair(pos[j], pos[i], vel[j], vel[i], mass[j], mass[i], radius[j], radius[i], elasticity[j], elasticity[i], d))
                continue;
            sum_dpos += d.dpos_2;
            sum_dvel += d.dvel_2;
        }
    }

    dpos[i] = sum_dpos;
    dvel[i] = sum_dvel;
}

__global__ void kernel_apply_collisions(int n, const vector2* dpos, const vector2* dvel, vector2* pos, vector2* vel)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    pos[i] += dpos[i];
    vel[i] += dvel[i];
}

namespace
{
    int choose_device()
    {
        int count = 0;
        CUDA_CHECK(cudaGetDeviceCount(&count));
        if (count == 0)
            throw std::runtime_error("no CUDA devices found");

        if (const char* env = std::getenv("PHYSICS_CUDA_DEVICE"))
        {
            const int requested = std::atoi(env);
            if (requested >= 0 && requested < count)
                return requested;
        }

        // Prefer the device with the strongest FP64 throughput (lowest FP32:FP64 ratio), e.g. Titan V over GeForce.
        int best = 0;
        int best_ratio = 0;
        for (int d = 0; d < count; ++d)
        {
            int ratio = 0;
            if (cudaDeviceGetAttribute(&ratio, cudaDevAttrSingleToDoublePrecisionPerfRatio, d) != cudaSuccess)
                continue;
            if (d == 0 || ratio < best_ratio)
            {
                best = d;
                best_ratio = ratio;
            }
        }
        return best;
    }

    class cuda_engine : public physics_engine
    {
    public:
        cuda_engine()
        {
            device_ = choose_device();
            bind();

            cudaDeviceProp props;
            CUDA_CHECK(cudaGetDeviceProperties(&props, device_));
            name_ = std::string("CUDA (") + props.name + ")";

            // Fails with "no kernel image" if the device architecture was not compiled in.
            cudaFuncAttributes attributes;
            CUDA_CHECK(cudaFuncGetAttributes(&attributes, kernel_sum_forces));
        }

        ~cuda_engine() override
        {
            cudaSetDevice(device_);
        }

        const char* name() const override { return name_.c_str(); }

        void upload(const simulation& sim, const std::vector<mass_state>& masses, const std::vector<spring_state>& springs, const std::vector<damper_state>& dampers) override
        {
            bind();
            sim_ = std::make_unique<simulation>(sim);

            n_ = masses.size();
            spring_count_ = springs.size();
            damper_count_ = dampers.size();

            std::vector<vector2> pos(n_), vel(n_), acc(n_), force(n_);
            std::vector<double> m(n_), r(n_), e(n_);
            std::vector<unsigned char> fixed(n_);
            fixed_host_.resize(n_);
            angular_velocity_.resize(n_);
            angular_position_.resize(n_);

            const auto& model_masses = sim.model().masses();
            max_r_ = 0.0;
            for (size_t i = 0; i < n_; ++i)
            {
                pos[i] = masses[i].position_;
                vel[i] = masses[i].velocity_;
                acc[i] = masses[i].acceleration_;
                force[i] = masses[i].force_;
                m[i] = model_masses[i].m();
                r[i] = model_masses[i].r();
                e[i] = model_masses[i].e();
                fixed[i] = masses[i].fixed_ ? 1 : 0;
                fixed_host_[i] = fixed[i];
                angular_velocity_[i] = masses[i].angular_velocity_;
                angular_position_[i] = masses[i].angular_position_;
                if (r[i] > max_r_)
                    max_r_ = r[i];
            }
            cell_size_ = std::max(2.0 * max_r_, 1e-3);

            pos_.set(pos);
            vel_.set(vel);
            acc_.set(acc);
            force_total_.set(force);
            force_gravity_.resize(n_);
            force_spring_.resize(n_);
            force_damper_.resize(n_);
            force_gravity_.zero();
            force_spring_.zero();
            force_damper_.zero();
            mass_.set(m);
            radius_.set(r);
            elasticity_.set(e);
            fixed_.set(fixed);

            const auto& model_springs = sim.model().springs();
            std::vector<int> s_id1(spring_count_), s_id2(spring_count_);
            std::vector<double> s_k(spring_count_), s_l0(spring_count_), s_min(spring_count_), s_max(spring_count_);
            std::vector<unsigned char> s_broken(spring_count_);
            std::vector<std::pair<int, int>> spring_rows;
            for (size_t s = 0; s < spring_count_; ++s)
            {
                s_id1[s] = static_cast<int>(model_springs[s].id_mass1());
                s_id2[s] = static_cast<int>(model_springs[s].id_mass2());
                s_k[s] = model_springs[s].k();
                s_l0[s] = model_springs[s].l0();
                s_min[s] = model_springs[s].min_length();
                s_max[s] = model_springs[s].max_length();
                s_broken[s] = springs[s].broken_ ? 1 : 0;
                spring_rows.emplace_back(s_id1[s], static_cast<int>(s) * 2);
                spring_rows.emplace_back(s_id2[s], static_cast<int>(s) * 2 + 1);
            }
            const auto spring_csr = build_csr(n_, spring_rows);
            spring_id1_.set(s_id1);
            spring_id2_.set(s_id2);
            spring_k_.set(s_k);
            spring_l0_.set(s_l0);
            spring_min_.set(s_min);
            spring_max_.set(s_max);
            spring_broken_.set(s_broken);
            spring_offsets_.set(spring_csr.offsets);
            spring_entries_.set(spring_csr.entries);

            const auto& model_dampers = sim.model().dampers();
            std::vector<int> d_id1(damper_count_), d_id2(damper_count_);
            std::vector<double> d_k(damper_count_);
            std::vector<unsigned char> d_broken(damper_count_);
            std::vector<std::pair<int, int>> damper_rows;
            std::unordered_map<unsigned long long, std::vector<int>> dampers_by_pair;
            for (size_t d = 0; d < damper_count_; ++d)
            {
                d_id1[d] = static_cast<int>(model_dampers[d].id_mass1());
                d_id2[d] = static_cast<int>(model_dampers[d].id_mass2());
                d_k[d] = model_dampers[d].k();
                d_broken[d] = dampers[d].broken_ ? 1 : 0;
                damper_rows.emplace_back(d_id1[d], static_cast<int>(d) * 2);
                damper_rows.emplace_back(d_id2[d], static_cast<int>(d) * 2 + 1);
                dampers_by_pair[(static_cast<unsigned long long>(model_dampers[d].id_mass1()) << 32) | model_dampers[d].id_mass2()].push_back(static_cast<int>(d));
            }
            const auto damper_csr = build_csr(n_, damper_rows);
            damper_id1_.set(d_id1);
            damper_id2_.set(d_id2);
            damper_k_.set(d_k);
            damper_broken_.set(d_broken);
            damper_offsets_.set(damper_csr.offsets);
            damper_entries_.set(damper_csr.entries);

            // Springs break their matching dampers: for each spring, the dampers on the same ordered mass pair.
            std::vector<std::pair<int, int>> spring_damper_rows;
            for (size_t s = 0; s < spring_count_; ++s)
            {
                const auto it = dampers_by_pair.find((static_cast<unsigned long long>(model_springs[s].id_mass1()) << 32) | model_springs[s].id_mass2());
                if (it == dampers_by_pair.end())
                    continue;
                for (const int d : it->second)
                    spring_damper_rows.emplace_back(static_cast<int>(s), d);
            }
            const auto spring_damper_csr = build_csr(spring_count_, spring_damper_rows);
            spring_damper_offsets_.set(spring_damper_csr.offsets);
            spring_damper_entries_.set(spring_damper_csr.entries);

            collision_keys_.resize(n_);
            collision_index_.resize(n_);
            collision_dpos_.resize(n_);
            collision_dvel_.resize(n_);

            render_pos_.resize(n_);
            render_vel_.resize(n_);
            render_spring_broken_.resize(spring_count_);

            CUDA_CHECK(cudaDeviceSynchronize());
        }

        void download(std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers) override
        {
            bind();
            std::vector<vector2> pos, vel, acc, force;
            pos_.get(pos);
            vel_.get(vel);
            acc_.get(acc);
            force_total_.get(force);

            masses.resize(n_);
            for (size_t i = 0; i < n_; ++i)
                masses[i] = mass_state(pos[i], force[i], vel[i], acc[i], angular_velocity_[i], angular_position_[i], fixed_host_[i] != 0);

            std::vector<unsigned char> flags;
            spring_broken_.get(flags);
            assign_flags(springs, flags);
            damper_broken_.get(flags);
            assign_flags(dampers, flags);
        }

        void download_render(std::vector<mass_state>& masses, std::vector<spring_state>& springs) override
        {
            bind();
            pos_.copy_to_async(render_pos_.data());
            vel_.copy_to_async(render_vel_.data());
            spring_broken_.copy_to_async(render_spring_broken_.data());
            CUDA_CHECK(cudaStreamSynchronize(0));

            masses.resize(n_);
            for (size_t i = 0; i < n_; ++i)
            {
                masses[i].position_ = render_pos_[i];
                masses[i].velocity_ = render_vel_[i];
                masses[i].fixed_ = fixed_host_[i] != 0;
            }

            if (springs.size() != spring_count_)
            {
                springs.clear();
                for (size_t i = 0; i < spring_count_; ++i)
                    springs.emplace_back(render_spring_broken_[i] != 0);
            }
            else
            {
                for (size_t i = 0; i < spring_count_; ++i)
                    springs[i].broken_ = render_spring_broken_[i] != 0;
            }
        }

        void update_broken() override
        {
            bind();
            if (spring_count_ == 0)
                return;
            kernel_update_broken<<<blocks_for(spring_count_), block_size>>>(static_cast<int>(spring_count_), pos_.data(), spring_id1_.data(), spring_id2_.data(),
                spring_min_.data(), spring_max_.data(), spring_broken_.data(), spring_damper_offsets_.data(), spring_damper_entries_.data(), damper_broken_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void compute_gravity() override
        {
            bind();
            if (n_ == 0)
                return;
            kernel_gravity<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), mass_.data(), pos_.data(), sim_->min_gravitational_mass(), sim_->softening(), force_gravity_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void compute_springs() override
        {
            bind();
            if (n_ == 0)
                return;
            kernel_springs<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), pos_.data(), spring_offsets_.data(), spring_entries_.data(),
                spring_id1_.data(), spring_id2_.data(), spring_k_.data(), spring_l0_.data(), spring_broken_.data(), force_spring_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void compute_dampers() override
        {
            bind();
            if (n_ == 0)
                return;
            kernel_dampers<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), pos_.data(), vel_.data(), damper_offsets_.data(), damper_entries_.data(),
                damper_id1_.data(), damper_id2_.data(), damper_k_.data(), damper_broken_.data(), force_damper_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void sum_forces() override
        {
            bind();
            if (n_ == 0)
                return;
            kernel_sum_forces<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), mass_.data(), force_gravity_.data(), force_spring_.data(),
                force_damper_.data(), force_total_.data(), acc_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void integrate(double dt) override
        {
            bind();
            if (n_ == 0)
                return;
            kernel_integrate<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), dt, fixed_.data(), acc_.data(), vel_.data(), pos_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void collisions() override
        {
            bind();
            if (n_ <= 1)
                return;

            const int n = static_cast<int>(n_);
            kernel_cell_keys<<<blocks_for(n_), block_size>>>(n, cell_size_, pos_.data(), collision_keys_.data(), collision_index_.data());
            CUDA_CHECK(cudaGetLastError());

            // Stable so equal keys keep ascending mass index.
            cached_allocator allocator{ &sort_scratch_ };
            thrust::stable_sort_by_key(thrust::cuda::par(allocator), thrust::device_pointer_cast(collision_keys_.data()), thrust::device_pointer_cast(collision_keys_.data()) + n,
                thrust::device_pointer_cast(collision_index_.data()));

            kernel_collide<<<blocks_for(n_), block_size>>>(n, cell_size_, pos_.data(), vel_.data(), mass_.data(), radius_.data(), elasticity_.data(),
                collision_keys_.data(), collision_index_.data(), collision_dpos_.data(), collision_dvel_.data());
            CUDA_CHECK(cudaGetLastError());

            kernel_apply_collisions<<<blocks_for(n_), block_size>>>(n, collision_dpos_.data(), collision_dvel_.data(), pos_.data(), vel_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void walls() override
        {
            bind();
            if (n_ == 0)
                return;
            const auto& w = sim_->walls();
            kernel_walls<<<blocks_for(n_), block_size>>>(static_cast<int>(n_), sim_->xmin(), sim_->xmax(), sim_->ymin(), sim_->ymax(),
                w.left, w.right, w.top, w.bottom, radius_.data(), pos_.data(), vel_.data());
            CUDA_CHECK(cudaGetLastError());
        }

        void read_forces(force_generator generator, std::vector<vector2>& out) override
        {
            bind();
            switch (generator)
            {
            case force_generator::gravity: force_gravity_.get(out); break;
            case force_generator::spring:  force_spring_.get(out); break;
            case force_generator::damper:  force_damper_.get(out); break;
            case force_generator::total:   force_total_.get(out); break;
            }
        }

        void synchronize() override
        {
            bind();
            CUDA_CHECK(cudaDeviceSynchronize());
        }

    private:
        // cudaSetDevice is per host thread and the simulation thread is recreated, so bind on every call.
        void bind()
        {
            thread_local int bound_device = -1;
            if (bound_device != device_)
            {
                CUDA_CHECK(cudaSetDevice(device_));
                bound_device = device_;
            }
        }

        template<typename S>
        static void assign_flags(std::vector<S>& out, const std::vector<unsigned char>& flags)
        {
            if (out.size() != flags.size())
            {
                out.clear();
                for (size_t i = 0; i < flags.size(); ++i)
                    out.emplace_back(flags[i] != 0);
            }
            else
            {
                for (size_t i = 0; i < flags.size(); ++i)
                    out[i].broken_ = flags[i] != 0;
            }
        }

        int device_ = 0;
        std::string name_;
        std::unique_ptr<simulation> sim_;

        size_t n_ = 0;
        size_t spring_count_ = 0;
        size_t damper_count_ = 0;
        double max_r_ = 0.0;
        double cell_size_ = 1.0;

        std::vector<unsigned char> fixed_host_;
        std::vector<double> angular_velocity_;
        std::vector<double> angular_position_;

        dev_array<vector2> pos_, vel_, acc_;
        dev_array<vector2> force_gravity_, force_spring_, force_damper_, force_total_;
        dev_array<double> mass_, radius_, elasticity_;
        dev_array<unsigned char> fixed_;

        dev_array<int> spring_id1_, spring_id2_, spring_offsets_, spring_entries_, spring_damper_offsets_, spring_damper_entries_;
        dev_array<double> spring_k_, spring_l0_, spring_min_, spring_max_;
        dev_array<unsigned char> spring_broken_;

        dev_array<int> damper_id1_, damper_id2_, damper_offsets_, damper_entries_;
        dev_array<double> damper_k_;
        dev_array<unsigned char> damper_broken_;

        dev_array<long long> collision_keys_;
        dev_array<int> collision_index_;
        dev_array<vector2> collision_dpos_, collision_dvel_;

        scratch_block sort_scratch_;
        pinned_array<vector2> render_pos_, render_vel_;
        pinned_array<unsigned char> render_spring_broken_;
    };
}

std::unique_ptr<physics_engine> make_cuda_engine(std::string& error)
{
    try
    {
        return std::make_unique<cuda_engine>();
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        return nullptr;
    }
}

std::vector<std::string> cuda_device_names()
{
    std::vector<std::string> names;
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess)
        return names;

    for (int d = 0; d < count; ++d)
    {
        cudaDeviceProp props;
        if (cudaGetDeviceProperties(&props, d) == cudaSuccess)
            names.emplace_back(props.name);
    }
    return names;
}
