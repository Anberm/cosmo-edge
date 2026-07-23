#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_CPU_BACKEND

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "nn/core/shared_resource.h"
#include "nn/device/cpu/cpu_dino_encode_node.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/op.h"

namespace fs = std::filesystem;

namespace {

class TemporaryVocab {
public:
    TemporaryVocab() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_             = fs::temp_directory_path() / ("cosmo_dino_vocab_" + std::to_string(suffix));
        std::ofstream output(path_);
        REQUIRE(output.good());
        output << "[PAD]\n[UNK]\n[CLS]\n[SEP]\n.\n?\nred\nfire\nex\n##ting\n##uisher\ndog\n";
        REQUIRE(output.good());
    }

    ~TemporaryVocab() {
        std::error_code error;
        fs::remove(path_, error);
    }

    const fs::path& Path() const {
        return path_;
    }

private:
    fs::path path_;
};

std::shared_ptr<cosmo::nn::Blob> MakeBlob(cosmo::nn::DataType type, const cosmo::nn::DimsVector& dims) {
    cosmo::nn::BlobDesc desc;
    desc.data_type   = type;
    desc.device_type = cosmo::nn::DEVICE_NAIVE;
    desc.dims        = dims;
    return std::make_shared<cosmo::nn::Blob>(desc, true);
}

}  // namespace

TEST_CASE("GroundingDINO CPU tokenizer handles phrases and WordPiece", "[dino][tokenizer]") {
    TemporaryVocab vocab;
    cosmo::nn::CpuTokenizer tokenizer;
    REQUIRE(tokenizer.LoadVocab(vocab.Path().string()));

    std::vector<int64_t> ids;
    tokenizer.EncodeText("Red fire extinguisher. dog?", ids);
    REQUIRE(ids == std::vector<int64_t>{2, 6, 7, 8, 9, 10, 4, 11, 5, 3});

    REQUIRE(tokenizer.DecodeIds({6, 7, 8, 9, 10}) == "red fire extinguisher");
    tokenizer.EncodeText("missing", ids);
    REQUIRE(ids == std::vector<int64_t>{2, 1, 3});
}

TEST_CASE("GroundingDINO CPU preprocessing honors RGB model order", "[dino][preprocess]") {
    TemporaryVocab vocab;
    cosmo::nn::DinoEncoder operation;
    operation.dst_width  = 1;
    operation.dst_height = 1;
    operation.is_bgr     = false;
    operation.mean       = {0.0f, 0.0f, 0.0f};
    operation.std        = {1.0f, 1.0f, 1.0f};

    cosmo::nn::CpuDinoEncodeNode node;
    node.SetMaxBatch(1);
    node.LoadParam(&operation);
    auto status = node.InferTopShapes();
    REQUIRE(status == cosmo::nn::COSMO_NN_OK);

    cosmo::nn::SharedResource resource;
    resource.tokenizer_path = vocab.Path().string();
    node.SetSharedResource(&resource);

    cosmo::nn::BlobDesc image_desc;
    image_desc.data_type    = cosmo::nn::DATA_TYPE_UINT8;
    image_desc.data_format  = cosmo::nn::DATA_FORMAT_NHWC;
    image_desc.image_format = cosmo::nn::IMAGE_BGR;
    image_desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    image_desc.dims         = {1, 1, 1, 3};
    auto image              = std::make_shared<cosmo::nn::Blob>(image_desc, true);
    auto* pixel             = static_cast<std::uint8_t*>(image->GetHandle().base);
    pixel[0]                = 10;
    pixel[1]                = 20;
    pixel[2]                = 30;

    cosmo::nn::BlobDesc prompt_desc;
    prompt_desc.data_type   = cosmo::nn::DATA_TYPE_UINT8;
    prompt_desc.device_type = cosmo::nn::DEVICE_NAIVE;
    prompt_desc.dims        = {1, 3};
    auto prompt             = std::make_shared<cosmo::nn::Blob>(prompt_desc, true);
    std::memcpy(prompt->GetHandle().base, "red", 3);

    const auto shapes = node.GetTopBlobShapes();
    const auto types  = node.GetTopBlobDataTypes();
    std::vector<std::shared_ptr<cosmo::nn::Blob>> tops;
    for (std::size_t index = 0; index < shapes.size(); ++index)
        tops.push_back(MakeBlob(types[index], shapes[index]));
    std::vector<std::shared_ptr<cosmo::nn::Blob>> images{image};
    std::vector<std::shared_ptr<cosmo::nn::Blob>> prompts{prompt};
    status = node.Forward(images, prompts, tops);
    REQUIRE(status == cosmo::nn::COSMO_NN_OK);

    const auto* samples = static_cast<const float*>(tops[0]->GetHandle().base);
    REQUIRE(samples[0] == Catch::Approx(30.0f / 255.0f));
    REQUIRE(samples[1] == Catch::Approx(20.0f / 255.0f));
    REQUIRE(samples[2] == Catch::Approx(10.0f / 255.0f));
}

TEST_CASE("GroundingDINO parsing composes phrases and respects per-call thresholds", "[dino][postprocess]") {
    TemporaryVocab vocab;
    cosmo::nn::CpuTokenizer tokenizer;
    REQUIRE(tokenizer.LoadVocab(vocab.Path().string()));

    constexpr int batch   = 2;
    constexpr int queries = 2;
    constexpr int tokens  = 12;
    auto logits           = MakeBlob(cosmo::nn::DATA_TYPE_FLOAT, {batch, queries, tokens});
    auto boxes            = MakeBlob(cosmo::nn::DATA_TYPE_FLOAT, {batch, queries, 4});
    auto* logits_data     = static_cast<float*>(logits->GetHandle().base);
    auto* boxes_data      = static_cast<float*>(boxes->GetHandle().base);
    std::fill(logits_data, logits_data + batch * queries * tokens, 0.01f);
    std::fill(boxes_data, boxes_data + batch * queries * 4, 0.0f);

    std::vector<int64_t> encoded;
    tokenizer.EncodeText("red fire extinguisher.", encoded);
    std::vector<int> padded(encoded.begin(), encoded.end());
    padded.resize(tokens, 0);

    for (int token : {1, 2, 3, 4, 5})
        logits_data[token] = 0.9f;
    boxes_data[0] = 0.5f;
    boxes_data[1] = 0.5f;
    boxes_data[2] = 0.5f;
    boxes_data[3] = 0.5f;

    const int second_batch_offset        = queries * tokens;
    logits_data[second_batch_offset + 1] = 0.8f;
    boxes_data[queries * 4 + 0]          = 0.25f;
    boxes_data[queries * 4 + 1]          = 0.25f;
    boxes_data[queries * 4 + 2]          = 0.25f;
    boxes_data[queries * 4 + 3]          = 0.25f;

    std::vector<std::shared_ptr<cosmo::nn::Blob>> outputs{logits, boxes};
    std::vector<cosmo::nn::Size> image_sizes{{640, 480}, {320, 240}};
    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> detections;
    auto status = cosmo::nn::NetUtils::ParseDINOOutput(outputs, &tokenizer, image_sizes, padded, 0.5f, 0.5f,
                                                       detections);
    REQUIRE(status == cosmo::nn::COSMO_NN_OK);
    REQUIRE(detections.size() == 2);
    REQUIRE(detections[0].size() == 1);
    REQUIRE(detections[0][0].infos[0].class_name == "red fire extinguisher");
    REQUIRE(detections[1].size() == 1);
    REQUIRE(detections[1][0].infos[0].class_name == "red");

    status = cosmo::nn::NetUtils::ParseDINOOutput(outputs, &tokenizer, image_sizes, padded, 1.1f, 0.5f,
                                                  detections);
    REQUIRE(status != cosmo::nn::COSMO_NN_OK);
}

#endif  // COSMO_NN_USE_CPU_BACKEND
