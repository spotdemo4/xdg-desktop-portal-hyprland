#include "Clipboard.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "ext-data-control-v1.hpp"
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <unistd.h>

[[noreturn]] static void fail(const std::string& error, const std::string& message) {
    Debug::log(ERR, "[clipboard] {}", message);
    throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.portal.Error." + error}, message};
}

// for objects dropped from within their own events, which aren't safe to destroy until the next loop
template <typename T>
static void destroyLater(SP<T>&& object) {
    g_pPortalManager->addTimer({0, [object = std::move(object)]() mutable { object.reset(); }});
}

CClipboardPortal::CClipboardPortal(SP<CCExtDataControlManagerV1> mgr) {
    Debug::log(LOG, "[clipboard] initializing clipboard portal");

    m_sState.manager = mgr;
    m_sState.device  = makeShared<CCExtDataControlDeviceV1>(mgr->sendGetDataDevice(g_pPortalManager->m_sWaylandConnection.seat->resource()));
    m_sState.device->setDataOffer([this](CCExtDataControlDeviceV1*, wl_proxy* offer) { onDataOffer(offer); });
    m_sState.device->setSelection([this](CCExtDataControlDeviceV1*, wl_proxy* offer) { onSelection(offer); });
    m_sState.device->setPrimarySelection([this](CCExtDataControlDeviceV1*, wl_proxy* offer) {
        // only the regular selection is shared
        if (m_sState.pendingOffer && m_sState.pendingOffer->offer->resource() == offer)
            m_sState.pendingOffer.reset();
    });
    m_sState.device->setFinished([this](CCExtDataControlDeviceV1*) {
        Debug::log(ERR, "[clipboard] The compositor finished the data control device, the clipboard is unavailable");
        m_sState.pendingOffer.reset();
        m_sState.selection.reset();
        destroyLater(std::move(m_sState.device));
    });

    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);
    m_pObject
        ->addVTable(
            sdbus::registerMethod("RequestClipboard").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m) { onRequestClipboard(o, m); }),
            sdbus::registerMethod("SetSelection").implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m) { onSetSelection(o, m); }),
            sdbus::registerMethod("SelectionWrite").implementedAs([this](sdbus::ObjectPath o, uint32_t u) { return onSelectionWrite(o, u); }),
            sdbus::registerMethod("SelectionWriteDone").implementedAs([this](sdbus::ObjectPath o, uint32_t u, bool b) { onSelectionWriteDone(o, u, b); }),
            sdbus::registerMethod("SelectionRead").implementedAs([this](sdbus::ObjectPath o, std::string s) { return onSelectionRead(o, s); }),
            sdbus::registerSignal("SelectionOwnerChanged").withParameters<sdbus::ObjectPath, std::unordered_map<std::string, sdbus::Variant>>(),
            sdbus::registerSignal("SelectionTransfer").withParameters<sdbus::ObjectPath, std::string, uint32_t>(),
            sdbus::registerProperty("version").withGetter([] { return uint32_t{1}; }))
        .forInterface(INTERFACE_NAME);
}

void CClipboardPortal::onRequestClipboard(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[clipboard] RequestClipboard for {}", sessionHandle.c_str());

    // only remote desktop sessions take the clipboard, which they get as they start
    const auto& REMOTEDESKTOP = g_pPortalManager->m_sPortals.remoteDesktop;
    if (!REMOTEDESKTOP || !REMOTEDESKTOP->m_mSessions.contains(sessionHandle))
        fail("NotFound", std::format("No remote desktop session {}", sessionHandle.c_str()));

    if (!m_mSessions.contains(sessionHandle))
        m_mSessions.emplace(sessionHandle, makeShared<SSession>(SSession{.sessionHandle = sessionHandle}));
}

void CClipboardPortal::onSetSelection(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = getEnabledSession(sessionHandle);
    if (!PSESSION)
        fail("NotAllowed", std::format("Session {} has no clipboard access", sessionHandle.c_str()));

    if (!m_sState.device)
        fail("Failed", "The clipboard is unavailable");

    std::vector<std::string> mimeTypes;
    if (const auto IT = opts.find("mime_types"); IT != opts.end() && IT->second.containsValueOfType<std::vector<std::string>>())
        mimeTypes = IT->second.get<std::vector<std::string>>();

    Debug::log(LOG, "[clipboard] Session {} sets the selection with {} mime types", sessionHandle.c_str(), mimeTypes.size());

    if (mimeTypes.empty()) {
        // offering nothing takes the session's content off the clipboard
        if (PSESSION->source) {
            m_sState.device->sendSetSelection(nullptr);
            PSESSION->source.reset();
        }
        return;
    }

    const auto SOURCE = makeShared<CCExtDataControlSourceV1>(m_sState.manager->sendCreateDataSource());
    for (const auto& mimeType : mimeTypes) {
        SOURCE->sendOffer(mimeType.c_str());
    }

    const WP<SSession> WEAKSESSION = PSESSION;
    SOURCE->setSend([this, WEAKSESSION](CCExtDataControlSourceV1*, const char* mimeType, int32_t fd) {
        Hyprutils::OS::CFileDescriptor transferFd{fd};
        const auto                     PSESSION = WEAKSESSION.lock();
        if (!PSESSION)
            return;

        // the session answers with SelectionWrite, under the serial
        const auto SERIAL = PSESSION->transfers.add(std::move(transferFd));
        Debug::log(TRACE, "[clipboard] Transfer {} of {} from session {}", SERIAL, mimeType, PSESSION->sessionHandle.c_str());
        m_pObject->emitSignal("SelectionTransfer").onInterface(INTERFACE_NAME).withArguments(PSESSION->sessionHandle, std::string{mimeType}, SERIAL);
    });
    SOURCE->setCancelled([WEAKSESSION](CCExtDataControlSourceV1* source) {
        // something else took the selection
        const auto PSESSION = WEAKSESSION.lock();
        if (PSESSION && PSESSION->source.get() == source)
            destroyLater(std::move(PSESSION->source));
    });

    // the compositor replaces the previous selection, cancelling the source of any other session that held it
    m_sState.device->sendSetSelection(SOURCE.get());
    PSESSION->source = SOURCE;
}

sdbus::UnixFd CClipboardPortal::onSelectionWrite(sdbus::ObjectPath sessionHandle, uint32_t serial) {
    const auto PSESSION = getEnabledSession(sessionHandle);
    if (!PSESSION)
        fail("NotAllowed", std::format("Session {} has no clipboard access", sessionHandle.c_str()));

    auto fd = PSESSION->transfers.take(serial);
    if (!fd)
        fail("InvalidArgument", std::format("Session {} has no transfer {}", sessionHandle.c_str(), serial));

    // the session holds the only write end left, so the reader gets all of the content once it's closed
    return sdbus::UnixFd{fd->take(), sdbus::adopt_fd};
}

void CClipboardPortal::onSelectionWriteDone(sdbus::ObjectPath sessionHandle, uint32_t serial, bool success) {
    const auto PSESSION = getEnabledSession(sessionHandle);
    if (!PSESSION)
        fail("NotAllowed", std::format("Session {} has no clipboard access", sessionHandle.c_str()));

    Debug::log(TRACE, "[clipboard] Transfer {} from session {} done, success: {}", serial, sessionHandle.c_str(), success);

    // a transfer the session never wrote ends empty
    PSESSION->transfers.take(serial);
}

sdbus::UnixFd CClipboardPortal::onSelectionRead(sdbus::ObjectPath sessionHandle, std::string mimeType) {
    const auto PSESSION = getEnabledSession(sessionHandle);
    if (!PSESSION)
        fail("NotAllowed", std::format("Session {} has no clipboard access", sessionHandle.c_str()));

    if (!m_sState.selection || !std::ranges::contains(m_sState.selection->mimeTypes, mimeType))
        fail("InvalidArgument", std::format("The selection has no content for {}", mimeType));

    std::array<int, 2> fds = {-1, -1};
    if (pipe2(fds.data(), O_CLOEXEC) < 0)
        fail("Failed", "Couldn't create a pipe for the selection");

    Hyprutils::OS::CFileDescriptor readFd{fds[0]}, writeFd{fds[1]};

    // the request carries its own copy of the write end, so ours closes for the reader to see the end of the content
    m_sState.selection->offer->sendReceive(mimeType.c_str(), writeFd.get());

    Debug::log(TRACE, "[clipboard] Session {} reads the selection as {}", sessionHandle.c_str(), mimeType);

    return sdbus::UnixFd{readFd.take(), sdbus::adopt_fd};
}

bool CClipboardPortal::enableSession(const sdbus::ObjectPath& sessionHandle) {
    const auto IT = m_mSessions.find(sessionHandle);
    if (IT == m_mSessions.end())
        return false;

    IT->second->enabled = true;

    Debug::log(LOG, "[clipboard] Session {} has the clipboard", sessionHandle.c_str());

    // the frontend passes signals on once it has the reply to Start, so the current selection follows it
    g_pPortalManager->addTimer({0, [this, sessionHandle]() {
                                    const auto IT = m_mSessions.find(sessionHandle);
                                    if (IT != m_mSessions.end() && m_sState.selection)
                                        emitOwnerChanged(*IT->second);
                                }});

    return true;
}

void CClipboardPortal::removeSession(const sdbus::ObjectPath& sessionHandle) {
    const auto IT = m_mSessions.find(sessionHandle);
    if (IT == m_mSessions.end())
        return;

    // which takes its content off the clipboard, and ends the transfers it never wrote
    m_mSessions.erase(IT);
}

SP<CClipboardPortal::SSession> CClipboardPortal::getEnabledSession(const sdbus::ObjectPath& sessionHandle) {
    const auto IT = m_mSessions.find(sessionHandle);
    if (IT == m_mSessions.end() || !IT->second->enabled)
        return nullptr;

    return IT->second;
}

void CClipboardPortal::onDataOffer(wl_proxy* offer) {
    const auto POFFER = makeShared<SOffer>();
    POFFER->offer     = makeShared<CCExtDataControlOfferV1>(offer);

    const WP<SOffer> WEAKOFFER = POFFER;
    POFFER->offer->setOffer([WEAKOFFER](CCExtDataControlOfferV1*, const char* mimeType) {
        if (const auto POFFER = WEAKOFFER.lock())
            POFFER->mimeTypes.emplace_back(mimeType);
    });

    // the selection or primary selection event that follows says which it's for
    m_sState.pendingOffer = POFFER;
}

void CClipboardPortal::onSelection(wl_proxy* offer) {
    if (offer && m_sState.pendingOffer && m_sState.pendingOffer->offer->resource() == offer)
        m_sState.selection = m_sState.pendingOffer;
    else
        m_sState.selection.reset();

    m_sState.pendingOffer.reset();

    Debug::log(TRACE, "[clipboard] Selection changed, {} mime types", m_sState.selection ? m_sState.selection->mimeTypes.size() : 0);

    for (const auto& [_, session] : m_mSessions) {
        if (!session->enabled)
            continue;

        emitOwnerChanged(*session);
    }
}

void CClipboardPortal::emitOwnerChanged(const SSession& session) {
    std::unordered_map<std::string, sdbus::Variant> options;
    options["mime_types"] = sdbus::Variant{m_sState.selection ? m_sState.selection->mimeTypes : std::vector<std::string>{}};
    // a session's source lives for as long as its content is the selection
    options["session_is_owner"] = sdbus::Variant{m_sState.selection && session.source};

    m_pObject->emitSignal("SelectionOwnerChanged").onInterface(INTERFACE_NAME).withArguments(session.sessionHandle, options);
}
