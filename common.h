#ifndef COMMON_H
#define COMMON_H

// Max message size [cite: 43]
#define MAX_MSG_SIZE 500

// Server Limits
#define MAX_PEERS 1       // Implicitly 1 peer connection per server [cite: 109]
#define MAX_CLIENTS 15    // Each server handles up to 15 clients [cite: 110]

// --- MESSAGE CODES ---

// Mensagens de Controle [cite: 47]
#define REQ_CONNPEER 20
#define RES_CONNPEER 21
#define REQ_DISCPEER 22
#define REQ_CONNSEN  23
#define RES_CONNSEN  24
#define REQ_DISCSEN  25

// Mensagens de Dados [cite: 48]
#define REQ_CHECKALERT 36
#define RES_CHECKALERT 37
#define REQ_SENSLOC    38
#define RES_SENSLOC    39
// Note: REQ_SENSSTATUS and REQ_LOCLIST share code 40,
// RES_SENSSTATUS and RES_LOCLIST share code 41.
// The server type (SS or SL) will differentiate context.
#define REQ_SENSSTATUS 40 // To SS
#define REQ_LOCLIST    40 // To SL
#define RES_SENSSTATUS 41 // From SS (payload: LocID)
#define RES_LOCLIST    41 // From SL (payload: SenIDs)


// Mensagens de Erro ou Confirmação
#define MSG_OK    0   // [cite: 50]
#define MSG_ERROR 255 // [cite: 49]

// Códigos de Erro (payload para MSG_ERROR) [cite: 49]
#define ERR_PEER_LIMIT_EXCEEDED  1  // "01"
#define ERR_PEER_NOT_FOUND       2  // "02"
#define ERR_SENSOR_LIMIT_EXCEEDED 9  // "09"
#define ERR_SENSOR_NOT_FOUND     10 // "10"

// Códigos de Confirmação (payload para MSG_OK) [cite: 50]
#define OK_SUCCESSFUL_DISCONNECT 1 // "01"
#define OK_SUCCESSFUL_CREATE     2 // "02"
#define OK_SUCCESSFUL_UPDATE     3 // "03"


// Location definitions for areas (based on page 4) [cite: 34]
// Not strictly codes, but useful for logic/printing
// Example:
// #define AREA_NORTE_MIN 1
// #define AREA_NORTE_MAX 3
// ... and so on for Sul, Leste, Oeste.

#endif // COMMON_H