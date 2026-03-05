// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/output_backend.hpp"

namespace deckboy::platform {
namespace {

class DefaultOutputBackendCatalog final : public OutputBackendCatalog {
 public:
  std::vector<OutputBackendInfo> list() const override {
    std::vector<OutputBackendInfo> out;
    out.push_back({"window", "SDL Window Output", true, ""});
    out.push_back({"stream", "FFmpeg Stream Output", true, ""});
#if defined(PLAYBOY_HAS_NDI_SDK)
    out.push_back({"ndi", "NDI Output", true, ""});
#else
    out.push_back({"ndi", "NDI Output", false, "NDI SDK not built"});
#endif
#if defined(PLAYBOY_HAS_DECKLINK)
    out.push_back({"decklink", "DeckLink Output", true, ""});
#else
    out.push_back({"decklink", "DeckLink Output", false, "DeckLink SDK feature gate disabled"});
#endif
    return out;
  }
};

}  // namespace

std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog() {
  return std::make_unique<DefaultOutputBackendCatalog>();
}

}  // namespace deckboy::platform
