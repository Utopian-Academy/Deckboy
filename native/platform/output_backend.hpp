// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace deckboy::platform {

struct OutputBackendInfo {
  std::string id;
  std::string displayName;
  bool supported = false;
  std::string reasonUnavailable;
};

class OutputBackendCatalog {
 public:
  virtual ~OutputBackendCatalog() = default;
  virtual std::vector<OutputBackendInfo> list() const = 0;
};

std::unique_ptr<OutputBackendCatalog> createOutputBackendCatalog();

}  // namespace deckboy::platform
