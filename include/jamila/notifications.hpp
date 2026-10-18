#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

namespace jamila {
// Freedesktop Notifications (session D-Bus) helper. Approval always stays pending
// until the GUI explicitly consumes an action, even if the notification expires.
class DesktopApprovalNotification {
public:
    DesktopApprovalNotification();
    ~DesktopApprovalNotification();
    DesktopApprovalNotification(const DesktopApprovalNotification&) = delete;
    DesktopApprovalNotification& operator=(const DesktopApprovalNotification&) = delete;
    bool show(const std::string& tool);
    // Returns "approve", "review", "deny" or empty string.
    std::string poll_action();
    bool active() const { return notification_id_ != 0; }
private:
    pid_t monitor_pid_ = -1;
    int monitor_fd_ = -1;
    uint32_t notification_id_ = 0;
    std::string monitor_buffer_;
    int state_ = 0;
    void close_monitor();
    std::string consume_line(const std::string& line);
};
// Exposed for deterministic parser tests, not as an authorization bypass.
std::string notification_action_from_signal(const std::string& text, uint32_t id);
}