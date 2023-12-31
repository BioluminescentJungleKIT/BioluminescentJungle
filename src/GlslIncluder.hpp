#pragma once

#include <cstring>
#include <shaderc/shaderc.h>
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <stdexcept>

static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    auto fileSize = file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

static bool fileExists(const std::string &filename) {
    std::ifstream file(filename);
    return file.good();
}

class GlslIncluder : public shaderc::CompileOptions::IncluderInterface {
  public:
    // Handles shaderc_include_resolver_fn callbacks.
    virtual shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type,
        const char* requesting_source, size_t include_depth)
    {
        if (type != shaderc_include_type::shaderc_include_type_relative) {
            return errorResult("We support relative includes only!"
                " Make sure that the file exists in the shaders/ subdirectory.");
        }

        std::string filePath = std::string("shaders/") + requested_source;
        if (!fileExists(filePath)) {
            return errorResult("Source file not found: " + filePath);
        }

        auto result = new CustomData;
        result->file = filePath;
        result->content = readFile(filePath);
        return customDataToShaderCResult(result);
    }

    virtual void ReleaseInclude(shaderc_include_result* data)
    {
        auto result = (CustomData*)data->user_data;

        delete result;
        delete data;
    }

  private:
    struct CustomData {
        std::vector<char> content;
        std::string file;
    };

    shaderc_include_result *customDataToShaderCResult(CustomData *result)
    {
        shaderc_include_result *shaderc_result = new shaderc_include_result;
        shaderc_result->source_name = result->file.data();
        shaderc_result->source_name_length = result->file.length();
        shaderc_result->content = result->content.data();
        shaderc_result->content_length = result->content.size();
        shaderc_result->user_data = result;

        return shaderc_result;
    }

    shaderc_include_result* errorResult(std::string message) {
        CustomData *data = new CustomData;
        data->content.insert(data->content.begin(), message.begin(), message.end());
        return customDataToShaderCResult(data);
    };
};
