// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <variant>
#include <boost/container/static_vector.hpp>
#include <tsl/robin_map.h>
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/specialization.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

template <>
struct std::hash<vk::ShaderModule> {
    std::size_t operator()(const vk::ShaderModule& module) const noexcept {
        return std::hash<size_t>{}(reinterpret_cast<size_t>((VkShaderModule)module));
    }
};

namespace AmdGpu {
class Liverpool;
}

namespace Serialization {
struct Archive;
}

namespace Shader {
struct Info;
}

namespace Vulkan {

class Instance;
class Scheduler;
class ShaderCache;

struct Program {
    struct Module {
        vk::ShaderModule module;
        Shader::StageSpecialization spec;
    };
    static constexpr size_t MaxPermutations = 8;
    using ModuleList = boost::container::small_vector<Module, MaxPermutations>;

    Shader::Info info;
    ModuleList modules{};
    size_t last_used = 0; ///< bbport: permutation of the previous lookup, compared first
    /// bbport: `info` as translated, for draw-preparation workers (they must not read `info`,
    /// whose user data the GPU thread rewrites every draw). Guarded by programs_mutex.
    std::unique_ptr<Shader::Info> info_template;

    Program() = default;
    Program(Shader::HwStage stage, Shader::SwStage l_stage, Shader::ShaderParams params)
        : info{stage, l_stage, params} {}

    void AddPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec) {
        modules.emplace_back(module, std::move(spec));
    }

    void InsertPermut(vk::ShaderModule module, Shader::StageSpecialization&& spec,
                      size_t perm_idx) {
        modules.resize(std::max(modules.size(), perm_idx + 1)); // <-- beware of realloc
        modules[perm_idx] = {module, std::move(spec)};
    }
};

struct DrawIndirectParams {
    u16 vertex_sgpr_offset;
    u32 instance_sgpr_offset;
};

} // namespace Vulkan

namespace AmdGpu {
union Regs;
}

namespace Vulkan {

/// bbport: state of one graphics/compute pipeline selection. The GPU thread owns one
/// (PipelineCache::sel); draw-preparation workers use their own with their register copies.
struct PipelineSelection {
    const AmdGpu::Regs* regs{};
    std::array<Shader::RuntimeInfo, MaxShaderStages> runtime_infos{};
    std::array<const Shader::Info*, MaxShaderStages> infos{};
    std::array<vk::ShaderModule, MaxShaderStages> modules{};
    std::optional<Shader::Gcn::FetchShaderData> fetch_shader{};
    GraphicsPipelineKey graphics_key{};
    bool motion = false;
    DrawIndirectParams draw_indirect_params{};
    struct PrepWorker* worker{}; ///< set: read-only selection for a draw-preparation worker
};

/// bbport: a draw-preparation worker's own program state (see vk_draw_prep.h).
struct PrepWorker {
    struct Stage {
        const Program* program;
        u64 hash;
        Shader::HwStage hw_stage;
        VAddr pgm_base;
        const std::vector<u32>* flat;
    };
    // Node-based: stages keep pointers to these Infos while later stages are inserted.
    std::unordered_map<const Program*, Shader::Info> infos;
    boost::container::static_vector<Stage, MaxShaderStages> stages;
    /// The programs' own Infos of the selection, by logical stage (the worker never reads them;
    /// a pipeline it builds is pointed at them).
    std::array<const Shader::Info*, MaxShaderStages> canonical{};
    bool failed = false;
    bool compile = false;  ///< may translate shaders and queue pipelines (compile ahead)
    Shader::Pools pools{}; ///< the recompiler's pools for this worker's translations
    std::unordered_set<u64> known_pipelines; ///< key hashes seen in graphics_pipelines
};

struct PipelineJob;

struct PreparedDraw;

class PipelineCache {
public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           AmdGpu::Liverpool* liverpool, u32 sparse_page_shift);
    ~PipelineCache();

    void WarmUp();
    void Sync();

    bool LoadComputePipeline(Serialization::Archive& ar);
    bool LoadGraphicsPipeline(Serialization::Archive& ar);
    bool LoadPipelineStage(Serialization::Archive& ar, size_t stage);

    const GraphicsPipeline* GetGraphicsPipeline(const DrawIndirectParams params = {},
                                                const PreparedDraw* prepared = nullptr);

    /// bbport: worker side of draw preparation: selects the pipeline key for `sel.regs` without
    /// creating anything. False when a program or permutation does not exist yet.
    bool PrepareGraphicsPipeline(PipelineSelection& sel);

    /// bbport: GPU-thread side: the pipeline for a prepared draw after checking that registers
    /// and flattened user data match; null to take the regular path.
    const GraphicsPipeline* TryPreparedPipeline(const PreparedDraw& prepared);

    /// bbport: the prepared draw the last GetGraphicsPipeline used, or null (regular path).
    [[nodiscard]] const PreparedDraw* UsedPrepared() const noexcept {
        return used_prepared;
    }

    /// bbport: compile ahead (docs/parallel_gpu.md). Draw-preparation workers translate the
    /// shaders the GPU thread has not met yet (GetProgram) and, once a draw's key is selected,
    /// queue its pipeline here; compile threads build it for the GPU thread to take. New areas
    /// compile on several cores before the GPU thread reaches their draws, instead of one
    /// after another on it.
    static bool CompileAheadEnabled();
    void PrecompileGraphicsPipeline(PipelineSelection& sel);

    const ComputePipeline* GetComputePipeline();

    using Result = std::tuple<const Shader::Info*, vk::ShaderModule,
                              std::optional<Shader::Gcn::FetchShaderData>, u64>;
    Result GetProgram(PipelineSelection& sel, Shader::HwStage stage, Shader::SwStage l_stage,
                      const Shader::ShaderParams& params, Shader::Backend::Bindings& binding);

    std::optional<vk::ShaderModule> ReplaceShader(vk::ShaderModule module,
                                                  std::span<const u32> spv_code);

    static std::string GetShaderName(Shader::HwStage stage, u64 hash,
                                     std::optional<size_t> perm = {});

    auto& GetProfile() const {
        return profile;
    }

private:
    bool RefreshGraphicsKey(PipelineSelection& sel);
    bool RefreshGraphicsStages(PipelineSelection& sel);
    bool RefreshComputeKey();

    void DumpShader(std::span<const u32> code, u64 hash, Shader::HwStage stage, size_t perm_idx,
                    std::string_view ext);
    std::optional<std::vector<u32>> GetShaderPatch(u64 hash, Shader::HwStage stage, size_t perm_idx,
                                                   std::string_view ext);
    /// Translates a shader: its module, and its SPIR-V in `spv` for the cache (stored by
    /// InsertModule once the permutation index is final; `perm_idx` names dumps and patches).
    vk::ShaderModule CompileModule(Shader::Info& info, Shader::RuntimeInfo& runtime_info,
                                   const std::span<const u32>& code, size_t perm_idx,
                                   Shader::Backend::Bindings& binding, Shader::Pools& pools,
                                   std::vector<u32>& spv);
    struct Inserted {
        Program* program;
        size_t perm_idx;
        vk::ShaderModule module;
        std::optional<Shader::Gcn::FetchShaderData> fetch_shader;
        bool created; ///< the new program was inserted (not one another thread added)
    };
    /// bbport: adds a translated module: as a new program (`new_program`, unless another thread
    /// added the program meanwhile) or a permutation. An equal permutation already there wins
    /// and the module is destroyed. Writes the cache entries with the final index.
    Inserted InsertModule(u64 hash, std::unique_ptr<Program> new_program, vk::ShaderModule module,
                          Shader::StageSpecialization&& spec, std::vector<u32>&& spv,
                          const Shader::Info& meta_info);
    Result GetProgramAhead(PipelineSelection& sel, Shader::HwStage stage, Shader::SwStage l_stage,
                           const Shader::ShaderParams& params, Shader::Backend::Bindings& binding,
                           Shader::RuntimeInfo& runtime_info);
    /// A worker's translation of a program (new_program) or of a permutation. Guest memory
    /// faults jump out of it: everything it made lives in its own frame until it returns.
    struct AheadTranslation* TranslateAhead(PrepWorker& worker, Shader::HwStage stage,
                                            Shader::SwStage l_stage,
                                            const Shader::ShaderParams& params,
                                            Shader::RuntimeInfo& runtime_info,
                                            Shader::Backend::Bindings binding, bool new_program,
                                            size_t perm_guess);
    bool TryClaimProgram(u64 hash);
    void ReleaseProgram(u64 hash);
    /// GPU thread: when a worker is translating `hash`, waits for it (bounded); true if it
    /// finished, so the lookup is worth repeating.
    bool WaitForProgram(u64 hash);
    /// GPU thread: a pipeline a compile thread built (or is building: bounded wait), moved into
    /// graphics_pipelines; null if none. A job still queued is withdrawn: the caller builds it.
    const GraphicsPipeline* AdoptPrecompiled(const GraphicsPipelineKey& key);
    void CompileLoop(std::stop_token stop);
    const Shader::RuntimeInfo& BuildRuntimeInfo(PipelineSelection& sel, Shader::HwStage stage,
                                                Shader::SwStage l_stage);

    [[nodiscard]] bool IsPipelineCacheDirty() const {
        return num_new_pipelines > 0;
    }

private:
    const Instance& instance;
    Scheduler& scheduler;
    AmdGpu::Liverpool* liverpool;
    DescriptorHeap desc_heap;
    vk::UniquePipelineCache pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
    Shader::Profile profile{};
    Shader::Pools pools;
    tsl::robin_map<size_t, std::unique_ptr<Program>> program_cache;
    /// bbport: exclusive for program/permutation insertions (the GPU thread and compiling
    /// workers), shared for lookups.
    std::shared_mutex programs_mutex;
    u64 prepared_hits = 0, prepared_misses = 0;
    const PreparedDraw* used_prepared = nullptr;
    tsl::robin_map<ComputePipelineKey, std::unique_ptr<ComputePipeline>> compute_pipelines;
    tsl::robin_map<GraphicsPipelineKey, std::unique_ptr<GraphicsPipeline>> graphics_pipelines;
    /// bbport: graphics_pipelines changes only on the GPU thread, under this exclusively;
    /// workers look into it shared. The GPU thread's own lookups need no lock.
    std::shared_mutex pipelines_mutex;
    /// bbport: compile ahead. Programs being translated; pipelines queued or being built (and
    /// those being built), the jobs, and pipelines built that the GPU thread has not taken yet.
    std::mutex compile_mutex;
    std::condition_variable compile_cv;      ///< a translation or a pipeline finished
    std::condition_variable_any jobs_cv;     ///< a pipeline job was queued
    std::unordered_set<u64> programs_compiling;
    std::unordered_set<GraphicsPipelineKey> pipelines_pending;
    std::unordered_set<GraphicsPipelineKey> pipelines_running;
    std::deque<std::unique_ptr<PipelineJob>> pipeline_jobs;
    tsl::robin_map<GraphicsPipelineKey, std::unique_ptr<GraphicsPipeline>> pipelines_ready;
    PipelineSelection sel{}; ///< GPU thread selection state
    ComputePipelineKey compute_key{};
    u32 num_new_pipelines{}; // new pipelines added to the cache since the game start

    // Only if Config::collectShadersForDebug()
    tsl::robin_map<vk::ShaderModule,
                   std::vector<std::variant<GraphicsPipelineKey, ComputePipelineKey>>>
        module_related_pipelines;
    /// bbport: last, so the threads stop before anything they use goes.
    std::vector<std::jthread> compile_threads;
};

} // namespace Vulkan
