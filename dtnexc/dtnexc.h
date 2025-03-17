/**
 * dtnexc.h
 * Network Information Exchange Mechanism for exchanging DTN Contacts - C Implementation
 * Based on the DTNEX bash script by Samo Grasic (samo@grasic.net)
 */

#ifndef DTNEXC_H
#define DTNEXC_H

#include <bp.h>
#include <rfx.h>
#include <ion.h>
// Internal ION structures we need
#include "../ione-code/bpv7/library/bpP.h"
#include "../ione-code/ici/include/smlist.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <openssl/sha.h>

// Added for bpecho service
#include "../ione-code/ici/include/zco.h"
#include "../ione-code/ici/include/lyst.h"
#include "../ione-code/ici/include/platform.h"

// External functions from BP/ION library needed
extern Sdr getIonsdr();
extern IonVdb *getIonVdb();
extern PsmPartition getIonwm();

// External functions from libbpP.c needed for direct endpoint registration
extern int addEndpoint(char *endpointName, BpRecvRule recvAction, char *recvScript);
extern int updateEndpoint(char *endpointName, BpRecvRule recvAction, char *recvScript);
extern int removeEndpoint(char *endpointName);

// BPlan, BpDB, and other structures are imported from bpP.h

// Function prototypes for functions that might not be fully defined
char *strptime(const char *s, const char *format, struct tm *tm);

// Configuration constants
#define DEFAULT_UPDATE_INTERVAL 30
#define DEFAULT_CONTACT_LIFETIME 3600
#define DEFAULT_CONTACT_TIME_TOLERANCE 1800
#define DEFAULT_BUNDLE_TTL 3600
#define DEFAULT_SERVICE_NR 12160
#define DEFAULT_BPECHO_SERVICE_NR 12161
#define DEFAULT_PRESHARED_KEY "open"
#define MAX_HASH_CACHE 5000
#define MAX_METADATA_LENGTH 128  // Increased from 32 to handle longer metadata strings
#define MAX_EID_LENGTH 64
#define MAX_LINE_LENGTH 1024
#define MAX_PLANS 100
#define SHA256_DIGEST_SIZE 32

// For bpecho service
#define BPECHO_ADU_LEN 1024

// Structure definitions
typedef struct {
    int updateInterval;
    int contactLifetime;
    int contactTimeTolerance;
    int bundleTTL;
    char presSharedNetworkKey[64];
    char serviceNr[16];
    char bpechoServiceNr[16];
    unsigned long nodeId;  // Using unsigned long to store larger node IDs
    char nodemetadata[MAX_METADATA_LENGTH];
    int createGraph;
    char graphFile[256];
    int noMetadataExchange; // Flag to indicate if we should exchange our own metadata
} DtnexConfig;

typedef struct {
    unsigned long planId;  // Using unsigned long to store larger plan IDs
    time_t timestamp;
} Plan;

typedef struct {
    char hash[20];  // Store the first 10 characters of SHA-256 hash
    time_t timestamp;
} HashCache;

typedef struct {
    unsigned long nodeId;  // Using unsigned long to store larger node IDs
    char metadata[MAX_METADATA_LENGTH];
} NodeMetadata;

// No thread argument needed in the single-threaded version

// Bpecho service state structure
typedef struct {
    BpSAP sap;
    int running;
    ReqAttendant attendant;
} BpechoState;

// Function prototypes
void loadConfig(DtnexConfig *config);
int init(DtnexConfig *config);
void getplanlist(DtnexConfig *config, Plan *plans, int *planCount);
void exchangeWithNeighbors(DtnexConfig *config, Plan *plans, int planCount);
void processReceivedContacts(DtnexConfig *config, Plan *plans, int planCount);
void getContacts(DtnexConfig *config);
void createGraph(DtnexConfig *config);
void hashString(const char *input, char *output, const char *key);
int checkLine(char *line);
void signalHandler(int sig);
void processMessage(DtnexConfig *config, char *buffer);
void processContactMessage(DtnexConfig *config, const char *msgHash, const char *msgBuffer, time_t msgExpireTime, 
                         unsigned long msgOrigin, unsigned long msgSentFrom, unsigned long nodeA, unsigned long nodeB);
void processMetadataMessage(DtnexConfig *config, const char *msgHash, const char *msgBuffer, 
                          time_t msgExpireTime, unsigned long msgOrigin, unsigned long msgSentFrom);
void updateNodeMetadata(DtnexConfig *config, unsigned long nodeId, const char *metadata);
void forwardContactMessage(DtnexConfig *config, const char *msgHash, const char *msgType, 
                         time_t msgExpireTime, unsigned long msgOrigin, unsigned long msgSentFrom, 
                         unsigned long nodeA, unsigned long nodeB, Plan *plans, int planCount);
void checkForIncomingBundles(DtnexConfig *config);
void dtnex_log(const char *format, ...);

// Bpecho service functions
int initBpechoService(DtnexConfig *config, BpechoState *state);
void *runBpechoService(void *arg);
void handleBpechoQuit(int signum);

// Global variables
extern int running;
extern BpSAP sap;
extern Sdr sdr;

#endif // DTNEXC_H