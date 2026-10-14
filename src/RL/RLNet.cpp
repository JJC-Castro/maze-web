#include "RLNet.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rl
{
    namespace
    {
        std::string Magic() { return std::string("MZRL"); }

        // Sanity bound for a corrupted header; the real trunk is 3-4 layers.
        constexpr int kMaxHidden = 8;
        constexpr int kMaxWidth = 8192;
    }

    int RLNet::HiddenAt(int i) const
    {
        if (i < 0 || i >= (int)layers_.size()) return 0;
        return layers_[i].out;
    }

    std::string RLNet::Shape() const
    {
        std::string s = std::to_string(obsDim_);
        for (const Layer& l : layers_) s += "-" + std::to_string(l.out);
        s += "->" + std::to_string(nActions_);
        return s;
    }

    void RLNet::Clear()
    {
        ready_ = false;
        version_ = 0;
        obsDim_ = nActions_ = lastHidden_ = 0;
        distEnabled_ = true;
        path_.clear();
        layers_.clear();
        aw_.clear(); ab_.clear(); cw_.clear(); cb_.clear();
        mean_.clear(); std_.clear();
        bufIn_.clear(); bufActs_.clear();
    }

    bool RLNet::Load(const std::string& path, std::string* errOut)
    {
        Clear();

        std::FILE* f = nullptr;
        const auto fail = [&](const char* msg) {
            if (errOut) *errOut = std::string(msg) + " (" + path + ")";
            if (f != nullptr) { std::fclose(f); f = nullptr; }
            Clear();
            return false;
        };
        const auto readExact = [](std::FILE* fp, void* dst, size_t bytes) {
            return std::fread(dst, 1, bytes, fp) == bytes;
        };
        const auto readFloats = [&](std::FILE* fp, std::vector<float>& dst, size_t count) {
            dst.assign(count, 0.0f);
            if (count == 0) return true;
            return readExact(fp, dst.data(), count * sizeof(float));
        };

#ifdef MAZE_WEB
        f = std::fopen(path.c_str(), "rb");
#else
        if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#endif
        if (f == nullptr)
            return fail("cannot open weight file");

        char magic[4] = { 0 };
        if (!readExact(f, magic, 4) || std::memcmp(magic, Magic().data(), 4) != 0)
            return fail("bad magic (not a MZRL weight file)");

        // v2 header: version, obs_dim, n_actions, n_hidden, flags
        unsigned int hdr[5] = { 0 };
        if (!readExact(f, hdr, sizeof(hdr)))
            return fail("truncated header");

        version_ = (int)hdr[0];
        obsDim_ = (int)hdr[1];
        nActions_ = (int)hdr[2];
        const int nHidden = (int)hdr[3];
        distEnabled_ = (hdr[4] & 1u) != 0;

        if (version_ != kWeightFormatVersion)
            return fail("unsupported weight format version");
        if (nHidden <= 0 || nHidden > kMaxHidden)
            return fail("bad hidden layer count");
        if (obsDim_ != kObsDim || nActions_ != kNActions)
            return fail("weight shape does not match RLSpec");

        std::vector<unsigned int> widths((size_t)nHidden, 0);
        if (!readExact(f, widths.data(), widths.size() * sizeof(unsigned int)))
            return fail("truncated hidden widths");

        layers_.assign((size_t)nHidden, Layer());
        int inDim = obsDim_;
        for (int i = 0; i < nHidden; ++i)
        {
            const int out = (int)widths[(size_t)i];
            if (out <= 0 || out > kMaxWidth)
                return fail("bad hidden width");
            Layer& l = layers_[(size_t)i];
            l.in = inDim;
            l.out = out;
            if (!readFloats(f, l.w, (size_t)inDim * (size_t)out) ||
                !readFloats(f, l.b, (size_t)out))
                return fail("truncated trunk weights");
            inDim = out;
        }
        lastHidden_ = inDim;

        const bool ok =
            readFloats(f, aw_, (size_t)lastHidden_ * (size_t)nActions_) &&
            readFloats(f, ab_, (size_t)nActions_) &&
            readFloats(f, cw_, (size_t)lastHidden_) &&
            readFloats(f, cb_, 1) &&
            readFloats(f, mean_, (size_t)obsDim_) &&
            readFloats(f, std_, (size_t)obsDim_);
        if (!ok) return fail("truncated weight payload");

        std::fclose(f);
        f = nullptr;

        bufIn_.assign((size_t)obsDim_, 0.0f);
        bufActs_.assign((size_t)nHidden, std::vector<float>());
        for (int i = 0; i < nHidden; ++i)
            bufActs_[(size_t)i].assign((size_t)layers_[(size_t)i].out, 0.0f);

        path_ = path;
        ready_ = true;
        return true;
    }

    void RLNet::BuildInput(const float* obs, float* out) const
    {
        for (int j = 0; j < obsDim_; ++j)
        {
            const float s = (std_[(size_t)j] > 1e-9f) ? std_[(size_t)j] : 1.0f;
            out[j] = (obs[j] - mean_[(size_t)j]) / s;
        }
        if (!distEnabled_)
            for (int j = kDistBegin; j < kDistEnd && j < obsDim_; ++j)
                out[j] = 0.0f;
    }

    void RLNet::Forward(const float* obs, float* logits, float* value) const
    {
        if (!ready_)
        {
            for (int a = 0; a < kNActions; ++a) logits[a] = 0.0f;
            if (value) *value = 0.0f;
            return;
        }

        BuildInput(obs, bufIn_.data());

        const float* act = bufIn_.data();
        for (size_t l = 0; l < layers_.size(); ++l)
        {
            const Layer& ly = layers_[l];
            float* out = bufActs_[l].data();
            for (int i = 0; i < ly.out; ++i)
            {
                const float* row = &ly.w[(size_t)i * (size_t)ly.in];
                float s = ly.b[(size_t)i];
                for (int j = 0; j < ly.in; ++j) s += row[j] * act[j];
                out[i] = std::tanh(s);
            }
            act = out;
        }

        for (int a = 0; a < nActions_; ++a)
        {
            const float* row = &aw_[(size_t)a * (size_t)lastHidden_];
            float s = ab_[(size_t)a];
            for (int j = 0; j < lastHidden_; ++j) s += row[j] * act[j];
            logits[a] = s;
        }

        if (value)
        {
            float s = cb_[0];
            for (int j = 0; j < lastHidden_; ++j) s += cw_[(size_t)j] * act[j];
            *value = s;
        }
    }

    int RLNet::Argmax(const float* obs, float* outValue, float* outLogits) const
    {
        float logits[kNActions] = { 0.0f };
        float v = 0.0f;
        Forward(obs, logits, &v);
        int best = 0;
        for (int a = 1; a < nActions_; ++a)
            if (logits[a] > logits[best]) best = a;
        if (outLogits)
            for (int a = 0; a < nActions_; ++a) outLogits[a] = logits[a];
        if (outValue) *outValue = v;
        return best;
    }

    void RLNet::Probabilities(const float* obs, float* probs) const
    {
        float logits[kNActions] = { 0.0f };
        Forward(obs, logits, nullptr);
        float mx = logits[0];
        for (int a = 1; a < nActions_; ++a) mx = (logits[a] > mx) ? logits[a] : mx;
        float sum = 0.0f;
        for (int a = 0; a < nActions_; ++a)
        {
            probs[a] = std::exp(logits[a] - mx);
            sum += probs[a];
        }
        if (sum <= 0.0f) sum = 1.0f;
        for (int a = 0; a < nActions_; ++a) probs[a] /= sum;
    }
}
