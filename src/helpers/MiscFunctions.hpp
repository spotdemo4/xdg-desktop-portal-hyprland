#pragma once
#include <string>
#include <sdbus-c++/Message.h>

std::string execAndGet(const char* cmd);
void        addHyprlandNotification(const std::string& icon, float timeMs, const std::string& color, const std::string& message);
bool        inShellPath(const std::string& exec);
void        sendEmptyDbusMethodReply(sdbus::MethodCall& call, u_int32_t responseCode);
int         allocateSHMFile(size_t len);
bool        allocateSHMFilePair(size_t size, int* rw_fd_ptr, int* ro_fd_ptr);
// milliseconds on the monotonic clock, which input events are timestamped with
uint32_t    monotonicMs();
