#ifndef SECRETS_H
#define SECRETS_H

struct WifiConfig {
    const char* ssid;
    const char* password; // Use "" or NULL for open networks
};

// Add your networks here. Newest networks should be appended to the BOTTOM
// to ensure they are given top connection priority during the boot scan.
const WifiConfig WIFI_NETWORKS[] = {
    {"Vaayu", "PASSWORDREDACTED"},
    {"wwp", ""},             // Example of an open network
    {"Pixel 8xay Replacement", "PASSWORDREDACTED"}   // Appended to bottom = Highest Priority
};

// Automatically calculate network array volume at compile time
const int WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

#endif
