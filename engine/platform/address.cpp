#include "platform/net.h"

namespace scraps {
namespace {

bool parse_decimal(std::string_view text, std::uint32_t max_value, std::uint32_t& out) {
  if (text.empty() || text.size() > 5) return false;
  std::uint32_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10u + static_cast<std::uint32_t>(c - '0');
  }
  if (value > max_value) return false;
  out = value;
  return true;
}

}  // namespace

bool parse_address(std::string_view text, Address& out) {
  const std::size_t colon = text.rfind(':');
  if (colon == std::string_view::npos) return false;
  const std::string_view host = text.substr(0, colon);
  const std::string_view port_text = text.substr(colon + 1);

  std::uint32_t port = 0;
  if (!parse_decimal(port_text, 65535u, port)) return false;

  std::uint32_t ip = 0;
  if (host == "localhost") {
    ip = kLocalhostIp;
  } else {
    int parts = 0;
    std::size_t start = 0;
    while (true) {
      const std::size_t dot = host.find('.', start);
      const std::string_view part =
          host.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
      std::uint32_t octet = 0;
      if (parts >= 4 || !parse_decimal(part, 255u, octet)) return false;
      ip = (ip << 8) | octet;
      ++parts;
      if (dot == std::string_view::npos) break;
      start = dot + 1;
    }
    if (parts != 4) return false;
  }

  out = Address(ip, static_cast<std::uint16_t>(port));
  return true;
}

std::string to_string(const Address& address) {
  std::string text;
  for (int shift = 24; shift >= 0; shift -= 8) {
    text += std::to_string((address.ip >> shift) & 0xFFu);
    if (shift > 0) text += '.';
  }
  text += ':';
  text += std::to_string(address.port);
  return text;
}

}  // namespace scraps
