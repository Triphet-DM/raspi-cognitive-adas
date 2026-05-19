#include "inference/OnnxModel.h"

OnnxModel::OnnxModel(const std::string& model_path, int threads)
    : env_(ORT_LOGGING_LEVEL_WARNING, "pi5_phase1"),
      session_options_(),
      session_(nullptr) {
    session_options_.SetIntraOpNumThreads(threads);
    session_options_.SetInterOpNumThreads(1);
    session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session_ = Ort::Session(env_, model_path.c_str(), session_options_);

    auto input_name_alloc = session_.GetInputNameAllocated(0, allocator_);
    auto output_name_alloc = session_.GetOutputNameAllocated(0, allocator_);

    input_name_ = input_name_alloc.get();
    output_name_ = output_name_alloc.get();
}

std::vector<Ort::Value> OnnxModel::run(float* input_data, size_t input_count, const std::vector<int64_t>& input_shape) {
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        input_data,
        input_count,
        input_shape.data(),
        input_shape.size()
    );

    const char* input_names[] = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};

    return session_.Run(
        Ort::RunOptions{nullptr},
        input_names,
        &input_tensor,
        1,
        output_names,
        1
    );
}
