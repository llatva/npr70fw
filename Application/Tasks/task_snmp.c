/*
  ******************************************************************************
  * @file    task_snmp.c
  * @brief   SNMP Agent Task Implementation
  ******************************************************************************
  * @attention
  *
  * Port of NPR-70 modem firmware from mbed OS to FreeRTOS
  * Original copyright (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port by Lasse OH3HZB
  *
  * SNMP v1 agent - processes GET/GET-NEXT/SET requests
  * Exposes system and modem-specific MIB variables
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "task_snmp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "w5500_driver.h"
#include "app_common.h"
#include <string.h>
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/
#define COPY_SEGMENT(x) \
{ \
    request_msg.index += seglen; \
    memcpy(&response_msg.buffer[response_msg.index], &request_msg.buffer[x.start], seglen); \
    response_msg.index += seglen; \
}

#define VALID_REQUEST(x) ((x == GET_REQUEST) || (x == GET_NEXT_REQUEST) || (x == SET_REQUEST))

/* Private types -------------------------------------------------------------*/
typedef struct {
    uint8_t buffer[MAX_SNMPMSG_LEN];
    uint16_t len;
    uint16_t index;
} MessageStruct_t;

typedef struct {
    uint16_t start;   /* Absolute Index of the TLV */
    uint16_t len;     /* The L value of the TLV */
    uint16_t vstart;  /* Absolute Index of this TLV's Value */
    uint16_t nstart;  /* Absolute Index of the next TLV */
} TLVStruct_t;

typedef struct {
    uint8_t oidlen;
    uint8_t oid[MAX_OID];
    uint8_t dataType;
    uint8_t dataLen;
    union {
        uint8_t octetstring[MAX_STRING];
        uint32_t intval;
    } u;
    void (*getfunction)(void *, uint8_t *);
    void (*setfunction)(int32_t);
} DataEntry_t;

/* Private variables ---------------------------------------------------------*/
static W5500_Context_t *pw5500 = NULL;
static SNMPStats_t stats = {0};

/* SNMP message buffers - ~800 bytes total */
static MessageStruct_t request_msg;
static MessageStruct_t response_msg;
static uint8_t errorStatus, errorIndex;

/* External variables --------------------------------------------------------*/
extern SemaphoreHandle_t xSPI3Mutex;

/* External global variables - declared in app_common.h */
extern volatile uint8_t G_temperature_SI4463;
extern volatile uint8_t my_client_radio_connexion_state;
extern uint8_t my_radio_client_ID;
extern volatile int32_t TDMA_table_TA[RADIO_ADDR_TABLE_SIZE];
extern volatile uint16_t G_radio_addr_table_RSSI[RADIO_ADDR_TABLE_SIZE];
extern volatile uint16_t G_downlink_RSSI;
extern volatile uint16_t G_radio_addr_table_BER[RADIO_ADDR_TABLE_SIZE];
extern volatile uint16_t G_downlink_BER;

/* Private function prototypes -----------------------------------------------*/
static void SNMPGetUptime(void *ptr, uint8_t *len);
static void SNMPGetTemp(void *ptr, uint8_t *len);
static void SNMPGetLinkStatus(void *ptr, uint8_t *len);
static void SNMPGetLinkDistance(void *ptr, uint8_t *len);
static void SNMPGetRSSIUp(void *ptr, uint8_t *len);
static void SNMPGetRSSIDown(void *ptr, uint8_t *len);
static void SNMPGetErrUp(void *ptr, uint8_t *len);
static void SNMPGetErrDown(void *ptr, uint8_t *len);

static void SNMPGetVar(void *ptr, uint8_t *len, const uint32_t var);
static int8_t FindEntry(uint8_t *oid, uint8_t len);
static int8_t GetOID(int8_t id, uint8_t *oid, uint8_t *len);
static int32_t GetValue(uint8_t *vptr, int32_t vlen);
static int32_t GetEntry(int8_t id, uint8_t *dataType, void *ptr, uint8_t *len);
static int8_t SetEntry(uint8_t id, void *val, int32_t vlen, uint8_t dataType, int32_t index);

static uint16_t ParseLength(const uint8_t *msg, uint16_t *len);
static void ParseTLV(const uint8_t *msg, int32_t index, TLVStruct_t *tlv);
static void InsertRespLen(int32_t reqStart, int32_t respStart, int16_t size);

static int32_t ParseVarBind(int32_t reqType, int32_t index);
static int32_t ParseSequence(int32_t reqType, int32_t index);
static int32_t ParseSequenceOf(int32_t reqType);
static int32_t ParseRequest(void);
static int16_t ParseCommunity(void);
static int16_t ParseVersion(void);
static int8_t ParseSNMP(void);
static void ProcessSNMPMessage(void);

/* MIB Data Table ------------------------------------------------------------*/
static DataEntry_t snmpData[] = {
    /* System MIB */
    /* sysDescr - 1.3.6.1.2.1.1.1.0 */
    {8, {0x2b, 6, 1, 2, 1, 1, 1, 0}, ASN_OCTETSTRING, 6, {{"NPR-70"}}, NULL, NULL},
    
    /* sysUptime - 1.3.6.1.2.1.1.3.0 */
    {8, {0x2b, 6, 1, 2, 1, 1, 3, 0}, ASN_TIMETICKS, 0, {{}}, SNMPGetUptime, NULL},
    
    /* NPR-70 Private Enterprise MIB - 1.3.6.1.4.1.54539.100.x.0 */
    /* Temperature - 1.3.6.1.4.1.54539.100.1.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 1, 0}, ASN_GAUGE, 0, {{}}, SNMPGetTemp, NULL},
    
    /* Link Status - 1.3.6.1.4.1.54539.100.2.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 2, 0}, ASN_INTEGER, 0, {{}}, SNMPGetLinkStatus, NULL},
    
    /* Link Distance - 1.3.6.1.4.1.54539.100.3.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 3, 0}, ASN_INTEGER, 0, {{}}, SNMPGetLinkDistance, NULL},
    
    /* RSSI Uplink - 1.3.6.1.4.1.54539.100.4.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 4, 0}, ASN_GAUGE, 0, {{}}, SNMPGetRSSIUp, NULL},
    
    /* RSSI Downlink - 1.3.6.1.4.1.54539.100.5.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 5, 0}, ASN_GAUGE, 0, {{}}, SNMPGetRSSIDown, NULL},
    
    /* Error Rate Uplink - 1.3.6.1.4.1.54539.100.6.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 6, 0}, ASN_GAUGE, 0, {{}}, SNMPGetErrUp, NULL},
    
    /* Error Rate Downlink - 1.3.6.1.4.1.54539.100.7.0 */
    {11, {0x2b, 6, 1, 4, 1, 0x83, 0xaa, 0x0b, 100, 7, 0}, ASN_GAUGE, 0, {{}}, SNMPGetErrDown, NULL},
};

static const uint8_t maxData = (sizeof(snmpData) / sizeof(DataEntry_t));

/* MIB Value Getter Functions ------------------------------------------------*/

/**
 * @brief Get system uptime in hundredths of a second
 */
static void SNMPGetUptime(void *ptr, uint8_t *len) {
    uint32_t seconds = (uint32_t)(xTaskGetTickCount() / 1000);
    *(uint32_t *)ptr = seconds * 100;  /* Convert to hundredths */
    *len = 4;
}

/**
 * @brief Generic getter for uint32_t variables
 */
static void SNMPGetVar(void *ptr, uint8_t *len, const uint32_t var) {
    *(uint32_t *)ptr = var;
    *len = 4;
}

/**
 * @brief Get SI4463 temperature
 */
static void SNMPGetTemp(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)G_temperature_SI4463);
}

/**
 * @brief Get link connection status
 * 1 = waiting connection, 2 = connected, 3 = connection rejected
 */
static void SNMPGetLinkStatus(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)my_client_radio_connexion_state);
}

/**
 * @brief Get link distance
 * Formula: 0.15 * value (in meters)
 */
static void SNMPGetLinkDistance(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)TDMA_table_TA[my_radio_client_ID]);
}

/**
 * @brief Get uplink RSSI
 * Formula for dBm: value/2 - 136
 */
static void SNMPGetRSSIUp(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)G_radio_addr_table_RSSI[my_radio_client_ID]);
}

/**
 * @brief Get downlink RSSI
 * Formula for dBm: value/2 - 136
 */
static void SNMPGetRSSIDown(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)(G_downlink_RSSI >> 8));
}

/**
 * @brief Get uplink error rate
 * Formula for percent: value / 500
 */
static void SNMPGetErrUp(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)G_radio_addr_table_BER[my_radio_client_ID]);
}

/**
 * @brief Get downlink error rate
 * Formula for percent: value / 500
 */
static void SNMPGetErrDown(void *ptr, uint8_t *len) {
    SNMPGetVar(ptr, len, (uint32_t)G_downlink_BER);
}

/* SNMP Data Access Functions ------------------------------------------------*/

/**
 * @brief Find MIB entry by OID
 */
static int8_t FindEntry(uint8_t *oid, uint8_t len) {
    for (uint8_t i = 0; i < maxData; i++) {
        if (len == snmpData[i].oidlen) {
            if (!memcmp(snmpData[i].oid, oid, len)) {
                return (int8_t)i;
            }
        }
    }
    return OID_NOT_FOUND;
}

/**
 * @brief Get OID by entry ID
 */
static int8_t GetOID(int8_t id, uint8_t *oid, uint8_t *len) {
    if (!((id >= 0) && (id < maxData))) {
        return INVALID_ENTRY_ID;
    }
    
    *len = snmpData[id].oidlen;
    for (uint8_t j = 0; j < *len; j++) {
        oid[j] = snmpData[id].oid[j];
    }
    
    return SNMP_SUCCESS;
}

/**
 * @brief Convert byte array to integer value
 */
static int32_t GetValue(uint8_t *vptr, int32_t vlen) {
    int32_t index = 0;
    int32_t value = 0;
    
    while (index < vlen) {
        if (index != 0) {
            value <<= 8;
        }
        value |= vptr[index++];
    }
    
    return value;
}

/**
 * @brief Get MIB entry data
 */
static int32_t GetEntry(int8_t id, uint8_t *dataType, void *ptr, uint8_t *len) {
    uint8_t *ptr_8;
    int32_t value;
    uint8_t *string;
    uint8_t j;
    
    if (!((id >= 0) && (id < maxData))) {
        return INVALID_ENTRY_ID;
    }
    
    *dataType = snmpData[id].dataType;
    
    switch (*dataType) {
    case ASN_OCTETSTRING:
    case ASN_OBJECTID:
    {
        string = (uint8_t *)ptr;
        
        if (snmpData[id].getfunction != NULL) {
            snmpData[id].getfunction((void *)&snmpData[id].u.octetstring, &snmpData[id].dataLen);
        }
        
        if ((*dataType) == ASN_OCTETSTRING) {
            snmpData[id].dataLen = (uint8_t)strlen((char const *)&snmpData[id].u.octetstring);
        }
        
        *len = snmpData[id].dataLen;
        for (j = 0; j < *len; j++) {
            string[j] = snmpData[id].u.octetstring[j];
        }
    }
    break;
    
    case ASN_INTEGER:
    case ASN_TIMETICKS:
    case ASN_COUNTER:
    case ASN_GAUGE:
    {
        if (snmpData[id].getfunction != NULL) {
            snmpData[id].getfunction((void *)&snmpData[id].u.intval, &snmpData[id].dataLen);
        }
        
        if (snmpData[id].dataLen) {
            *len = snmpData[id].dataLen;
        } else {
            *len = sizeof(uint32_t);
        }
        
        ptr_8 = (uint8_t *)ptr;
        value = snmpData[id].u.intval;
        
        for (j = 0; j < *len; j++) {
            ptr_8[j] = (uint8_t)((value >> ((*len - j - 1) * 8)));
        }
    }
    break;
    
    default:
        return INVALID_DATA_TYPE;
    }
    
    return SNMP_SUCCESS;
}

/**
 * @brief Set MIB entry data
 */
static int8_t SetEntry(uint8_t id, void *val, int32_t vlen, uint8_t dataType, int32_t index) {
    int8_t retStatus = OID_NOT_FOUND;
    int32_t j;
    
    if (snmpData[id].dataType != dataType) {
        errorStatus = BAD_VALUE;
        errorIndex = index;
        return INVALID_DATA_TYPE;
    }
    
    switch (snmpData[id].dataType) {
    case ASN_OCTETSTRING:
    case ASN_OBJECTID:
    {
        uint8_t *string = (uint8_t *)val;
        for (j = 0; j < vlen; j++) {
            snmpData[id].u.octetstring[j] = string[j];
        }
        snmpData[id].dataLen = vlen;
    }
        retStatus = SNMP_SUCCESS;
        break;
    
    case ASN_INTEGER:
    case ASN_TIMETICKS:
    case ASN_COUNTER:
    case ASN_GAUGE:
    {
        snmpData[id].u.intval = GetValue((uint8_t *)val, vlen);
        snmpData[id].dataLen = vlen;
        
        if (snmpData[id].setfunction != NULL) {
            snmpData[id].setfunction(snmpData[id].u.intval);
        }
    }
        retStatus = SNMP_SUCCESS;
        break;
    
    default:
        retStatus = INVALID_DATA_TYPE;
        break;
    }
    
    return retStatus;
}

/* SNMP Protocol Parsing Functions -------------------------------------------*/

/**
 * @brief Insert response length into message
 */
static void InsertRespLen(int32_t reqStart, int32_t respStart, int16_t size) {
    int32_t indexStart, lenLength;
    uint32_t mask = 0xff;
    int32_t shift = 0;
    
    if (request_msg.buffer[reqStart + 1] & 0x80) {
        lenLength = request_msg.buffer[reqStart + 1] & 0x7f;
        indexStart = respStart + 2;
        
        while (lenLength--) {
            response_msg.buffer[indexStart + lenLength] = 
                (uint8_t)((size & mask) >> shift);
            shift += 8;
            mask <<= shift;
        }
    } else {
        response_msg.buffer[respStart + 1] = (uint8_t)(size & 0xff);
    }
}

/**
 * @brief Parse ASN.1 length field
 */
static uint16_t ParseLength(const uint8_t *msg, uint16_t *len) {
    uint16_t i = 1;
    
    if (msg[0] & 0x80) {
        uint16_t tlen = (msg[0] & 0x7f) - 1;
        *len = msg[i++];
        while (tlen--) {
            *len <<= 8;
            *len |= msg[i++];
        }
    } else {
        *len = msg[0];
    }
    return i;
}

/**
 * @brief Parse TLV (Type-Length-Value) structure
 */
static void ParseTLV(const uint8_t *msg, int32_t index, TLVStruct_t *tlv) {
    int32_t Llen = 0;
    
    tlv->start = index;
    Llen = ParseLength((const uint8_t *)&msg[index + 1], &tlv->len);
    tlv->vstart = index + Llen + 1;
    
    switch (msg[index]) {
    case SNMP_SEQUENCE:
    case GET_REQUEST:
    case GET_NEXT_REQUEST:
    case SET_REQUEST:
        tlv->nstart = tlv->vstart;
        break;
    default:
        tlv->nstart = tlv->vstart + tlv->len;
        break;
    }
}

/**
 * @brief Parse variable binding
 */
static int32_t ParseVarBind(int32_t reqType, int32_t index) {
    int32_t seglen = 0;
    int8_t id;
    TLVStruct_t name, value;
    int32_t size = 0;
    
    ParseTLV(request_msg.buffer, request_msg.index, &name);
    
    if (request_msg.buffer[name.start] != ASN_OBJECTID) {
        return -1;
    }
    
    id = FindEntry(&request_msg.buffer[name.vstart], name.len);
    
    if ((reqType == GET_REQUEST) || (reqType == SET_REQUEST)) {
        seglen = name.nstart - name.start;
        COPY_SEGMENT(name);
        size = seglen;
    } else if (reqType == GET_NEXT_REQUEST) {
        response_msg.buffer[response_msg.index] = request_msg.buffer[name.start];
        
        if (++id >= maxData) {
            id = OID_NOT_FOUND;
            seglen = name.nstart - name.start;
            COPY_SEGMENT(name);
            size = seglen;
        } else {
            request_msg.index += name.nstart - name.start;
            
            GetOID(id, &response_msg.buffer[response_msg.index + 2], 
                   &response_msg.buffer[response_msg.index + 1]);
            
            seglen = response_msg.buffer[response_msg.index + 1] + 2;
            response_msg.index += seglen;
            size = seglen;
        }
    }
    
    ParseTLV(request_msg.buffer, request_msg.index, &value);
    
    if (id != OID_NOT_FOUND) {
        uint8_t dataType;
        uint8_t len;
        
        if ((reqType == GET_REQUEST) || (reqType == GET_NEXT_REQUEST)) {
            GetEntry(id, &dataType, &response_msg.buffer[response_msg.index + 2], &len);
            
            response_msg.buffer[response_msg.index] = dataType;
            response_msg.buffer[response_msg.index + 1] = len;
            seglen = (2 + len);
            response_msg.index += seglen;
            
            request_msg.index += (value.nstart - value.start);
        } else if (reqType == SET_REQUEST) {
            SetEntry(id, &request_msg.buffer[value.vstart], value.len, 
                    request_msg.buffer[value.start], index);
            seglen = value.nstart - value.start;
            COPY_SEGMENT(value);
        }
    } else {
        seglen = value.nstart - value.start;
        COPY_SEGMENT(value);
        
        errorIndex = index;
        errorStatus = NO_SUCH_NAME;
    }
    
    size += seglen;
    return size;
}

/**
 * @brief Parse sequence
 */
static int32_t ParseSequence(int32_t reqType, int32_t index) {
    int32_t seglen;
    TLVStruct_t seq;
    int32_t size = 0, respLoc;
    
    ParseTLV(request_msg.buffer, request_msg.index, &seq);
    
    if (request_msg.buffer[seq.start] != SNMP_SEQUENCE) {
        return -1;
    }
    
    seglen = seq.vstart - seq.start;
    respLoc = response_msg.index;
    COPY_SEGMENT(seq);
    
    size = ParseVarBind(reqType, index);
    InsertRespLen(seq.start, respLoc, size);
    size += seglen;
    
    return size;
}

/**
 * @brief Parse sequence of variable bindings
 */
static int32_t ParseSequenceOf(int32_t reqType) {
    int32_t seglen;
    TLVStruct_t seqof;
    int32_t size = 0, respLoc;
    int32_t index = 0;
    
    ParseTLV(request_msg.buffer, request_msg.index, &seqof);
    
    if (request_msg.buffer[seqof.start] != SNMP_SEQUENCE_OF) {
        return -1;
    }
    
    seglen = seqof.vstart - seqof.start;
    respLoc = response_msg.index;
    COPY_SEGMENT(seqof);
    
    while (request_msg.index < request_msg.len) {
        size += ParseSequence(reqType, index++);
    }
    
    InsertRespLen(seqof.start, respLoc, size);
    
    return size;
}

/**
 * @brief Parse SNMP request PDU
 */
static int32_t ParseRequest(void) {
    int32_t ret, seglen;
    TLVStruct_t snmpreq, requestid, errStatus, errIndex;
    int32_t size = 0, respLoc, reqType;
    
    ParseTLV(request_msg.buffer, request_msg.index, &snmpreq);
    
    reqType = request_msg.buffer[snmpreq.start];
    
    if (!VALID_REQUEST(reqType)) {
        return -1;
    }
    
    seglen = snmpreq.vstart - snmpreq.start;
    respLoc = snmpreq.start;
    size += seglen;
    COPY_SEGMENT(snmpreq);
    
    response_msg.buffer[snmpreq.start] = GET_RESPONSE;
    
    /* Insert the Request ID */
    ParseTLV(request_msg.buffer, request_msg.index, &requestid);
    seglen = requestid.nstart - requestid.start;
    size += seglen;
    COPY_SEGMENT(requestid);
    
    /* Insert the error status */
    ParseTLV(request_msg.buffer, request_msg.index, &errStatus);
    seglen = errStatus.nstart - errStatus.start;
    size += seglen;
    COPY_SEGMENT(errStatus);
    
    ParseTLV(request_msg.buffer, request_msg.index, &errIndex);
    seglen = errIndex.nstart - errIndex.start;
    size += seglen;
    COPY_SEGMENT(errIndex);
    
    ret = ParseSequenceOf(reqType);
    if (ret == -1) {
        return -1;
    } else {
        size += ret;
    }
    
    InsertRespLen(snmpreq.start, respLoc, size);
    
    if (errorStatus) {
        response_msg.buffer[errStatus.vstart] = errorStatus;
        response_msg.buffer[errIndex.vstart] = errorIndex + 1;
    }
    
    return size;
}

/**
 * @brief Parse community string
 */
static int16_t ParseCommunity(void) {
    int32_t seglen;
    TLVStruct_t community;
    int16_t size = 0;
    
    ParseTLV(request_msg.buffer, request_msg.index, &community);
    
    if (!((request_msg.buffer[community.start] == ASN_OCTETSTRING) && 
          (community.len == SNMP_COMMUNITY_SIZE))) {
        stats.bad_community++;
        return -1;
    }
    
    if (!memcmp(&request_msg.buffer[community.vstart], (int8_t *)SNMP_COMMUNITY, SNMP_COMMUNITY_SIZE)) {
        seglen = community.nstart - community.start;
        size += seglen;
        COPY_SEGMENT(community);
        size += ParseRequest();
    } else {
        stats.bad_community++;
        return -1;
    }
    
    return size;
}

/**
 * @brief Parse SNMP version
 */
static int16_t ParseVersion(void) {
    int16_t size = 0, seglen;
    TLVStruct_t tlv;
    
    ParseTLV(request_msg.buffer, request_msg.index, &tlv);
    
    if (!((request_msg.buffer[tlv.start] == ASN_INTEGER) && 
          (request_msg.buffer[tlv.vstart] == SNMP_V1))) {
        return -1;
    }
    
    seglen = tlv.nstart - tlv.start;
    size += seglen;
    COPY_SEGMENT(tlv);
    size = ParseCommunity();
    
    if (size == -1) {
        return size;
    } else {
        return (size + seglen);
    }
}

/**
 * @brief Parse complete SNMP message
 */
static int8_t ParseSNMP(void) {
    int16_t size = 0, seglen, respLoc;
    TLVStruct_t tlv;
    
    ParseTLV(request_msg.buffer, request_msg.index, &tlv);
    
    if (request_msg.buffer[tlv.start] != SNMP_SEQUENCE) {
        return -1;
    }
    
    seglen = tlv.vstart - tlv.start;
    respLoc = tlv.start;
    COPY_SEGMENT(tlv);
    
    size = ParseVersion();
    
    if (size == -1) {
        return -1;
    } else {
        size += seglen;
    }
    
    InsertRespLen(tlv.start, respLoc, size);
    
    return 0;
}

/**
 * @brief Process incoming SNMP message
 */
static void ProcessSNMPMessage(void) {
    int8_t ret;
    uint8_t buf[6];
    uint16_t idx = 0;
    uint16_t recv_len;
    
    /* Read UDP packet from W5500 - need to manually handle UDP header */
    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
    recv_len = W5500_GetRxSize(pw5500, W5500_SOCK_SNMP);
    if (recv_len > 0 && recv_len <= MAX_SNMPMSG_LEN) {
        /* Read the data including UDP header */
        W5500_RecvData(pw5500, W5500_SOCK_SNMP, request_msg.buffer, recv_len);
    }
    xSemaphoreGive(xSPI3Mutex);
    
    if (recv_len < 8 || recv_len > MAX_SNMPMSG_LEN) {
        return;  /* Packet too small or too large */
    }
    
    request_msg.len = recv_len;
    stats.requests_received++;
    
    /* Extract caller's IP and source port for reply (first 6 bytes of UDP header) */
    for (int i = 0; i < 6; i++) {
        buf[i] = request_msg.buffer[i];
    }
    
    /* Set destination IP and port */
    xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
    W5500_WriteData(pw5500, W5500_Sn_DIPR0, W5500_SOCKET_REG_BLOCK(W5500_SOCK_SNMP), buf, 4);      /* IP */
    W5500_WriteData(pw5500, W5500_Sn_DPORT0, W5500_SOCKET_REG_BLOCK(W5500_SOCK_SNMP), buf + 4, 2); /* Port */
    xSemaphoreGive(xSPI3Mutex);
    
    /* Remove 8-byte header and realign buffer */
    request_msg.len -= 8;
    while (idx < request_msg.len) {
        request_msg.buffer[idx] = request_msg.buffer[idx + 8];
        idx++;
    }
    
    /* Initialize message structures */
    request_msg.index = 0;
    response_msg.index = 0;
    errorStatus = errorIndex = 0;
    memset(response_msg.buffer, 0x00, MAX_SNMPMSG_LEN);
    
    /* Parse and process SNMP message */
    ret = ParseSNMP();
    
    if (ret > -1) {
        /* Send response */
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        W5500_SendData(pw5500, W5500_SOCK_SNMP, response_msg.buffer, response_msg.index);
        xSemaphoreGive(xSPI3Mutex);
        stats.responses_sent++;
    } else {
        stats.parse_errors++;
    }
}

/* Public Functions ----------------------------------------------------------*/

/**
 * @brief Initialize SNMP Task
 */
int SNMPTask_Init(W5500_Context_t *w5500_ctx) {
    /* Store W5500 context pointer */
    pw5500 = w5500_ctx;
    
    /* Reset statistics */
    memset(&stats, 0, sizeof(stats));
    
    /* Initialize message buffers */
    memset(&request_msg, 0, sizeof(request_msg));
    memset(&response_msg, 0, sizeof(response_msg));
    
    return 0;
}

/**
 * @brief SNMP Task main function
 */
void vSNMPTask(void *argument) {
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(100);  /* 100ms period */
    
    /* Initialize the xLastWakeTime variable with the current time */
    xLastWakeTime = xTaskGetTickCount();
    
    /* Task main loop */
    for (;;) {
        /* Wait for the next cycle */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        
        /* Check if SNMP socket has data */
        uint16_t recv_size = 0;
        
        xSemaphoreTake(xSPI3Mutex, portMAX_DELAY);
        recv_size = W5500_GetRxSize(pw5500, W5500_SOCK_SNMP);
        xSemaphoreGive(xSPI3Mutex);
        
        if (recv_size > 0) {
            /* Process SNMP message */
            ProcessSNMPMessage();
        }
    }
}

/**
 * @brief Get SNMP task statistics
 */
void SNMPTask_GetStats(SNMPStats_t *pstats) {
    if (pstats != NULL) {
        memcpy(pstats, &stats, sizeof(SNMPStats_t));
    }
}
