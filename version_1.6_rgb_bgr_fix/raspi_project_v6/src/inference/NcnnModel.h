#pragma once

#include "net.h"

#include <string>

class NcnnModel {
public:
    NcnnModel(
        const std::string& param_path,
        const std::string& bin_path,
        int threads,
        bool use_vulkan,
        bool use_packing
    );

    ncnn::Mat run(
        const ncnn::Mat& input,
        const std::string& input_name,
        const std::string& output_name
    ) const;

private:
    ncnn::Net net_;
};
