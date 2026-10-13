#pragma once

#include <hyprutils/os/FileDescriptor.hpp>
#include <cstdint>
#include <optional>
#include <unordered_map>

// clipboard content requested from a session, each waiting under a serial for the session to write it
class CClipboardTransfers {
  public:
    uint32_t                                      add(Hyprutils::OS::CFileDescriptor&& fd);
    std::optional<Hyprutils::OS::CFileDescriptor> take(uint32_t serial);
    size_t                                        size() const;

  private:
    uint32_t                                                     m_nextSerial = 1;
    std::unordered_map<uint32_t, Hyprutils::OS::CFileDescriptor> m_transfers;
};
