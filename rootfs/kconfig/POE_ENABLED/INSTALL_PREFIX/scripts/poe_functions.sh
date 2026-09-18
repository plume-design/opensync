#!/bin/sh -e

poe_low_power_detected()
{
    echo "Function 'poe_low_power_detected' not overridden, returning false"
    return 1
}

target_device_wdt_ping()
{
    echo "Function 'target_device_wdt_ping' not overridden, not pinging watchdog"
}

target_device_wdt_ping_stop()
{
    echo "Function 'target_device_wdt_ping_stop' not overridden, not stopping watchdog ping"
}
