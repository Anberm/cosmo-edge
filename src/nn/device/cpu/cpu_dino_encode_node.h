#pragma once

#ifdef COSMO_NN_USE_HOST_BACKEND

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nn/node/node.h"

namespace cosmo::nn {

// BERT-compatible uncased WordPiece tokenizer used by GroundingDINO host
// preprocessing.  This intentionally avoids the Rust tokenizers dependency so
// Deployment images can tokenize prompts without a second runtime.
class CpuTokenizer {
public:
    bool LoadVocab(const std::string& vocab_path);
    void EncodeText(const std::string& text, std::vector<int64_t>& ids);
    std::string DecodeIds(const std::vector<int>& ids) const;
    int64_t ClsId() const {
        return cls_id_;
    }
    int64_t SepId() const {
        return sep_id_;
    }
    int64_t PadId() const {
        return pad_id_;
    }
    std::map<int64_t, std::string> idx2token;
    std::map<std::string, int64_t> token2idx;

private:
    std::vector<std::string> BasicTokenize(const std::string& text) const;
    void WordPieceTokenize(const std::string& token, std::vector<int64_t>& ids) const;

    int64_t unk_id_ = -1;
    int64_t cls_id_ = -1;
    int64_t sep_id_ = -1;
    int64_t pad_id_ = -1;
};

/**
 * @brief CPU DINO encode node.
 *
 * Performs DINO ViT preprocessing: resize + normalize + tokenize text prompt.
 * Outputs include the preprocessed image tensor (NCHW float32) and
 * tokenized text tensor.
 */
class CpuDinoEncodeNode : public Node {
public:
    CpuDinoEncodeNode();
    ~CpuDinoEncodeNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    size_t GetTopCount() override;
    size_t GetBottomCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bases, std::vector<std::shared_ptr<Blob>>& prompts,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    void CreateTokenizer(std::string path);
    std::vector<int64_t> Encode(const std::string& text);
    std::string GetPromptStr(std::shared_ptr<Blob>& prompt);
    std::string Strip(const std::string& str);
    bool Endswith(const std::string& s, const std::string& suffix);

    static constexpr int kTokenLength = 256;

    std::unique_ptr<CpuTokenizer> tokenizer;
    std::vector<std::string> special_tokens = {"[CLS]", "[SEP]", ".", "?"};
    std::vector<int64_t> special_token_ids;

    int dst_width  = 0;
    int dst_height = 0;
    std::vector<float> mean;
    std::vector<float> scale;
    bool is_bgr = true;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_HOST_BACKEND
