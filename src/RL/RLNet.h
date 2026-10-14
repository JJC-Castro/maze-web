#pragma once
// ---------------------------------------------------------------------------
// RLNet.h : pure C++ inference for the PPO actor-critic MLP (any depth).
//
// Loads the bare weight file exported by rl/rl/model.py::export_binary (layout
// documented in rl/docs/05_WEIGHTS_FORMAT.md). No PyTorch / no third party deps
// -- the game only ever needs a forward pass:
//
//   x -> Linear(obs, h1) -> tanh -> Linear(h1, h2) -> tanh -> ... -> h_last
//     -> actor  : Linear(h_last, n_actions)   (logits)
//     -> critic : Linear(h_last, 1)           (state value)
//
// A v2 file carries the hidden widths in its header, so the depth is whatever
// the trainer used (currently 256-256-256). Nothing here is hard-coded.
//
// Input normalisation (obs - mean) / std and the distance-heatmap ablation
// mask are applied here so the game and the trainer always see the exact same
// vector.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "RLSpec.h"

namespace rl
{
    class RLNet
    {
    public:
        bool Load(const std::string& path, std::string* errOut = nullptr);
        void Clear();

        bool Ready() const { return ready_; }
        int  ObsDim() const { return obsDim_; }
        int  NActions() const { return nActions_; }
        int  NLayers() const { return (int)layers_.size(); }
        int  HiddenAt(int i) const;                 // 0 outside range
        int  LastHidden() const { return lastHidden_; }
        // legacy two-layer accessors (kept for older HUD code)
        int  H1() const { return HiddenAt(0); }
        int  H2() const { return HiddenAt(1); }
        bool DistEnabled() const { return distEnabled_; }
        int  Version() const { return version_; }
        const std::string& Path() const { return path_; }
        std::string Shape() const;                  // e.g. "133-256-256-256->25"

        // normalized + masked copy of obs, in the exact layout the net expects
        void BuildInput(const float* obs, float* out) const;

        // raw obs in, logits (nActions) and value out
        void Forward(const float* obs, float* logits, float* value) const;

        // argmax over the actor head; returns the action index
        int Argmax(const float* obs, float* outValue = nullptr,
                   float* outLogits = nullptr) const;

        // softmax probabilities (diagnostics / HUD)
        void Probabilities(const float* obs, float* probs) const;

    private:
        struct Layer
        {
            int in = 0;
            int out = 0;
            std::vector<float> w;      // out x in, row major
            std::vector<float> b;      // out
        };

        bool ready_ = false;
        int version_ = 0;
        int obsDim_ = 0;
        int nActions_ = 0;
        int lastHidden_ = 0;
        bool distEnabled_ = true;
        std::string path_;

        std::vector<Layer> layers_;              // trunk: Linear / tanh alternating
        std::vector<float> aw_, ab_, cw_, cb_;   // actor / critic heads
        std::vector<float> mean_, std_;
        mutable std::vector<float> bufIn_;
        mutable std::vector<std::vector<float>> bufActs_;
    };
}
