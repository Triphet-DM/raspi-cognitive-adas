// src/inference/OnnxModel.h
#pragma once

#include <onnxruntime_cxx_api.h>

#include <string>
#include <vector>

class OnnxModel {
public:
    OnnxModel(const std::string& model_path, int threads);

    std::vector<Ort::Value> run(
        std::vector<float>& input,
        const std::vector<int64_t>& input_shape
    );

    const std::string& input_name() const {
        return input_name_;
    }

    const std::string& output_name() const {
        return output_name_;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    Ort::Session session_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::string input_name_;
    std::string output_name_;
};
