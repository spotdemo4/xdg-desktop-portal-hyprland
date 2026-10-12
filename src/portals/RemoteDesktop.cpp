#include "RemoteDesktop.hpp"
#include "../core/PortalManager.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"

void CRemoteDesktopPortal::registerPointer(SP<CCZwlrVirtualPointerManagerV1> mgr) {
    m_sState.pointerMgr = mgr;
}

void CRemoteDesktopPortal::registerKeyboard(SP<CCZwpVirtualKeyboardManagerV1> mgr) {
    m_sState.keyboardMgr = mgr;
}

CRemoteDesktopPortal::CRemoteDesktopPortal() {
    Debug::log(LOG, "[remotedesktop] initializing remote desktop portal");
    m_pObject = sdbus::createObject(*g_pPortalManager->getConnection(), OBJECT_PATH);
    m_pObject
        ->addVTable(
            sdbus::registerMethod("CreateSession")
                .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                    return onCreateSession(o1, o2, s, m);
                }),
            sdbus::registerMethod("SelectDevices")
                .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                    return onSelectDevices(o1, o2, s, m);
                }),
            sdbus::registerMethod("Start")
                .implementedAs([this](sdbus::ObjectPath o1, sdbus::ObjectPath o2, std::string s1, std::string s2, std::unordered_map<std::string, sdbus::Variant> m) {
                    return onStart(o1, o2, s1, s2, m);
                }),
            sdbus::registerMethod("NotifyPointerMotion")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                    return onNotifyPointerMotion(o, d1, d2, m);
                }),
            sdbus::registerMethod("NotifyPointerMotionAbsolute")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, unsigned int u, double d1, double d2) {
                    return onNotifyPointerMotionAbsolute(o, u, d1, d2, m);
                }),
            sdbus::registerMethod("NotifyPointerButton")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int i, unsigned int u) {
                    return onNotifyPointerButton(o, i, u, m);
                }),
            sdbus::registerMethod("NotifyPointerAxis")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, double d1, double d2) {
                    return onNotifyPointerAxis(o, d1, d2, m);
                }),
            sdbus::registerMethod("NotifyPointerAxisDiscrete")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, unsigned int u, int i) {
                    return onNotifyPointerAxisDiscrete(o, u, i, m);
                }),
            sdbus::registerMethod("NotifyKeyboardKeycode")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int i, unsigned int u) {
                    return onNotifyKeyboardKeycode(o, i, u, m);
                }),
            sdbus::registerMethod("NotifyKeyboardKeysym")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, int i, unsigned int u) {
                    return onNotifyKeyboardKeysym(o, i, u, m);
                }),
            sdbus::registerMethod("NotifyTouchDown")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, unsigned int u1, unsigned int u2, double d1, double d2) {
                    return onNotifyTouchDown(o, u1, u2, d1, d2, m);
                }),
            sdbus::registerMethod("NotifyTouchMotion")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, unsigned int u1, unsigned int u2, double d1, double d2) {
                    return onNotifyTouchMotion(o, u1, u2, d1, d2, m);
                }),
            sdbus::registerMethod("NotifyTouchUp")
                .implementedAs([this](sdbus::ObjectPath o, std::unordered_map<std::string, sdbus::Variant> m, unsigned int u) {
                    return onNotifyTouchUp(o, u, m);
                }),
            sdbus::registerMethod("ConnectToEIS").implementedAs([this](sdbus::ObjectPath o, std::string s, std::unordered_map<std::string, sdbus::Variant> m) {
                return onConnectToEIS(o, s, m);
            }),
            sdbus::registerProperty("AvailableDeviceTypes").withGetter([] { return (uint32_t)(1 | 2); }),
            sdbus::registerProperty("version").withGetter([] { return (uint32_t)(2); }))
        .forInterface(INTERFACE_NAME);

}

dbUasv CRemoteDesktopPortal::onCreateSession(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] New session:");
    Debug::log(LOG, "[remotedesktop]  | {}", requestHandle.c_str());
    Debug::log(LOG, "[remotedesktop]  | {}", sessionHandle.c_str());
    Debug::log(LOG, "[remotedesktop]  | appid: {}", appID);

    std::shared_ptr<SSession> PSESSION = std::make_shared<SSession>(appID, requestHandle, sessionHandle);

    // create objects
    PSESSION->session            = createDBusSession(sessionHandle);
    PSESSION->sessionHandle      = sessionHandle;
    PSESSION->session->onDestroy = [PSESSION, this]() {
        if (g_pPortalManager->m_sPortals.screencopy)
            g_pPortalManager->m_sPortals.screencopy->removeRemoteDesktopSession(PSESSION->sessionHandle);
        PSESSION->eis->stopServer();
        PSESSION->eis.reset();
        Debug::log(LOG, "[remotedesktop] Session {} destroyed", PSESSION->sessionHandle.c_str());
        PSESSION->session.release();
        m_mSessions.erase(PSESSION->sessionHandle);
    };

    PSESSION->request            = createDBusRequest(requestHandle);
    PSESSION->requestHandle      = requestHandle;
    PSESSION->request->onDestroy = [PSESSION]() { PSESSION->request.release(); };

    PSESSION->pointer            = makeShared<CCZwlrVirtualPointerV1>(m_sState.pointerMgr->sendCreateVirtualPointer(g_pPortalManager->m_sWaylandConnection.seat->resource()));
    PSESSION->keyboard           = makeShared<CCZwpVirtualKeyboardV1>(m_sState.keyboardMgr->sendCreateVirtualKeyboard(g_pPortalManager->m_sWaylandConnection.seat->resource()));

    const auto& keymap = g_pPortalManager->m_sKeymap;
    PSESSION->keyboard->sendKeymap(keymap.format, keymap.fd, keymap.size);

    PSESSION->eis                = std::make_unique<EmulatedInputServer>("eisr-" + std::to_string(m_uSessionCounter++));
    PSESSION->eis->setVirtualPointer(PSESSION->pointer);
    PSESSION->eis->setVirtualKeyboard(PSESSION->keyboard);
    PSESSION->eis->setKeymap({.fd = keymap.fd, .size = keymap.size});

    m_mSessions.emplace(sessionHandle, PSESSION);

    // ScreenCast.SelectSources may be called with this session, to share the screen along with input
    if (g_pPortalManager->m_sPortals.screencopy)
        g_pPortalManager->m_sPortals.screencopy->addRemoteDesktopSession(requestHandle, sessionHandle, appID);

    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onSelectDevices(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID,
                                             std::unordered_map<std::string, sdbus::Variant> opts) {
    // what a restored session shared comes with its devices, and its sources are selected after
    const auto PCAST = g_pPortalManager->m_sPortals.screencopy ? g_pPortalManager->m_sPortals.screencopy->getSession(sessionHandle) : nullptr;
    if (const auto RESTORE = opts.find("restore_data"); PCAST && RESTORE != opts.end())
        PCAST->restoreData = RESTORE->second;

    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onStart(sdbus::ObjectPath requestHandle, sdbus::ObjectPath sessionHandle, std::string appID, std::string parentWindow,
                                     std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] start request");

    std::unordered_map<std::string, sdbus::Variant> results;

    // share the screen, when sources were selected with this session
    const auto PCAST = g_pPortalManager->m_sPortals.screencopy ? g_pPortalManager->m_sPortals.screencopy->getSession(sessionHandle) : nullptr;
    if (PCAST && PCAST->sourcesSelected) {
        auto [code, cast] = g_pPortalManager->m_sPortals.screencopy->onStart(requestHandle, sessionHandle, appID, parentWindow, opts);
        if (code != 0)
            return {code, {}};
        results = cast;

        const auto PSESSION = m_mSessions[sessionHandle];
        const auto POUTPUT  = PCAST->selection.type == TYPE_OUTPUT ? g_pPortalManager->getOutputFromName(PCAST->selection.output) : nullptr;
        if (PSESSION && POUTPUT && wl_proxy_get_version((wl_proxy*)m_sState.pointerMgr->resource()) >= 2) {
            PSESSION->outputPointer = makeShared<CCZwlrVirtualPointerV1>(
                m_sState.pointerMgr->sendCreateVirtualPointerWithOutput(g_pPortalManager->m_sWaylandConnection.seat->resource(), POUTPUT->output->resource()));
            PSESSION->streamNode   = PCAST->sharingData.nodeID;
            PSESSION->streamWidth  = PCAST->sharingData.frameInfoSHM.w;
            PSESSION->streamHeight = PCAST->sharingData.frameInfoSHM.h;
        }
    }

    results["devices"]           = sdbus::Variant{uint32_t{1 | 2}};
    results["clipboard_enabled"] = sdbus::Variant{bool(true)};

    if (!results.contains("restore_data")) {
        std::unordered_map<std::string, sdbus::Variant> restoreData;
        results["restore_data"] = sdbus::Variant{sdbus::Struct<std::string, uint32_t, sdbus::Variant>{"hyprland", 1, sdbus::Variant{restoreData}}};
        results["persist_mode"] = sdbus::Variant{uint32_t{2}};
    }

    return {0, results};
}

dbUasv CRemoteDesktopPortal::onNotifyPointerMotion(sdbus::ObjectPath sessionHandle, double dx, double dy, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    PSESSION->pointer->sendMotion(0, dx, dy);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyPointerMotionAbsolute(sdbus::ObjectPath sessionHandle, unsigned int stream, double x, double y, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    // positions are in the stream's coordinates, which the output's pointer maps onto the output
    if (PSESSION && PSESSION->outputPointer && stream == PSESSION->streamNode && PSESSION->streamWidth > 0 && PSESSION->streamHeight > 0) {
        PSESSION->outputPointer->sendMotionAbsolute(0, x, y, PSESSION->streamWidth, PSESSION->streamHeight);
        PSESSION->outputPointer->sendFrame();
        return {0, {}};
    }
    PSESSION->pointer->sendMotionAbsolute(0, x, y, 1920, 1080);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyPointerButton(sdbus::ObjectPath sessionHandle, int button, unsigned int state, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    PSESSION->pointer->sendButton(0, button, state);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyPointerAxis(sdbus::ObjectPath sessionHandle, double dx, double dy, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    PSESSION->pointer->sendAxis(0, 0, dy);
    PSESSION->pointer->sendAxis(0, 1, dx);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyPointerAxisDiscrete(sdbus::ObjectPath sessionHandle, unsigned int axis, int steps, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    PSESSION->pointer->sendAxisDiscrete(1, 0, axis, steps);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyKeyboardKeycode(sdbus::ObjectPath sessionHandle, int keycode, unsigned int state, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    PSESSION->keyboard->sendKey(1, keycode, state);
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyKeyboardKeysym(sdbus::ObjectPath sessionHandle, int keysym, unsigned int state, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyTouchDown(sdbus::ObjectPath sessionHandle, unsigned int stream, unsigned int slot, double x, double y,
                                               std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyTouchMotion(sdbus::ObjectPath sessionHandle, unsigned int stream, unsigned int slot, double x, double y,
                                                 std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    return {0, {}};
}

dbUasv CRemoteDesktopPortal::onNotifyTouchUp(sdbus::ObjectPath sessionHandle, unsigned int slot, std::unordered_map<std::string, sdbus::Variant> opts) {
    const auto PSESSION = m_mSessions[sessionHandle];
    return {0, {}};
}

sdbus::UnixFd CRemoteDesktopPortal::onConnectToEIS(sdbus::ObjectPath sessionHandle, std::string appID, std::unordered_map<std::string, sdbus::Variant> opts) {
    Debug::log(LOG, "[remotedesktop] New ConnectToEIS request: {}:", sessionHandle.c_str());
    const auto PSESSION = m_mSessions[sessionHandle];
    if (!PSESSION) return (sdbus::UnixFd)0;

    int sockfd = PSESSION->eis->getFileDescriptor();

    Debug::log(LOG, "[remotedesktop] Connected to the socket. File descriptor: {}", sockfd);
    return (sdbus::UnixFd)sockfd;
}
