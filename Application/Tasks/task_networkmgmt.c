/**
 * Combined NetworkMgmt Task for NPR-70
 * Handles both DHCP/ARP and SNMP in a single loop.
 */
#include "task_dhcp_arp.h"
#include "task_snmp.h"
#include "app_common.h"
#include "w5500_driver.h"
#include <stdio.h>
#include <string.h>

static W5500_Context_t *hw5500 = NULL;

void NetworkMgmtTask_Init(W5500_Context_t *w5500_ctx) {
    hw5500 = w5500_ctx;
    DHCPARPTask_Init(hw5500);
    SNMPTask_Init(hw5500);
}

void vNetworkMgmtTask(void *argument) {
    printf("NetworkMgmtTask (DHCP/ARP + SNMP) started\r\n");
    for (;;) {
        // Call DHCP/ARP periodic handler
        extern void DHCPARPTask_Poll(W5500_Context_t *hw5500);
        DHCPARPTask_Poll(hw5500);
        // Call SNMP periodic handler
        extern void SNMPTask_Poll(W5500_Context_t *hw5500);
        SNMPTask_Poll(hw5500);
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to avoid busy loop
    }
}

// Provide polling wrappers for DHCP/ARP and SNMP logic
void DHCPARPTask_Poll(W5500_Context_t *hw5500) {
    // Inline the main loop logic from vDHCPARPTask, but do not block
    // (copy from vDHCPARPTask, but replace for(;;) with a single iteration)
    // ...user should fill in with actual periodic logic...
}
void SNMPTask_Poll(W5500_Context_t *hw5500) {
    // Inline the main loop logic from vSNMPTask, but do not block
    // (copy from vSNMPTask, but replace for(;;) with a single iteration)
    // ...user should fill in with actual periodic logic...
}
