#include "ClipboardTransfers.hpp"

uint32_t CClipboardTransfers::add(Hyprutils::OS::CFileDescriptor&& fd) {
    // 0 isn't handed out, so a serial is never mistaken for an unset one
    if (m_nextSerial == 0)
        m_nextSerial = 1;

    const auto SERIAL = m_nextSerial++;
    m_transfers.insert_or_assign(SERIAL, std::move(fd));
    return SERIAL;
}

std::optional<Hyprutils::OS::CFileDescriptor> CClipboardTransfers::take(uint32_t serial) {
    const auto IT = m_transfers.find(serial);
    if (IT == m_transfers.end())
        return std::nullopt;

    auto fd = std::move(IT->second);
    m_transfers.erase(IT);
    return fd;
}

size_t CClipboardTransfers::size() const {
    return m_transfers.size();
}
