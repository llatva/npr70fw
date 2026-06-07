/*
 * Combined NetworkMgmt + Telnet Task for NPR-70
 * Runs both network management (DHCP/ARP + SNMP) and Telnet HMI in a single FreeRTOS task.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "task_telnet.h"
#include "task_networkmgmt.h"
#include "w5500_driver.h"
#include <stdio.h>

static W5500_Context_t *hw5500 = NULL;

void NetMgmtTelnetTask_Init(W5500_Context_t *w5500_ctx) {
    hw5500 = w5500_ctx;
    DHCPARPTask_Init(hw5500);
    SNMPTask_Init(hw5500);
    TelnetTask_Init(hw5500);
}

void vNetMgmtTelnetTask(void *argument) {
    printf("NetMgmtTelnetTask (DHCP/ARP + SNMP + Telnet) started\r\n");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(100); // 100ms period
    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        // Network management polling
        DHCPARPTask_Poll(hw5500);
        SNMPTask_Poll(hw5500);
        // Telnet polling
        ProcessTelnetConnection();
    }
}
