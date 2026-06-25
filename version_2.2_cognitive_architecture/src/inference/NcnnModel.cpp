#include "inference/NcnnModel.h"

#include <stdexcept>

NcnnModel::NcnnModel(
    const std::string& param_path,
    const std::string& bin_path,
    int threads,
    bool use_vulkan,
    bool use_packing
) {
    net_.opt.num_threads = threads;
    net_.opt.use_vulkan_compute = use_vulkan;
    net_.opt.use_packing_layout = use_packing;
    net_.opt.lightmode = true;

    if (net_.load_param(param_path.c_str()) != 0) {
        throw std::runtime_error("Failed to load NCNN param: " + param_path);
    }

    if (net_.load_model(bin_path.c_str()) != 0) {
        throw std::runtime_error("Failed to load NCNN bin: " + bin_path);
    }
}

ncnn::Mat NcnnModel::run(
    const ncnn::Mat& input,
    const std::string& input_name,
    const std::string& output_name
) const {
    ncnn::Extractor extractor = net_.create_extractor();

    if (extractor.input(input_name.c_str(), input) != 0) {
        throw std::runtime_error("Failed to set NCNN input blob: " + input_name);
    }

    ncnn::Mat output;
    if (extractor.extract(output_name.c_str(), output) != 0) {
        throw std::runtime_error("Failed to extract NCNN output blob: " + output_name);
    }

    return output;
}
