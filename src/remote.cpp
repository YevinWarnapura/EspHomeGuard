#include "remote.h"
#include "esp_log.h"

static const char* TAG = "REMOTE";

// ---- TEST MODE ----
// Modify these two variables manually for testing
static bool test_arm_request = false;
static bool test_disarm_request = false;

// ---------------------
// Initialization
// ---------------------
void remote_init()
{
    ESP_LOGI(TAG, "Remote module initialized (stub)");
}

// ---------------------
// MAIN REMOTE CHECK
// ---------------------
RemoteCommandType remote_check_command()
{
    // Simulated ARM request
    if (test_arm_request)
    {
        test_arm_request = false;
        ESP_LOGI(TAG, "REMOTE: ARM command received");
        return RemoteCommandType::ARM;
    }

    // Simulated DISARM request
    if (test_disarm_request)
    {
        test_disarm_request = false;
        ESP_LOGI(TAG, "REMOTE: DISARM command received");
        return RemoteCommandType::DISARM;
    }

    return RemoteCommandType::NONE;
}
