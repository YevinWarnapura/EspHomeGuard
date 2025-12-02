#pragma once

enum class RemoteCommandType {
    NONE,
    ARM,
    DISARM
};

// Initialize remote module (optional)
void remote_init();

// Check for remote command
RemoteCommandType remote_check_command();