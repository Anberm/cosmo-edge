#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::nn {

std::string Sha256Hex(const void* data, std::size_t size);
std::string Sha256Hex(const std::vector<std::uint8_t>& data);

}  // namespace cosmo::nn
