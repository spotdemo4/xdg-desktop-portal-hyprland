#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "../includes.hpp"
#include "../shared/ClipboardTransfers.hpp"

struct wl_proxy;
class CCExtDataControlManagerV1;
class CCExtDataControlDeviceV1;
class CCExtDataControlSourceV1;
class CCExtDataControlOfferV1;

// the clipboard of sessions from other portals, which share the seat's selection through data control
class CClipboardPortal {
  public:
    CClipboardPortal(SP<CCExtDataControlManagerV1> mgr);

    void          onRequestClipboard(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    void          onSetSelection(sdbus::ObjectPath sessionHandle, std::unordered_map<std::string, sdbus::Variant> opts);
    sdbus::UnixFd onSelectionWrite(sdbus::ObjectPath sessionHandle, uint32_t serial);
    void          onSelectionWriteDone(sdbus::ObjectPath sessionHandle, uint32_t serial, bool success);
    sdbus::UnixFd onSelectionRead(sdbus::ObjectPath sessionHandle, std::string mimeType);

    // a session that requested the clipboard gets it as it starts, returns whether it did
    bool enableSession(const sdbus::ObjectPath& sessionHandle);
    void removeSession(const sdbus::ObjectPath& sessionHandle);

  private:
    struct SSession {
        sdbus::ObjectPath sessionHandle;
        bool              enabled = false;
        // what the session offers while it owns the selection
        SP<CCExtDataControlSourceV1> source;
        CClipboardTransfers          transfers;
    };

    struct SOffer {
        SP<CCExtDataControlOfferV1> offer;
        std::vector<std::string>    mimeTypes;
    };

    SP<SSession> getEnabledSession(const sdbus::ObjectPath& sessionHandle);
    void         onDataOffer(wl_proxy* offer);
    void         onSelection(wl_proxy* offer);
    void         emitOwnerChanged(const SSession& session);

    struct {
        SP<CCExtDataControlManagerV1> manager;
        SP<CCExtDataControlDeviceV1>  device;
        // the offer last introduced, until it's known which selection it's for
        SP<SOffer> pendingOffer;
        SP<SOffer> selection;
    } m_sState;

    std::unordered_map<std::string, SP<SSession>> m_mSessions;

    std::unique_ptr<sdbus::IObject>               m_pObject;

    const sdbus::InterfaceName                    INTERFACE_NAME = sdbus::InterfaceName{"org.freedesktop.impl.portal.Clipboard"};
    const sdbus::ObjectPath                       OBJECT_PATH    = sdbus::ObjectPath{"/org/freedesktop/portal/desktop"};
};
