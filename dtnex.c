/**
 * dtnex.c
 * DTNEX - DTN Network Information Exchange
 * High-performance C implementation for exchanging DTN contact and metadata information
 * Author: Samo Grasic (samo@grasic.net)
 */

#include "dtnex.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>  // for execv

// Global variables
volatile int running = 1;
volatile int ionConnected = 0;  // Global ION connection status
volatile int ionRestartDetected = 0;  // Flag to trigger complete restart
static char **original_argv = NULL;  // Store original argv for restart
static int original_argc = 0;        // Store original argc for restart
BpSAP sap;
Sdr sdr;
HashCache hashCache[MAX_HASH_CACHE];
int hashCacheCount = 0;
NodeMetadata nodeMetadataList[MAX_PLANS];
int nodeMetadataCount = 0;
NonceCache nonceCache[MAX_HASH_CACHE]; // For CBOR replay protection
int nonceCacheCount = 0;
BpechoState bpechoState;   // Bpecho service state
BundleReceptionState bundleReceptionState;  // Bundle reception service state
pthread_t bpechoThread;    // Thread for bpecho service

/**
 * Logging helper function with color support
 * 
 * Color codes:
 * - Red (ERROR): \033[31m
 * - Green (SUCCESS/RECEIVED): \033[32m
 * - Yellow (SENT/WARNING): \033[33m
 * - Blue (FORWARDING): \033[34m
 * - Magenta (NEIGHBOR INFO): \033[35m
 * - Cyan (INFO/SYSTEM): \033[36m
 * - Bold: \033[1m
 * - Reset: \033[0m
 */
void dtnex_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

/**
 * Debug logging - only outputs if debug mode is enabled
 */
void debug_log(DtnexConfig *config, const char *format, ...) {
    if (!config || !config->debugMode) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    printf("\033[90m[DEBUG] ");  // Dark gray color
    vprintf(format, args);
    printf("\033[0m\n");  // Reset color
    va_end(args);
    fflush(stdout);
}

/**
 * Optimized message logging functions
 * Format: [TYPE] Origin→From→To: Contact(A↔B) | Metadata(Node:Name)
 */

// Yellow for sending messages
void log_message_sent(DtnexConfig *config, unsigned long origin, unsigned long to, const char *type, 
                     unsigned long nodeA, unsigned long nodeB, const char *metadata) {
    if (!config->debugMode) return;
    
    if (strcmp(type, "contact") == 0) {
        printf("\033[33m[SENT] Origin:%lu, Source:%lu, Dest:%lu: Contact(%lu↔%lu)\033[0m\n", 
               origin, config->nodeId, to, nodeA, nodeB);
    } else if (strcmp(type, "metadata") == 0) {
        printf("\033[33m[SENT] Origin:%lu, Source:%lu, Dest:%lu: Metadata(%lu:%s)\033[0m\n",
               origin, config->nodeId, to, nodeA, metadata ? metadata : "?");
    }
    fflush(stdout);
}

// Green for receiving messages  
void log_message_received(DtnexConfig *config, unsigned long origin, unsigned long from, const char *type,
                         unsigned long nodeA, unsigned long nodeB, const char *metadata) {
    if (!config->debugMode) return;
    
    if (strcmp(type, "contact") == 0) {
        printf("\033[32m[RECV] Origin:%lu, Source:%lu, Dest:%lu: Contact(%lu↔%lu)\033[0m\n",
               origin, from, config->nodeId, nodeA, nodeB);
    } else if (strcmp(type, "metadata") == 0) {
        printf("\033[32m[RECV] Origin:%lu, Source:%lu, Dest:%lu: Metadata(%lu:%s)\033[0m\n",
               origin, from, config->nodeId, nodeA, metadata ? metadata : "?");
    }
    fflush(stdout);
}

// Purple for forwarding messages
void log_message_forwarded(DtnexConfig *config, unsigned long origin, unsigned long from, unsigned long to,
                          const char *type, unsigned long nodeA, unsigned long nodeB, const char *metadata) {
    if (!config->debugMode) return;
    
    if (strcmp(type, "contact") == 0) {
        printf("\033[35m[FRWD] Origin:%lu, Source:%lu, Dest:%lu: Contact(%lu↔%lu)\033[0m\n",
               origin, from, to, nodeA, nodeB);
    } else if (strcmp(type, "metadata") == 0) {
        printf("\033[35m[FRWD] Origin:%lu, Source:%lu, Dest:%lu: Metadata(%lu:%s)\033[0m\n",
               origin, from, to, nodeA, metadata ? metadata : "?");
    }
    fflush(stdout);
}

// Gray for error/unknown messages (always shown even in normal mode)
void log_message_error(DtnexConfig *config, const char *error_msg) {
    printf("\033[90m[ERROR] %s\033[0m\n", error_msg);
    fflush(stdout);
}

// New contact graph updates (shown in both debug and normal mode)
void log_contact_update(DtnexConfig *config, int contactCount) {
    printf("\033[36m[UPDATE] Contact graph refreshed: %d active contacts\033[0m\n", contactCount);
    fflush(stdout);
}

/**
 * Loads configuration from dtnex.conf file
 * If file doesn't exist, use defaults (but will not exchange own metadata)
 */
void loadConfig(DtnexConfig *config) {
    // Set defaults
    config->updateInterval = DEFAULT_UPDATE_INTERVAL;
    config->contactLifetime = DEFAULT_CONTACT_LIFETIME;
    config->contactTimeTolerance = DEFAULT_CONTACT_TIME_TOLERANCE;
    config->bundleTTL = DEFAULT_BUNDLE_TTL;
    strcpy(config->presSharedNetworkKey, DEFAULT_PRESHARED_KEY);
    sprintf(config->serviceNr, "%d", DEFAULT_SERVICE_NR);
    sprintf(config->bpechoServiceNr, "%d", DEFAULT_BPECHO_SERVICE_NR);
    config->nodeId = 0;
    memset(config->nodemetadata, 0, MAX_METADATA_LENGTH);
    config->createGraph = 0;
    strcpy(config->graphFile, "contactGraph.png");
    config->noMetadataExchange = 1; // Default to not exchanging own metadata if no config file
    config->debugMode = 0; // Default to no debug output
    config->serviceMode = 0; // Default to interactive mode
    config->gpsLatitude = 0.0;
    config->gpsLongitude = 0.0;
    config->hasGpsCoordinates = 0;

    // Try to read from config file
    FILE *configFile = fopen("dtnex.conf", "r");
    if (configFile) {
        // Config file exists, disable the no-metadata-exchange flag
        config->noMetadataExchange = 0;
        
        char line[MAX_LINE_LENGTH];
        while (fgets(line, MAX_LINE_LENGTH, configFile)) {
            char *ptr = line;
            // Skip whitespace
            while(*ptr && isspace(*ptr)) ptr++;
            // Skip comments and empty lines
            if (*ptr == '#' || *ptr == '\0') continue;

            // Parse key=value pairs
            char *key = strtok(ptr, "=");
            char *value = strtok(NULL, "\n");
            if (key && value) {
                // Remove whitespace
                char *end = key + strlen(key) - 1;
                while(end > key && isspace(*end)) *end-- = '\0';
                
                while(*value && isspace(*value)) value++;
                
                // Remove comments from value (anything after #)
                char *comment = strchr(value, '#');
                if (comment) {
                    *comment = '\0';  // Terminate the string at the comment
                }
                
                // Remove trailing whitespace
                end = value + strlen(value) - 1;
                while(end > value && isspace(*end)) *end-- = '\0';

                // Remove quotes if present
                if (*value == '"' && value[strlen(value)-1] == '"') {
                    value[strlen(value)-1] = '\0';
                    value++;
                }

                // Assign values based on key
                if (strcmp(key, "updateInterval") == 0) {
                    config->updateInterval = atoi(value);
                } else if (strcmp(key, "contactLifetime") == 0) {
                    config->contactLifetime = atoi(value);
                } else if (strcmp(key, "contactTimeTolerance") == 0) {
                    config->contactTimeTolerance = atoi(value);
                } else if (strcmp(key, "bundleTTL") == 0) {
                    config->bundleTTL = atoi(value);
                } else if (strcmp(key, "presSharedNetworkKey") == 0) {
                    strncpy(config->presSharedNetworkKey, value, sizeof(config->presSharedNetworkKey) - 1);
                } else if (strcmp(key, "serviceNr") == 0) {
                    strncpy(config->serviceNr, value, sizeof(config->serviceNr) - 1);
                } else if (strcmp(key, "bpechoServiceNr") == 0) {
                    strncpy(config->bpechoServiceNr, value, sizeof(config->bpechoServiceNr) - 1);
                } else if (strcmp(key, "nodemetadata") == 0) {
                    strncpy(config->nodemetadata, value, sizeof(config->nodemetadata) - 1);
                } else if (strcmp(key, "createGraph") == 0) {
                    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                        config->createGraph = 1;
                    }
                } else if (strcmp(key, "graphFile") == 0) {
                    strncpy(config->graphFile, value, sizeof(config->graphFile) - 1);
                } else if (strcmp(key, "noMetadataExchange") == 0) {
                    if (strcmp(value, "true") == 0) {
                        config->noMetadataExchange = 1;
                    }
                } else if (strcmp(key, "debugMode") == 0) {
                    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                        config->debugMode = 1;
                    } else {
                        config->debugMode = 0;
                    }
                } else if (strcmp(key, "serviceMode") == 0) {
                    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                        config->serviceMode = 1;
                    } else {
                        config->serviceMode = 0;
                    }
                } else if (strcmp(key, "gpsLatitude") == 0) {
                    config->gpsLatitude = atof(value);
                    config->hasGpsCoordinates = 1;
                } else if (strcmp(key, "gpsLongitude") == 0) {
                    config->gpsLongitude = atof(value);
                    config->hasGpsCoordinates = 1;
                }
            }
        }
        fclose(configFile);
        dtnex_log("Configuration loaded from dtnex.conf");
    } else {
        dtnex_log("No dtnex.conf found, using f settings (no metadata exchange)");
    }
}


// No global endpoint status needed in the single-threaded version

/**
 * Try to connect to ION - returns 0 on success, -1 on failure
 * This function handles all ION connection logic cleanly
 */
int tryConnectToIon(DtnexConfig *config) {
    char endpointId[MAX_EID_LENGTH];
    
    // Try to attach to ION BP system
    if (bp_attach() < 0) {
        return -1;
    }
    
    // Get the node ID from ION configuration
    Sdr ionsdr = getIonsdr();
    if (ionsdr == NULL) {
        bp_detach();
        return -1;
    }
    
    // Start transaction to safely access ION configuration
    if (sdr_begin_xn(ionsdr) < 0) {
        bp_detach();
        return -1;
    }
    
    // Get the node number from ION configuration
    IonDB iondb;
    Object iondbObject = getIonDbObject();
    if (iondbObject == 0) {
        sdr_exit_xn(ionsdr);
        bp_detach();
        return -1;
    }
    
    // Read the iondb object to get the node number
    sdr_read(ionsdr, (char *) &iondb, iondbObject, sizeof(IonDB));
    config->nodeId = iondb.ownNodeNbr;
    sdr_exit_xn(ionsdr);
    
    if (config->nodeId == 0) {
        bp_detach();
        return -1;
    }
    
    dtnex_log("Using node ID: %lu detected from ION configuration", config->nodeId);
    
    // Construct endpoint ID
    snprintf(endpointId, MAX_EID_LENGTH, "ipn:%lu.%s", config->nodeId, config->serviceNr);
    dtnex_log("Using endpoint: %s", endpointId);
    
    // Get the SDR
    sdr = bp_get_sdr();
    if (sdr == NULL) {
        bp_detach();
        return -1;
    }
    
    // First try to add/register the endpoint in ION's routing database
    if (addEndpoint(endpointId, EnqueueBundle, NULL) < 0) {
        debug_log(config, "Warning: Could not register endpoint %s in routing database", endpointId);
    }
    
    // Try to open the endpoint for receiving messages
    if (bp_open(endpointId, &sap) < 0) {
        bp_detach();
        return -1;
    }
    
    dtnex_log("✅ Endpoint opened successfully: %s", endpointId);
    
    // If metadata exchange is enabled, add our own metadata first
    if (!config->noMetadataExchange && strlen(config->nodemetadata) > 0) {
        StructuredMetadata metadata;
        char ownMetadata[MAX_METADATA_LENGTH];
        
        // Parse our own metadata
        parseNodeMetadata(config->nodemetadata, &metadata);
        
        // Create metadata string with GPS if available
        if (config->hasGpsCoordinates) {
            snprintf(ownMetadata, sizeof(ownMetadata), "%s,%s,%.6f,%.6f", 
                    metadata.name, metadata.contact, 
                    config->gpsLatitude, config->gpsLongitude);
        } else {
            snprintf(ownMetadata, sizeof(ownMetadata), "%s,%s", 
                    metadata.name, metadata.contact);
        }
        
        // Add as first entry
        updateNodeMetadata(config, config->nodeId, ownMetadata);
        dtnex_log("✅ Added own node metadata: %s", ownMetadata);
    }
    
    return 0;
}

/**
 * Initialize the DTNEX application - Modified to work without requiring ION connection
 */
int init(DtnexConfig *config) {
    dtnex_log("Starting DTNEXC v%s (built %s %s), author: Samo Grasic (samo@grasic.net)", 
              DTNEXC_VERSION, DTNEXC_BUILD_DATE, DTNEXC_BUILD_TIME);
    
    // Try to connect to ION, but don't fail if unavailable
    if (tryConnectToIon(config) == 0) {
        ionConnected = 1;
        dtnex_log("✅ Successfully connected to ION");
    } else {
        ionConnected = 0;
        dtnex_log("⚠️ ION not available - will retry every minute");
        // Set default node ID for service mode
        config->nodeId = 0;
    }
    
    // Initialize service configuration
    strcpy(config->serviceNr, "12160");
    strcpy(config->bpechoServiceNr, "12161");
    
    dtnex_log("DTNEXC initialized successfully");
    return 0;
}

/**
 * Get the list of plans (neighbor nodes) directly from ION using ION API
 * Based on ipnadmin's listPlans function
 */
void getplanlist(DtnexConfig *config, Plan *plans, int *planCount) {
    time_t currentTime;
    static Plan cachedPlans[MAX_PLANS];
    static int cachedPlanCount = 0;
    static time_t lastPlanUpdate = 0;
    
    // ION API related variables
    Sdr sdr;
    BpDB *bpConstants;
    
    // Initialize the plan count to 0
    *planCount = 0;
    
    // Get current time
    time(&currentTime);
    
    // Check if we've updated plans recently - if so, use cached results to avoid 
    // constant calls to ION API which can be expensive
    if (lastPlanUpdate > 0 && (currentTime - lastPlanUpdate) < 20) {
        // Copy the cached plans to the output
        for (int i = 0; i < cachedPlanCount; i++) {
            plans[i] = cachedPlans[i];
        }
        *planCount = cachedPlanCount;
        
        dtnex_log("Using cached plan list (age: %ld seconds)", currentTime - lastPlanUpdate);
        return;
    }
    
    dtnex_log("Getting a fresh list of neighbors from ION...");
    
    // Get the SDR database
    sdr = getIonsdr();
    if (sdr == NULL) {
        dtnex_log("Error: can't get ION SDR");
        
        // Fallback: use previously cached plans if available
        if (cachedPlanCount > 0) {
            for (int i = 0; i < cachedPlanCount; i++) {
                plans[i] = cachedPlans[i];
            }
            *planCount = cachedPlanCount;
            dtnex_log("Using %d plans from cache (fallback)", *planCount);
        }
        return;
    }
    
    // Start a transaction
    if (sdr_begin_xn(sdr) < 0) {
        dtnex_log("Error: can't begin SDR transaction");
        return;
    }
    
    // Get the BP constants
    bpConstants = getBpConstants();
    if (bpConstants == NULL) {
        dtnex_log("Error: can't get BP constants");
        sdr_exit_xn(sdr);
        return;
    }
    
    // Get the list of plans and iterate through it with safe access
    Object planElt = 0;
    for (planElt = sdr_list_first(sdr, bpConstants->plans); 
         planElt && planElt != 0; 
         planElt = sdr_list_next(sdr, planElt)) {
        
        // Get the plan data with careful error checking
        Object planData = sdr_list_data(sdr, planElt);
        if (planData == 0) {
            dtnex_log("Warning: Null plan data, skipping");
            continue;
        }
        
        // Get the plan object - cast to BpPlan* with proper error checking
        BpPlan *plan = (BpPlan*) sdr_pointer(sdr, planData);
        if (plan == NULL) {
            dtnex_log("Warning: Null plan pointer, skipping");
            continue;
        }
        
        // Only include plans with a valid neighbor node number (CBHE-compliant)
        if (plan->neighborNodeNbr == 0) {
            continue;
        }
        
        // Skip our own node
        if (plan->neighborNodeNbr == config->nodeId) {
            continue;
        }
        
        // No verbose output for each plan found
        
        // Add this plan to our lists (both the output and the cache)
        if (*planCount < MAX_PLANS) {
            plans[*planCount].planId = plan->neighborNodeNbr;
            time(&plans[*planCount].timestamp);
            
            // Also update the cache
            cachedPlans[*planCount].planId = plan->neighborNodeNbr;
            cachedPlans[*planCount].timestamp = plans[*planCount].timestamp;
            
            (*planCount)++;
        } else {
            dtnex_log("Warning: Plan list is full (%d entries), skipping additional plans", MAX_PLANS);
            break; // Stop processing if we hit the limit
        }
    }
    
    // End the transaction
    sdr_exit_xn(sdr);
    
    // Update the cached plan count
    cachedPlanCount = *planCount;
    
    // Update timestamp of last plan update
    lastPlanUpdate = currentTime;
    
    dtnex_log("\033[35mList of configured plans:\033[0m");  // Magenta color like tput setaf 5
    for (int i = 0; i < *planCount; i++) {
        dtnex_log(">%lu", plans[i].planId);
    }
    
    dtnex_log("%d neighbors found in ION configuration", *planCount);
}

/**
 * Send a bundle using ION BP functions - following bpsource.c pattern
 */

/**
 * Exchange contact information with neighbors
 * Only perform exchange every 30 minutes (1800 seconds) or if plan list changes
 */
/**
 * Exchange CBOR-encoded contact and metadata messages with neighbor nodes
 * Pure CBOR implementation - no string format support
 */
void exchangeWithNeighbors(DtnexConfig *config, Plan *plans, int planCount) {
    int i, j;
    time_t currentTime, expireTime;
    char destEid[MAX_EID_LENGTH];
    unsigned char cborBuffer[MAX_CBOR_BUFFER];
    int messageSize;
    static time_t lastExchangeTime = 0;
    static int lastPlanCount = 0;
    static unsigned long lastPlanList[MAX_PLANS];
    int planListChanged = 0;
    
    // Get current time
    time(&currentTime);
    
    // Check if we need to perform exchange (using updateInterval instead of hardcoded 1800)
    // 1. First time (lastExchangeTime == 0)
    // 2. Update interval has passed
    // 3. Plan list has changed
    
    // Check if plan list has changed
    if (planCount != lastPlanCount) {
        planListChanged = 1;
    } else {
        for (i = 0; i < planCount; i++) {
            int found = 0;
            for (j = 0; j < lastPlanCount; j++) {
                if (plans[i].planId == lastPlanList[j]) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                planListChanged = 1;
                break;
            }
        }
    }
    
    // Determine if we should exchange now (use config updateInterval)
    if (lastExchangeTime == 0 || (currentTime - lastExchangeTime) >= config->updateInterval || planListChanged) {
        dtnex_log("📤 Exchanging CBOR contact information with %d neighbors...", planCount);
        
        // Update last exchange time
        lastExchangeTime = currentTime;
        
        // Save current plan list for next comparison
        lastPlanCount = planCount;
        for (i = 0; i < planCount && i < MAX_PLANS; i++) {
            lastPlanList[i] = plans[i].planId;
        }
        
        // Calculate expire time
        expireTime = currentTime + config->contactLifetime + config->contactTimeTolerance;
        
        // Send CBOR contact information to all neighbors
        for (i = 0; i < planCount; i++) {
            for (j = 0; j < planCount; j++) {
                unsigned long targetPlan = plans[i].planId;
                unsigned long neighborId = plans[j].planId;
                
                // Skip local loopback plan
                if (neighborId == config->nodeId) {
                    continue;
                }
                
                // Create contact info structure
                ContactInfo contact;
                contact.nodeA = config->nodeId;
                contact.nodeB = targetPlan;
                contact.duration = config->contactLifetime / 60; // Convert seconds to minutes
                
                // Encode contact message to CBOR
                messageSize = encodeCborContactMessage(config, &contact, cborBuffer, MAX_CBOR_BUFFER);
                if (messageSize > 0) {
                    sprintf(destEid, "ipn:%lu.%s", neighborId, config->serviceNr);
                    
                    // Send CBOR bundle
                    sendCborBundle(destEid, cborBuffer, messageSize, config->bundleTTL);
                    
                    // Log optimized sending message
                    log_message_sent(config, config->nodeId, neighborId, "contact", 
                                   contact.nodeA, contact.nodeB, NULL);
                } else {
                    dtnex_log("❌ Failed to encode CBOR contact message for %lu↔%lu", 
                        config->nodeId, targetPlan);
                }
            }
        }
        
        // Send CBOR metadata to neighbors (if enabled)
        if (!config->noMetadataExchange && strlen(config->nodemetadata) > 0) {
            dtnex_log("📤 Exchanging CBOR metadata with neighbors...");
            
            for (i = 0; i < planCount; i++) {
                unsigned long neighborId = plans[i].planId;
                
                // Skip local loopback plan
                if (neighborId == config->nodeId) {
                    continue;
                }
                
                // Create metadata structure
                StructuredMetadata metadata;
                metadata.nodeId = config->nodeId;
                parseNodeMetadata(config->nodemetadata, &metadata);
                
                // Add GPS coordinates if available
                if (config->hasGpsCoordinates) {
                    metadata.latitude = (int)(config->gpsLatitude * GPS_PRECISION_FACTOR);
                    metadata.longitude = (int)(config->gpsLongitude * GPS_PRECISION_FACTOR);
                } else {
                    metadata.latitude = 0;
                    metadata.longitude = 0;
                }
                
                // Encode metadata message to CBOR
                messageSize = encodeCborMetadataMessage(config, &metadata, cborBuffer, MAX_CBOR_BUFFER);
                if (messageSize > 0) {
                    sprintf(destEid, "ipn:%lu.%s", neighborId, config->serviceNr);
                    
                    // Send CBOR bundle
                    sendCborBundle(destEid, cborBuffer, messageSize, config->bundleTTL);
                    
                    // Log optimized metadata sending message
                    log_message_sent(config, config->nodeId, neighborId, "metadata", 
                                   metadata.nodeId, 0, metadata.name);
                } else {
                    dtnex_log("❌ Failed to encode CBOR metadata message for node %lu", config->nodeId);
                }
            }
        } else if (config->noMetadataExchange) {
            if (config->debugMode) {
                dtnex_log("📤 Metadata exchange disabled in configuration");
            }
        }
        
        // CBOR exchange completed - no file I/O needed
    } else {
        // Calculate remaining time until next exchange  
        int remainingTime = config->updateInterval - (currentTime - lastExchangeTime);
        dtnex_log("Skipping neighbor exchange (next in %d seconds)", remainingTime);
    }
}

/**
 * Forward a received contact message to other neighbors
 */


/**
 * Process a contact message type "c"
 */


/**
 * Update node metadata in memory and optionally in file
 */
void updateNodeMetadata(DtnexConfig *config, unsigned long nodeId, const char *metadata) {
    int i;
    int nodeFound = 0;
    
    // First check if we already have metadata for this node
    for (i = 0; i < nodeMetadataCount; i++) {
        if (nodeMetadataList[i].nodeId == nodeId) {
            // Update existing entry
            strncpy(nodeMetadataList[i].metadata, metadata, sizeof(nodeMetadataList[i].metadata) - 1);
            nodeMetadataList[i].metadata[sizeof(nodeMetadataList[i].metadata) - 1] = '\0';
            nodeFound = 1;
            debug_log(config, "[INFO] Updated metadata for node %lu: \"%s\"", 
                      nodeId, nodeMetadataList[i].metadata);
            break;
        }
    }
    
    // If not found, add new entry
    if (!nodeFound && nodeMetadataCount < MAX_PLANS) {
        nodeMetadataList[nodeMetadataCount].nodeId = nodeId;
        strncpy(nodeMetadataList[nodeMetadataCount].metadata, metadata, 
                sizeof(nodeMetadataList[nodeMetadataCount].metadata) - 1);
        nodeMetadataList[nodeMetadataCount].metadata[sizeof(nodeMetadataList[nodeMetadataCount].metadata) - 1] = '\0';
        debug_log(config, "[INFO] Added new metadata for node %lu: \"%s\"", 
                  nodeId, nodeMetadataList[nodeMetadataCount].metadata);
        nodeMetadataCount++;
    }
    
    // Update the metadata file if graph generation is enabled
    if (config->createGraph) {
        FILE *metadataFile = fopen("nodesmetadata.txt", "w");
        if (metadataFile) {
            for (i = 0; i < nodeMetadataCount; i++) {
                fprintf(metadataFile, "%lu:%s\n", nodeMetadataList[i].nodeId, nodeMetadataList[i].metadata);
            }
            fclose(metadataFile);
            debug_log(config, "[INFO] Updated nodesmetadata.txt for graph generation");
        }
    }
    
    // Print a complete list of all metadata after receiving and processing (debug mode only)
    debug_log(config, "======== COLLECTED NODE METADATA (%d nodes) ========", nodeMetadataCount);
    debug_log(config, "NODE ID    | METADATA");
    debug_log(config, "----------------------------------------");
    for (i = 0; i < nodeMetadataCount; i++) {
        debug_log(config, "%-10lu | %s", 
                nodeMetadataList[i].nodeId, nodeMetadataList[i].metadata);
    }
    debug_log(config, "========================================");
}

/**
 * Process one received message by extracting the basic information,
 * then calling the appropriate specialized message handler.
 */

/**
 * Check for incoming bundles - Non-blocking implementation
 * Based on bpsink.c but modified to be non-blocking and integrated with main loop
 */

/**
 * Signal handler for clean shutdown
 * Based on bpsink's handleQuit pattern
 */
void signalHandler(int sig) {
    // Re-arm signal handlers in case we receive multiple signals
    isignal(SIGINT, signalHandler);
    isignal(SIGTERM, signalHandler);
    isignal(SIGTSTP, signalHandler);
    
    // Prevent re-entrancy (in case signal is received again during shutdown)
    static int inShutdown = 0;
    if (inShutdown) {
        dtnex_log("Already in shutdown process, forcing immediate exit...");
        exit(1);  // Force exit if shutdown takes too long
    }
    inShutdown = 1;
    
    // Provide feedback based on signal type
    if (sig == SIGINT) {
        dtnex_log("Received interrupt signal (Ctrl+C), shutting down gracefully...");
    } else if (sig == SIGTERM) {
        dtnex_log("Received termination signal, shutting down gracefully...");
    } else if (sig == SIGTSTP) {
        dtnex_log("Received suspend signal (Ctrl+Z), shutting down gracefully instead of suspending...");
    } else {
        dtnex_log("Received signal %d, shutting down gracefully...", sig);
    }
    
    // Set global flag to stop the main loop
    running = 0;
    
    // Only perform ION cleanup if we're actually connected to ION
    if (ionConnected && sap != NULL) {
        // Interrupt any pending receives if we have an open endpoint
        dtnex_log("Interrupting BP endpoint");
        bp_interrupt(sap);
        
        // Stop bundle reception service
        stopBundleReception(&bundleReceptionState);
        
        // Stop bpecho service
        bpechoState.running = 0;
        if (bpechoState.sap != NULL) {
            bp_interrupt(bpechoState.sap);
            ionPauseAttendant(&bpechoState.attendant);
        }
        
        // Force cleanup and exit for all signals since main loop might be blocked
        dtnex_log("Performing cleanup and immediate exit...");
        
        // Close endpoints directly
        dtnex_log("🔌 Closing BP endpoint");
        bp_close(sap);
        sap = NULL;
        
        // Close bpecho endpoint if it exists
        if (bpechoState.sap != NULL) {
            bp_close(bpechoState.sap);
            bpechoState.sap = NULL;
        }
        
        // Detach from BP
        dtnex_log("🧹 Detaching from ION BP system");
        bp_detach();
    } else {
        dtnex_log("Performing cleanup without ION detachment (not connected)...");
        // Reset states even if not connected to ION
        bpechoState.running = 0;
        bundleReceptionState.running = 0;
    }
    
    dtnex_log("DTNEXC shutdown complete");
    exit(0);
}

/**
 * Display the current contact graph by accessing ION's contact plan directly
 * Based EXACTLY on the ionadmin's listContacts function but with prettier formatting
 */
void getContacts(DtnexConfig *config) {
    Sdr sdr;
    IonVdb *ionvdb;
    PsmPartition ionwm;
    PsmAddress elt;    // For traversing the red-black tree
    PsmAddress addr;
    time_t currentTime;
    IonCXref *contact;
    int contactCount = 0;
    
    // Only show detailed table in debug mode
    if (config->debugMode) {
        // Header for contact plan table
        dtnex_log("\033[36m%-12s %-12s %-20s %-20s %-15s %-12s\033[0m",
                "FROM NODE", "TO NODE", "START TIME", "END TIME", "DURATION", "STATUS");
        dtnex_log("\033[36m-----------------------------------------------------------------------\033[0m");
    }
    
    // Get the SDR database
    sdr = getIonsdr();
    if (sdr == NULL) {
        dtnex_log("⚠️  Cannot access ION SDR - ION may have been restarted");
        dtnex_log("🔄 Attempting to reinitialize ION connection...");
        
        // Close current SAP if it exists
        if (sap != NULL) {
            bp_close(sap);
            sap = NULL;
        }
        
        // Mark as disconnected
        ionConnected = 0;
        
        // ION restart detected - completely restart DTNEX
        restartDtnex(config);
        return;
    }
    
    // Get current time
    time(&currentTime);
    
    // Start transaction for memory safety
    if (sdr_begin_xn(sdr) < 0) {
        dtnex_log("⚠️  Cannot start SDR transaction - ION may have been restarted");
        dtnex_log("🔄 Attempting to reinitialize ION connection...");
        
        // Close current SAP if it exists
        if (sap != NULL) {
            bp_close(sap);
            sap = NULL;
        }
        
        // Mark as disconnected
        ionConnected = 0;
        
        // ION restart detected - completely restart DTNEX
        restartDtnex(config);
        return;
    }
    
    // Get ion volatile database
    ionvdb = getIonVdb();
    if (ionvdb == NULL) {
        dtnex_log("⚠️  Cannot access ION volatile database - ION may have been restarted");
        sdr_exit_xn(sdr);
        
        // Close current SAP if it exists
        if (sap != NULL) {
            bp_close(sap);
            sap = NULL;
        }
        
        // Mark as disconnected
        ionConnected = 0;
        
        // ION restart detected - completely restart DTNEX
        restartDtnex(config);
        return;
    }
    
    // Get the working memory
    ionwm = getIonwm();
    if (ionwm == NULL) {
        dtnex_log("⚠️  Cannot access ION working memory - ION may have been restarted");
        sdr_exit_xn(sdr);
        
        // Close current SAP if it exists
        if (sap != NULL) {
            bp_close(sap);
            sap = NULL;
        }
        
        // Mark as disconnected
        ionConnected = 0;
        
        // ION restart detected - completely restart DTNEX
        restartDtnex(config);
        return;
    }
    
    // Check if contact index is initialized
    if (ionvdb->contactIndex == 0) {
        dtnex_log("Contact index not initialized");
        sdr_exit_xn(sdr);
        return;
    }

    /* 
     * This is the EXACT pattern from ionadmin.c for traversing contacts
     * using the red-black tree in ION's contact database.
     */
    for (elt = sm_rbt_first(ionwm, ionvdb->contactIndex); 
         elt; 
         elt = sm_rbt_next(ionwm, elt)) {
        addr = sm_rbt_data(ionwm, elt);
        if (addr == 0) {
            continue;  // Skip invalid addresses
        }
        
        contact = (IonCXref *) psp(ionwm, addr);
        if (contact == NULL) {
            continue;  // Skip NULL contacts
        }
        
        // Calculate time remaining and format duration in a readable way
        time_t timediff = contact->toTime - currentTime;
        char durationStr[20];
        
        // Format duration based on size for better readability
        if (timediff > 86400) { // More than a day
            snprintf(durationStr, sizeof(durationStr), "%.1f days", timediff / 86400.0);
        } else if (timediff > 3600) { // More than an hour
            snprintf(durationStr, sizeof(durationStr), "%.1f hours", timediff / 3600.0);
        } else if (timediff > 60) { // More than a minute
            snprintf(durationStr, sizeof(durationStr), "%.1f minutes", timediff / 60.0);
        } else {
            snprintf(durationStr, sizeof(durationStr), "%ld seconds", timediff);
        }
        
        // Convert UNIX timestamps to human readable format
        char startTimeStr[25], endTimeStr[25];
        struct tm *timeinfo;
        
        timeinfo = localtime(&contact->fromTime);
        strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        timeinfo = localtime(&contact->toTime);
        strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        // Determine if contact is active now
        const char* status = (contact->fromTime <= currentTime && currentTime <= contact->toTime) ? 
                             "\033[32mACTIVE\033[0m" : "\033[33mFUTURE\033[0m";
        
        // Only show detailed contact info in debug mode
        if (config->debugMode) {
            // Format and output the contact information in a table row
            dtnex_log("%-12lu %-12lu %-20s %-20s %-15s %s",
                    (unsigned long)contact->fromNode, 
                    (unsigned long)contact->toNode,
                    startTimeStr,
                    endTimeStr,
                    durationStr,
                    status);
        }
        
        contactCount++;
    }
    
    // End the transaction
    sdr_exit_xn(sdr);
    
    // Check if ION might have been restarted (no contacts found)
    if (contactCount == 0) {
        dtnex_log("⚠️  No contacts found - ION may have been restarted");
        // ION restart detected - completely restart DTNEX
        restartDtnex(config);
    }
    
    if (config->debugMode) {
        // Show detailed summary in debug mode
        if (contactCount == 0) {
            dtnex_log("No contacts found in ION database");
        } else {
            dtnex_log("\033[36m-----------------------------------------------------------------------\033[0m");
            dtnex_log("Total contacts: %d", contactCount);
        }
    } else {
        // Show simple update in normal mode
        log_contact_update(config, contactCount);
    }
    
    // Generate graph after every contact printout as requested
    if (config->createGraph) {
        createGraph(config);
    }
}

/**
 * Generate a graph visualization file (Graphviz .gv format)
 * Based on creategraph function in dtnex.sh
 */
void createGraph(DtnexConfig *config) {
    int i;
    time_t currentTime;
    FILE *graphFile;
    
    if (!config->createGraph) {
        // This check is redundant as the main loop already checks, but kept for safety
        return;
    }
    
    // Use the exact file path from config - don't automatically append .gv
    char graphvizFile[256];
    strncpy(graphvizFile, config->graphFile, sizeof(graphvizFile) - 1);
    graphvizFile[sizeof(graphvizFile) - 1] = '\0';
    
    graphFile = fopen(graphvizFile, "w");
    if (!graphFile) {
        dtnex_log("Failed to open graph file for writing: %s", graphvizFile);
        return;
    }
    
    // Write the comment header with instructions
    fprintf(graphFile, "// DTN Contact Graph generated by DTNEXC\n");
    fprintf(graphFile, "// To generate an image from this file, run:\n");
    fprintf(graphFile, "// dot -Tpng %s -o %s\n", graphvizFile, config->graphFile);
    fprintf(graphFile, "// You can also use other formats like: -Tsvg, -Tpdf, -Tjpg\n\n");
    
    // Write the graph header (bash-compatible format)
    fprintf(graphFile, "digraph G { layout=neato; overlap=false;\n");
    
    // Add nodes to the graph from in-memory metadata list
    // Note: The nodeMetadataList is kept in memory and only written to nodesmetadata.txt
    // when createGraph is true, so we rely on the in-memory list here
    for (i = 0; i < nodeMetadataCount; i++) {
        // Prepare metadata string (escape special chars)
        char escapedMetadata[MAX_METADATA_LENGTH * 2] = {0};
        char *metadata = nodeMetadataList[i].metadata;
        char *dst = escapedMetadata;
        
        // Escape special characters
        while (*metadata) {
            if (*metadata == '@') {
                *dst++ = '&'; *dst++ = '#'; *dst++ = '6'; *dst++ = '4'; *dst++ = ';';
            }
            else if (*metadata == '.') {
                *dst++ = '&'; *dst++ = '#'; *dst++ = '4'; *dst++ = '6'; *dst++ = ';';
            }
            else if (*metadata == ',') {
                *dst++ = '<'; *dst++ = 'b'; *dst++ = 'r'; *dst++ = '/'; *dst++ = '>';
            }
            else {
                *dst++ = *metadata;
            }
            metadata++;
        }
        *dst = '\0';
        
        // Add the node with bash-compatible styling
        fprintf(graphFile, "\"ipn:%lu\" [label=< <FONT POINT-SIZE=\"14\" FACE=\"Arial\" COLOR=\"darkred\"><B>ipn:%lu</B></FONT><BR/><FONT POINT-SIZE=\"10\" FACE=\"Arial\" COLOR=\"blue\">%s</FONT>>];\n", 
                nodeMetadataList[i].nodeId, nodeMetadataList[i].nodeId, escapedMetadata);
    }
    
    // Add self node
    char escapedMetadata[MAX_METADATA_LENGTH * 2] = {0};
    char *metadata = config->nodemetadata;
    char *dst = escapedMetadata;
    
    // Escape special characters
    while (*metadata) {
        if (*metadata == '@') {
            *dst++ = '&'; *dst++ = '#'; *dst++ = '6'; *dst++ = '4'; *dst++ = ';';
        }
        else if (*metadata == '.') {
            *dst++ = '&'; *dst++ = '#'; *dst++ = '4'; *dst++ = '6'; *dst++ = ';';
        }
        else if (*metadata == ',') {
            *dst++ = '<'; *dst++ = 'b'; *dst++ = 'r'; *dst++ = '/'; *dst++ = '>';
        }
        else {
            *dst++ = *metadata;
        }
        metadata++;
    }
    *dst = '\0';
    
    // Add local node with bash-compatible formatting
    fprintf(graphFile, "\"ipn:%lu\" [label=< <FONT POINT-SIZE=\"14\" FACE=\"Arial\" COLOR=\"darkred\"><B>ipn:%lu</B></FONT><BR/><FONT POINT-SIZE=\"10\" FACE=\"Arial\" COLOR=\"blue\">%s</FONT>>];\n", 
            config->nodeId, config->nodeId, escapedMetadata);
    
    // Get contacts using ionadmin command (same as bash version)
    int contactCount = 0;
    debug_log(config, "Extracting contacts using ionadmin command...");
    
    FILE *ionadmin_pipe = popen("echo 'l contact' | ionadmin 2>/dev/null | grep -o -P '(?<=From).*?(?=is)'", "r");
    if (ionadmin_pipe) {
        char line[1024];
        while (fgets(line, sizeof(line), ionadmin_pipe) != NULL) {
            // Remove trailing newline
            line[strcspn(line, "\n")] = 0;
            
            if (strlen(line) > 0) {
                debug_log(config, "Processing contact line: '%s'", line);
                
                // Split line into words (same logic as bash version)
                char *words[20];  // Enough space for all words
                int wordCount = 0;
                char *token = strtok(line, " ");
                while (token != NULL && wordCount < 20) {
                    words[wordCount++] = token;
                    token = strtok(NULL, " ");
                }
                
                // In bash version: nodearray[8] and nodearray[11] are the node numbers
                if (wordCount > 11) {
                    unsigned long fromNode = atol(words[8]);
                    unsigned long toNode = atol(words[11]);
                    
                    if (fromNode > 0 && toNode > 0) {
                        fprintf(graphFile, "\"ipn:%lu\" -> \"ipn:%lu\"\n", fromNode, toNode);
                        contactCount++;
                        debug_log(config, "Added contact: %lu -> %lu", fromNode, toNode);
                    }
                } else {
                    debug_log(config, "Not enough words in contact line (%d words)", wordCount);
                }
            }
        }
        pclose(ionadmin_pipe);
    } else {
        debug_log(config, "Failed to execute ionadmin command");
    }
    
    // Close the graph (bash-compatible format)
    time(&currentTime);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d_%H-%M-%S", localtime(&currentTime));
    fprintf(graphFile, "labelloc=\"t\"; label=\"IPNSIG's DTN Network Graph, Updated:%s\"}\n", timeStr);
    
    // Ensure all data is written and properly close the file
    fflush(graphFile);
    fclose(graphFile);
    
    dtnex_log("[INFO] Graph file updated with %d contacts: %s", contactCount, graphvizFile);
    
    // Write the metadata list to a file in the same directory as the .gv file
    // Extract the directory path from the graphvizFile
    char metadataFilePath[512];
    char *lastSlash = strrchr(graphvizFile, '/');
    
    if (lastSlash) {
        // If graphvizFile includes a path, use the same path
        size_t dirLen = lastSlash - graphvizFile + 1;
        strncpy(metadataFilePath, graphvizFile, dirLen);
        metadataFilePath[dirLen] = '\0';
        strcat(metadataFilePath, "metadata_list.txt");
    } else {
        // If graphvizFile is just a filename, use the current directory
        strcpy(metadataFilePath, "metadata_list.txt");
    }
    
    // Write metadata list to file
    FILE *metadataFile = fopen(metadataFilePath, "w");
    if (metadataFile) {
        fprintf(metadataFile, "# DTN Metadata List - Generated by DTNEXC on %s\n\n", timeStr);
        fprintf(metadataFile, "NODE ID    | METADATA\n");
        fprintf(metadataFile, "------------------------------------------------------------\n");
        
        // First write our own node's metadata
        fprintf(metadataFile, "%-10lu | %s (LOCAL NODE)\n", config->nodeId, config->nodemetadata);
        
        // Then write all other nodes' metadata
        for (i = 0; i < nodeMetadataCount; i++) {
            if (nodeMetadataList[i].nodeId != config->nodeId) {
                fprintf(metadataFile, "%-10lu | %s\n", 
                        nodeMetadataList[i].nodeId, nodeMetadataList[i].metadata);
            }
        }
        
        fprintf(metadataFile, "\n# Total nodes: %d\n", nodeMetadataCount + 1); // +1 for our own node
        
        fflush(metadataFile);
        fclose(metadataFile);
        debug_log(config, "[INFO] Metadata list written to %s", metadataFilePath);
    } else {
        dtnex_log("[ERROR] Failed to write metadata list to %s", metadataFilePath);
    }
    
    // Print a complete list of all metadata after graph generation (debug mode only)
    debug_log(config, "======== METADATA USED FOR GRAPH GENERATION (%d nodes) ========", nodeMetadataCount);
    debug_log(config, "NODE ID    | METADATA");
    debug_log(config, "----------------------------------------");
    
    // Print our own node's metadata
    debug_log(config, "%-10lu | %s (LOCAL NODE)", config->nodeId, config->nodemetadata);
    
    // Print all other nodes' metadata
    for (i = 0; i < nodeMetadataCount; i++) {
        // Skip our own node as we already printed it
        if (nodeMetadataList[i].nodeId != config->nodeId) {
            debug_log(config, "%-10lu | %s", 
                    nodeMetadataList[i].nodeId, nodeMetadataList[i].metadata);
        }
    }
    debug_log(config, "========================================");
}


/**
 * Initialize the bpecho service
 */
int initBpechoService(DtnexConfig *config, BpechoState *state) {
    char bpechoEid[MAX_EID_LENGTH];
    
    dtnex_log("Initializing bpecho service...");
    
    // Format the endpoint ID as ipn:node.service
    snprintf(bpechoEid, MAX_EID_LENGTH, "ipn:%lu.%s", config->nodeId, config->bpechoServiceNr);
    dtnex_log("Using bpecho endpoint: %s", bpechoEid);
    
    // First try to add/register the bpecho endpoint in ION's routing database
    if (addEndpoint(bpechoEid, EnqueueBundle, NULL) < 0) {
        dtnex_log("⚠️ Warning: Could not register bpecho endpoint %s in routing database", bpechoEid);
    }
    
    // Open the bpecho endpoint
    if (bp_open(bpechoEid, &state->sap) < 0) {
        dtnex_log("❌ Failed to open bpecho endpoint");
        return -1;
    }
    
    // Initialize state
    state->running = 1;
    
    // Initialize attendant for blocking transmission
    if (ionStartAttendant(&state->attendant) < 0) {
        dtnex_log("❌ Failed to initialize blocking transmission for bpecho");
        bp_close(state->sap);
        return -1;
    }
    
    dtnex_log("✅ Bpecho service initialized successfully");
    return 0;
}

/**
 * Run the bpecho service (based on bpecho.c)
 */
void *runBpechoService(void *arg) {
    DtnexConfig *config = (DtnexConfig *)arg;
    static char dlvmarks[] = "?.*!X";  // Delivery status markers
    Sdr sdr;
    char dataToSend[BPECHO_ADU_LEN];
    Object bundleZco;
    Object newBundle;
    Object extent;
    BpDelivery dlv;
    ZcoReader reader;
    char sourceEid[MAX_EID_LENGTH];
    int bytesToEcho = 0;
    int result;
    
    // Don't set separate signal handler for bpecho - use main process handler
    
    sdr = bp_get_sdr();
    dtnex_log("Starting bpecho service thread on service %s", config->bpechoServiceNr);
    
    while (bpechoState.running) {
        // Wait for a bundle from a sender
        if (bp_receive(bpechoState.sap, &dlv, BP_BLOCKING) < 0) {
            dtnex_log("❌ Bpecho bundle reception failed");
            bpechoState.running = 0;
            break;
        }
        
        // Debug indicators for different status codes
        putchar(dlvmarks[dlv.result]);
        fflush(stdout);
        
        // Handle different reception results
        if (dlv.result == BpReceptionInterrupted) {
            continue;
        }
        
        if (dlv.result == BpEndpointStopped) {
            bpechoState.running = 0;
            continue;
        }
        
        if (dlv.result == BpPayloadPresent) {
            // Got a bundle with payload, echo it back
            
            // Save the source EID
            istrcpy(sourceEid, dlv.bundleSourceEid, sizeof(sourceEid));
            
            // Calculate how many bytes to echo (limit to our buffer size)
            bytesToEcho = MIN(zco_source_data_length(sdr, dlv.adu), BPECHO_ADU_LEN);
            
            // Set up for reading the payload
            zco_start_receiving(dlv.adu, &reader);
            
            // Read the payload data
            CHKZERO(sdr_begin_xn(sdr));
            result = zco_receive_source(sdr, &reader, bytesToEcho, dataToSend);
            
            if (sdr_end_xn(sdr) < 0 || result < 0) {
                dtnex_log("❌ Can't receive payload for echo");
                bp_release_delivery(&dlv, 1);
                continue;
            }
            
            // Parse source node number if available
            unsigned long sourceNode = 0;
            if (strncmp(sourceEid, "ipn:", 4) == 0) {
                sscanf(sourceEid, "ipn:%lu", &sourceNode);
            }
            
            // Echo message received with enhanced information
            dtnex_log("\033[32m[BPECHO] Received %d bytes from %s\033[0m", 
                bytesToEcho, sourceEid);
            
            // Release the delivery
            bp_release_delivery(&dlv, 1);
            
            // Skip anonymous senders
            if (strcmp(sourceEid, "dtn:none") == 0) {
                dtnex_log("\033[33m[WARN] Anonymous sender - echo reply skipped\033[0m");
                continue;
            }
            
            // Prepare to send echo reply
            CHKZERO(sdr_begin_xn(sdr));
            extent = sdr_malloc(sdr, bytesToEcho);
            if (extent) {
                sdr_write(sdr, extent, dataToSend, bytesToEcho);
            }
            
            if (sdr_end_xn(sdr) < 0) {
                dtnex_log("❌ No space for ZCO extent for echo reply");
                continue;
            }
            
            // Create ZCO for the echo data
            bundleZco = ionCreateZco(ZcoSdrSource, extent, 0, bytesToEcho,
                                   BP_STD_PRIORITY, 0, ZcoOutbound, &bpechoState.attendant);
            
            if (bundleZco == 0 || bundleZco == (Object) ERROR) {
                dtnex_log("❌ Can't create ZCO for echo reply");
                continue;
            }
            
            // Send the echo reply
            if (bp_send(bpechoState.sap, sourceEid, NULL, 300, BP_STD_PRIORITY,
                      NoCustodyRequested, 0, 0, NULL, bundleZco, &newBundle) < 1) {
                dtnex_log("\033[31m[ERROR] Failed to send bpecho reply\033[0m");
                continue;
            }
            
            // Enhanced output with node number
            unsigned long replyNode = 0;
            if (strncmp(sourceEid, "ipn:", 4) == 0) {
                sscanf(sourceEid, "ipn:%lu", &replyNode);
            }
            
            dtnex_log("\033[33m[BPECHO] Reply sent to %s - %d bytes\033[0m", 
                     sourceEid, bytesToEcho);
        } else {
            // Other status (timeout, etc)
            bp_release_delivery(&dlv, 1);
        }
    }
    
    // Clean up
    dtnex_log("🧹 Shutting down bpecho service...");
    bp_close(bpechoState.sap);
    ionStopAttendant(&bpechoState.attendant);
    
    dtnex_log("✅ Bpecho service terminated normally");
    return NULL;
}

/**
 * Initialize bundle reception service
 */
int initBundleReception(DtnexConfig *config, BundleReceptionState *state) {
    state->config = config;
    state->running = 1;
    return 0;
}

/**
 * Bundle reception thread - handles incoming DTNEX CBOR messages
 * Uses blocking reception pattern like bpsink
 */
void *runBundleReception(void *arg) {
    BundleReceptionState *state = (BundleReceptionState *)arg;
    DtnexConfig *config = state->config;
    BpDelivery dlv;
    Sdr sdr;
    char buffer[MAX_LINE_LENGTH];
    ZcoReader reader;
    
    dtnex_log("📥 Starting bundle reception thread");
    
    sdr = bp_get_sdr();
    
    while (state->running && running) {
        // Clear the delivery structure
        memset(&dlv, 0, sizeof(BpDelivery));
        
        // Wait for a bundle using blocking reception (like bpsink)
        if (bp_receive(sap, &dlv, BP_BLOCKING) < 0) {
            if (!running) break; // Normal shutdown
            dtnex_log("❌ Bundle reception failed, thread terminating");
            state->running = 0;
            break;
        }
        
        // Handle different reception results
        if (dlv.result == BpReceptionInterrupted) {
            continue;
        }
        
        if (dlv.result == BpEndpointStopped) {
            dtnex_log("❌ Endpoint stopped, bundle reception thread terminating");
            state->running = 0;
            running = 0;
            break;
        }
        
        if (dlv.result == BpPayloadPresent) {
            // Get source node information
            unsigned long sourceNode = 0;
            if (dlv.bundleSourceEid && strncmp(dlv.bundleSourceEid, "ipn:", 4) == 0) {
                sscanf(dlv.bundleSourceEid, "ipn:%lu", &sourceNode);
            }
            
            // Get content length
            int contentLength = 0;
            CHKZERO(sdr_begin_xn(sdr));
            contentLength = zco_source_data_length(sdr, dlv.adu);
            sdr_exit_xn(sdr);
            
            if (contentLength > 0 && contentLength < MAX_LINE_LENGTH) {
                // Read the bundle content
                zco_start_receiving(dlv.adu, &reader);
                CHKZERO(sdr_begin_xn(sdr));
                int len = zco_receive_source(sdr, &reader, contentLength, buffer);
                if (sdr_end_xn(sdr) < 0 || len < 0) {
                    dtnex_log("❌ Error reading bundle content");
                    bp_release_delivery(&dlv, 1);
                    continue;
                }
                
                // Build source info string
                char sourceInfo[128];
                if (dlv.bundleSourceEid && strlen(dlv.bundleSourceEid) > 0) {
                    snprintf(sourceInfo, sizeof(sourceInfo), "%s", dlv.bundleSourceEid);
                } else if (sourceNode > 0) {
                    snprintf(sourceInfo, sizeof(sourceInfo), "ipn:%lu", sourceNode);
                } else {
                    snprintf(sourceInfo, sizeof(sourceInfo), "unknown");
                }
                
                // Bundle received - will be logged in message processing
                
                // Process the CBOR message
                processCborMessage(config, (unsigned char*)buffer, contentLength);
            } else {
                dtnex_log("⚠️ Bundle content invalid size (%d bytes), skipping", contentLength);
            }
        }
        
        // Always release the delivery
        bp_release_delivery(&dlv, 1);
    }
    
    dtnex_log("📥 Bundle reception thread terminated normally");
    return NULL;
}

/**
 * Stop bundle reception service
 */
void stopBundleReception(BundleReceptionState *state) {
    if (state) {
        state->running = 0;
        // Interrupt the blocking receive call only if ION is connected
        if (sap && ionConnected) {
            bp_interrupt(sap);
        }
    }
}

/**
 * Main function - Single-threaded implementation based on bpsink pattern
 */
int main(int argc, char **argv) {
    DtnexConfig config;
    
    // Store original arguments for potential restart
    original_argc = argc;
    original_argv = argv;
    
    // Set up signal handlers for clean shutdown with signal masking
    struct sigaction sa;
    sigset_t mask;
    
    // Block signals during handler execution
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGTSTP);
    
    sa.sa_handler = signalHandler;
    sa.sa_mask = mask;
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls
    
    sigaction(SIGINT, &sa, NULL);   // Ctrl+C
    sigaction(SIGTERM, &sa, NULL);  // kill
    sigaction(SIGTSTP, &sa, NULL);  // Ctrl+Z
    
    // Load configuration
    loadConfig(&config);
    
    // Initialize - now returns an error code if it fails
    if (init(&config) < 0) {
        dtnex_log("Initialization failed, exiting");
        return 1;
    }
    
    // Initialize bpecho service only if ION is connected
    if (ionConnected) {
        if (initBpechoService(&config, &bpechoState) < 0) {
            dtnex_log("⚠️ Bpecho service initialization failed, continuing without it");
        } else {
            // Create bpecho service thread
            if (pthread_create(&bpechoThread, NULL, runBpechoService, (void *)&config) != 0) {
                dtnex_log("⚠️ Failed to create bpecho service thread, continuing without it");
                // Clean up bpecho resources
                bp_close(bpechoState.sap);
                ionStopAttendant(&bpechoState.attendant);
            } else {
                dtnex_log("✅ Bpecho service thread started");
            }
        }
    } else {
        dtnex_log("⚠️ Skipping bpecho service initialization (ION not connected)");
    }
    
    // Initialize bundle reception service only if ION is connected
    if (ionConnected) {
        if (initBundleReception(&config, &bundleReceptionState) < 0) {
            dtnex_log("❌ Bundle reception service initialization failed");
            return 1;
        } else {
            // Create bundle reception thread
            if (pthread_create(&bundleReceptionState.thread, NULL, runBundleReception, (void *)&bundleReceptionState) != 0) {
                dtnex_log("❌ Failed to create bundle reception thread");
                return 1;
            } else {
                dtnex_log("✅ Bundle reception thread started");
            }
        }
    } else {
        dtnex_log("⚠️ Skipping bundle reception service initialization (ION not connected)");
    }
    
    dtnex_log("DTNEXC running - Ctrl+C to exit");
    
    // Perform startup contact broadcast - send our contact list to all neighbors
    Plan plans[MAX_PLANS];
    int planCount = 0;
    
    dtnex_log("🚀 Performing startup contact broadcast to all neighbors...");
    getplanlist(&config, plans, &planCount);
    if (planCount > 0) {
        exchangeWithNeighbors(&config, plans, planCount);
        dtnex_log("✅ Startup contact broadcast completed to %d neighbors", planCount);
    } else {
        dtnex_log("⚠️ No neighbors found for startup broadcast");
    }
    
    // Main loop - use event-driven loop instead of continuous polling
    eventDrivenLoop(&config);
    
    // Clean up
    dtnex_log("Shutting down...");
    
    // Wait for bundle reception thread to terminate if it's running
    if (bundleReceptionState.running) {
        dtnex_log("Waiting for bundle reception thread to terminate...");
        stopBundleReception(&bundleReceptionState);
        pthread_join(bundleReceptionState.thread, NULL);
    }
    
    // Wait for bpecho thread to terminate if it's running
    if (bpechoState.running) {
        dtnex_log("Waiting for bpecho service to terminate...");
        pthread_join(bpechoThread, NULL);
    }
    
    // Close the BP endpoint gracefully if we have one
    if (sap != NULL) {
        dtnex_log("🔌 Closing BP endpoint");
        bp_close(sap);
    }
    
    // Detach from BP
    dtnex_log("🧹 Detaching from ION BP system");
    bp_detach();
    
    dtnex_log("DTNEXC terminated normally");
    return 0;
}

/* ========================================================================
 * CBOR Message Implementation Functions
 * ======================================================================== */

/**
 * Generate a cryptographically random nonce
 */
void generateNonce(unsigned char *nonce) {
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        fread(nonce, 1, DTNEX_NONCE_SIZE, urandom);
        fclose(urandom);
    } else {
        // Fallback to time-based pseudo-random
        srand(time(NULL));
        for (int i = 0; i < DTNEX_NONCE_SIZE; i++) {
            nonce[i] = rand() & 0xFF;
        }
    }
}

/**
 * Calculate HMAC-SHA256 (truncated to DTNEX_HMAC_SIZE for size efficiency)
 */
int calculateHmac(const unsigned char *message, int msgLen, const char *key, unsigned char *hmac) {
    unsigned char fullHmac[SHA256_DIGEST_SIZE];
    
    // Simple HMAC implementation (not OpenSSL's HMAC for minimal dependencies)
    unsigned char keyPad[64]; // Block size for SHA-256
    memset(keyPad, 0, sizeof(keyPad));
    
    int keyLen = strlen(key);
    if (keyLen > 64) {
        // Hash key if longer than block size
        SHA256((unsigned char*)key, keyLen, keyPad);
    } else {
        memcpy(keyPad, key, keyLen);
    }
    
    // Create inner and outer padding
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = keyPad[i] ^ 0x36;
        opad[i] = keyPad[i] ^ 0x5c;
    }
    
    // Inner hash: SHA256(key XOR ipad || message)
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, ipad, 64);
    SHA256_Update(&ctx, message, msgLen);
    SHA256_Final(fullHmac, &ctx);
    
    // Outer hash: SHA256(key XOR opad || inner_hash)
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, opad, 64);
    SHA256_Update(&ctx, fullHmac, SHA256_DIGEST_SIZE);
    SHA256_Final(fullHmac, &ctx);
    
    // Use only first DTNEX_HMAC_SIZE bytes for size optimization
    memcpy(hmac, fullHmac, DTNEX_HMAC_SIZE);
    return DTNEX_HMAC_SIZE;
}

/**
 * Verify HMAC
 */
int verifyHmac(DtnexConfig *config, const unsigned char *message, int msgLen, const unsigned char *receivedHmac, const char *key) {
    unsigned char calculatedHmac[DTNEX_HMAC_SIZE];
    calculateHmac(message, msgLen, key, calculatedHmac);
    
    // Debug logging for HMAC comparison
    debug_log(config, "🔍 HMAC verification details:");
    debug_log(config, "Message length: %d bytes", msgLen);
    debug_log(config, "Key: '%s'", key);
    
    if (config->debugMode) {
        printf("\033[90m[DEBUG] Calculated HMAC: ");
        for (int i = 0; i < DTNEX_HMAC_SIZE; i++) {
            printf("%02x", calculatedHmac[i]);
        }
        printf("\n[DEBUG] Received HMAC:   ");
        for (int i = 0; i < DTNEX_HMAC_SIZE; i++) {
            printf("%02x", receivedHmac[i]);
        }
        printf("\033[0m\n");
    }
    
    int result = memcmp(calculatedHmac, receivedHmac, DTNEX_HMAC_SIZE) == 0;
    debug_log(config, "HMAC match: %s", result ? "YES" : "NO");
    return result;
}

/**
 * Check if nonce is duplicate (replay protection)
 */
int isNonceDuplicate(unsigned char *nonce, unsigned long origin) {
    for (int i = 0; i < nonceCacheCount; i++) {
        if (nonceCache[i].origin == origin && 
            memcmp(nonceCache[i].nonce, nonce, DTNEX_NONCE_SIZE) == 0) {
            return 1; // Duplicate found
        }
    }
    return 0;
}

/**
 * Add nonce to cache (FIFO)
 */
void addNonceToCache(unsigned char *nonce, unsigned long origin) {
    if (nonceCacheCount < MAX_HASH_CACHE) {
        // Still have space
        memcpy(nonceCache[nonceCacheCount].nonce, nonce, DTNEX_NONCE_SIZE);
        nonceCache[nonceCacheCount].origin = origin;
        nonceCache[nonceCacheCount].timestamp = time(NULL);
        nonceCacheCount++;
    } else {
        // Cache full, implement FIFO
        for (int i = 0; i < MAX_HASH_CACHE - 1; i++) {
            nonceCache[i] = nonceCache[i + 1];
        }
        memcpy(nonceCache[MAX_HASH_CACHE - 1].nonce, nonce, DTNEX_NONCE_SIZE);
        nonceCache[MAX_HASH_CACHE - 1].origin = origin;
        nonceCache[MAX_HASH_CACHE - 1].timestamp = time(NULL);
    }
}

/**
 * Encode CBOR contact message
 * Format: [version, type, timestamp, expireTime, origin, from, nonce, [nodeA, nodeB, duration, datarate, reliability], hmac]
 */
int encodeCborContactMessage(DtnexConfig *config, ContactInfo *contact, unsigned char *buffer, int bufferSize) {
    unsigned char *cursor = buffer;
    unsigned char nonce[DTNEX_NONCE_SIZE];
    int bytesWritten = 0;
    
    // Generate nonce
    generateNonce(nonce);
    
    time_t currentTime = time(NULL);
    time_t expireTime = currentTime + config->contactLifetime;
    
    // Encode main array with 9 elements [version, type, ts, exp, orig, from, nonce, data, hmac]
    bytesWritten += cbor_encode_array_open(9, &cursor);
    
    // 1. Version
    bytesWritten += cbor_encode_integer(DTNEX_PROTOCOL_VERSION, &cursor);
    
    // 2. Message type "c"
    bytesWritten += cbor_encode_text_string("c", 1, &cursor);
    
    // 3. Timestamp
    bytesWritten += cbor_encode_integer(currentTime, &cursor);
    
    // 4. Expire time
    bytesWritten += cbor_encode_integer(expireTime, &cursor);
    
    // 5. Origin node
    bytesWritten += cbor_encode_integer(config->nodeId, &cursor);
    
    // 6. From node (same as origin for originating messages)
    bytesWritten += cbor_encode_integer(config->nodeId, &cursor);
    
    // 7. Nonce
    bytesWritten += cbor_encode_byte_string(nonce, DTNEX_NONCE_SIZE, &cursor);
    
    // 8. Contact data array - ultra-minimal format (3 elements: nodeA, nodeB, duration)
    bytesWritten += cbor_encode_array_open(3, &cursor);
    bytesWritten += cbor_encode_integer(contact->nodeA, &cursor);
    bytesWritten += cbor_encode_integer(contact->nodeB, &cursor);  
    bytesWritten += cbor_encode_integer(contact->duration, &cursor);
    
    // 9. Calculate HMAC over everything except the HMAC field itself
    unsigned char hmac[DTNEX_HMAC_SIZE];
    calculateHmac(buffer, bytesWritten, config->presSharedNetworkKey, hmac);
    bytesWritten += cbor_encode_byte_string(hmac, DTNEX_HMAC_SIZE, &cursor);
    
    debug_log(config, "[CBOR] Encoded contact message: %d bytes", bytesWritten);
    return bytesWritten;
}

/**
 * Encode CBOR metadata message  
 * Format: [version, type, timestamp, expireTime, origin, from, nonce, metadata_data, hmac]
 */
int encodeCborMetadataMessage(DtnexConfig *config, StructuredMetadata *metadata, unsigned char *buffer, int bufferSize) {
    unsigned char *cursor = buffer;
    unsigned char nonce[DTNEX_NONCE_SIZE];
    int bytesWritten = 0;
    
    // Generate nonce
    generateNonce(nonce);
    
    time_t currentTime = time(NULL);
    time_t expireTime = currentTime + config->contactLifetime;
    
    // Encode main array with 9 elements
    bytesWritten += cbor_encode_array_open(9, &cursor);
    
    // 1. Version
    bytesWritten += cbor_encode_integer(DTNEX_PROTOCOL_VERSION, &cursor);
    
    // 2. Message type "m"
    bytesWritten += cbor_encode_text_string("m", 1, &cursor);
    
    // 3. Timestamp
    bytesWritten += cbor_encode_integer(currentTime, &cursor);
    
    // 4. Expire time
    bytesWritten += cbor_encode_integer(expireTime, &cursor);
    
    // 5. Origin node
    bytesWritten += cbor_encode_integer(config->nodeId, &cursor);
    
    // 6. From node
    bytesWritten += cbor_encode_integer(config->nodeId, &cursor);
    
    // 7. Nonce
    bytesWritten += cbor_encode_byte_string(nonce, DTNEX_NONCE_SIZE, &cursor);
    
    // 8. Metadata array - format: [nodeId, name, contact, lat?, lon?]
    int metadataElements = 3; // nodeId, name, contact (GPS optional)
    if (metadata->latitude != 0 && metadata->longitude != 0) {
        metadataElements += 2; // Add latitude and longitude if both are set
    }
    
    bytesWritten += cbor_encode_array_open(metadataElements, &cursor);
    bytesWritten += cbor_encode_integer(metadata->nodeId, &cursor);
    bytesWritten += cbor_encode_text_string(metadata->name, strlen(metadata->name), &cursor);
    bytesWritten += cbor_encode_text_string(metadata->contact, strlen(metadata->contact), &cursor);
    
    // Add GPS coordinates if both are available
    if (metadata->latitude != 0 && metadata->longitude != 0) {
        bytesWritten += cbor_encode_integer(metadata->latitude, &cursor);
        bytesWritten += cbor_encode_integer(metadata->longitude, &cursor);
    }
    
    // 9. Calculate HMAC
    unsigned char hmac[DTNEX_HMAC_SIZE];
    calculateHmac(buffer, bytesWritten, config->presSharedNetworkKey, hmac);
    bytesWritten += cbor_encode_byte_string(hmac, DTNEX_HMAC_SIZE, &cursor);
    
    debug_log(config, "[CBOR] Encoded metadata message: %d bytes", bytesWritten);
    return bytesWritten;
}

/**
 * Parse raw metadata string into structured format
 * Input format: "NodeName,contact@email.com,Location"
 */
void parseNodeMetadata(const char *rawMetadata, StructuredMetadata *metadata) {
    char buffer[MAX_METADATA_LENGTH];
    char *token;
    int fieldCount = 0;
    
    // Preserve nodeId and GPS coordinates, only clear the string fields
    unsigned long savedNodeId = metadata->nodeId;
    int savedLatitude = metadata->latitude;
    int savedLongitude = metadata->longitude;
    
    // Initialize only the string fields, preserve nodeId and GPS
    memset(metadata->name, 0, MAX_NODE_NAME_LENGTH);
    memset(metadata->contact, 0, MAX_CONTACT_INFO_LENGTH);
    
    // Restore preserved values
    metadata->nodeId = savedNodeId;
    metadata->latitude = savedLatitude;
    metadata->longitude = savedLongitude;
    
    // Copy raw metadata to avoid modifying original
    strncpy(buffer, rawMetadata, MAX_METADATA_LENGTH - 1);
    buffer[MAX_METADATA_LENGTH - 1] = '\0';
    
    // Parse comma-separated fields
    token = strtok(buffer, ",");
    while (token && fieldCount < 3) {
        // Remove leading/trailing whitespace
        while (*token == ' ') token++;
        
        switch (fieldCount) {
            case 0: // Node name
                strncpy(metadata->name, token, MAX_NODE_NAME_LENGTH - 1);
                break;
            case 1: // Contact info
                strncpy(metadata->contact, token, MAX_CONTACT_INFO_LENGTH - 1);
                break;
            case 2: // Location (ignored in CBOR version - use GPS instead)
                break;
        }
        
        fieldCount++;
        token = strtok(NULL, ",");
    }
    
    // Ensure null termination
    metadata->name[MAX_NODE_NAME_LENGTH - 1] = '\0';
    metadata->contact[MAX_CONTACT_INFO_LENGTH - 1] = '\0';
}

/**
 * Send CBOR bundle with direct ION API - following bpsource.c pattern
 * This replaces the old sendBundle function for CBOR data
 */
int sendCborBundle(const char *destEid, unsigned char *cborData, int dataSize, int ttl) {
    // Check if destination EID is valid
    if (!destEid || strlen(destEid) == 0) {
        dtnex_log("\033[31m[ERROR] Invalid destination EID\033[0m");
        return -1;
    }
    
    // Get SDR and prepare to create and send bundle
    Sdr sdr = bp_get_sdr();
    Object bundleZco;
    Object extent;
    Object newBundle;
    int sendResult;
    
    // Start an SDR transaction
    if (sdr_begin_xn(sdr) < 0) {
        dtnex_log("Error starting SDR transaction for bundle creation");
        return -1;
    }
    
    // Allocate memory for the CBOR data
    extent = sdr_malloc(sdr, dataSize);
    if (!extent) {
        dtnex_log("Failed to allocate memory for CBOR data");
        sdr_cancel_xn(sdr);
        return -1;
    }
    
    // Write the CBOR data to the SDR
    sdr_write(sdr, extent, (char*)cborData, dataSize);
    
    // End the transaction
    if (sdr_end_xn(sdr) < 0) {
        dtnex_log("No space for ZCO extent");
        return -1;
    }
    
    // Create ZCO from the extent
    bundleZco = ionCreateZco(ZcoSdrSource, extent, 0, dataSize, 
                            BP_STD_PRIORITY, 0, ZcoOutbound, NULL);
    
    if (bundleZco == 0 || bundleZco == (Object) ERROR) {
        dtnex_log("Can't create ZCO extent");
        return -1;
    }
    
    // Send the bundle using direct ION API - no source EID for CBOR messages
    sendResult = bp_send(NULL, destEid, NULL, ttl, BP_STD_PRIORITY,
                        NoCustodyRequested, 0, 0, NULL, bundleZco, &newBundle);
    
    if (sendResult <= 0) {
        dtnex_log("\033[31m[ERROR] Failed to send CBOR bundle to %s\033[0m", destEid);
        return -1;
    }
    
    // Log the successful CBOR send operation with size
    // Sending handled by caller with log_message_sent
    
    return dataSize;
}

// generateNonce function is already defined earlier in the file

/**
 * Event-driven main loop - sleeps until next scheduled event
 * Avoids continuous polling and reduces CPU usage
 */
void eventDrivenLoop(DtnexConfig *config) {
    Plan plans[MAX_PLANS];
    int planCount = 0;
    time_t nextUpdateTime = 0;
    time_t nextIonRetry = 0;
    time_t currentTime;
    int messageReceived;
    // Use global ionConnected flag, but also verify current state
    if (!ionConnected) {
        ionConnected = (sap != NULL && config->nodeId != 0);
    }
    
    dtnex_log("🔄 Starting event-driven operation (update every %d minutes)", config->updateInterval / 60);
    
    // If ION is not connected, schedule immediate retry
    if (!ionConnected) {
        nextIonRetry = time(NULL);
        dtnex_log("⚠️ ION not connected - will retry every minute");
    } else {
        // Schedule first update immediately if ION is connected
        scheduleNextUpdate(config, &nextUpdateTime);
    }
    
    while (running) {
        currentTime = time(NULL);
        messageReceived = 0;
        
        // Check if we need to retry ION connection
        if (!ionConnected && currentTime >= nextIonRetry) {
            dtnex_log("🔌 Attempting to connect to ION...");
            if (tryConnectToIon(config) == 0) {
                ionConnected = 1;
                dtnex_log("✅ Successfully connected to ION");
                
                // Initialize services that were skipped during startup
                if (!bpechoState.running) {
                    dtnex_log("🚀 Initializing bpecho service after ION reconnection...");
                    if (initBpechoService(config, &bpechoState) == 0) {
                        if (pthread_create(&bpechoThread, NULL, runBpechoService, (void *)config) == 0) {
                            dtnex_log("✅ Bpecho service thread started");
                        } else {
                            dtnex_log("❌ Failed to create bpecho service thread");
                            bpechoState.running = 0;
                        }
                    } else {
                        dtnex_log("❌ Failed to initialize bpecho service");
                    }
                }
                
                if (!bundleReceptionState.running) {
                    dtnex_log("🚀 Initializing bundle reception service after ION reconnection...");
                    if (initBundleReception(config, &bundleReceptionState) == 0) {
                        if (pthread_create(&bundleReceptionState.thread, NULL, runBundleReception, (void *)&bundleReceptionState) == 0) {
                            dtnex_log("✅ Bundle reception thread started");
                        } else {
                            dtnex_log("❌ Failed to create bundle reception thread");
                        }
                    } else {
                        dtnex_log("❌ Failed to initialize bundle reception service");
                    }
                }
                
                // Schedule first update immediately after connection
                scheduleNextUpdate(config, &nextUpdateTime);
            } else {
                // Check if ION is completely stopped
                int ionProcessCount = system("pgrep -c '^(ion|bp)' >/dev/null 2>&1");
                if (ionProcessCount != 0) {
                    // ION processes are running, quick retry
                    dtnex_log("🚨 Failed to connect to ION (processes running - may still be starting)");
                    nextIonRetry = currentTime + 10; // Retry in 10 seconds
                } else {
                    // No ION processes found, longer wait
                    dtnex_log("🚨 Failed to connect to ION (no ION processes detected)");
                    nextIonRetry = currentTime + 300; // Retry in 5 minutes
                }
            }
        }
        
        // If ION is connected, check if it's time for scheduled update
        if (ionConnected && currentTime >= nextUpdateTime) {
            // Check ION status before proceeding
            IonStatus ionStatus = checkIonStatus(config);
            if (ionStatus != ION_STATUS_RUNNING) {
                dtnex_log("⚠️ Lost connection to ION");
                ionConnected = 0;
                nextIonRetry = currentTime + 60; // Retry in 1 minute
                continue;
            }
            
            // Get current plan list
            getplanlist(config, plans, &planCount);
            
            // Only perform operations if we have ION connection
            if (ionConnected) {
                // Perform scheduled operations
                exchangeWithNeighbors(config, plans, planCount);
                getContacts(config);
            }
            
            // Display collected metadata (if debug mode)
            if (config->debugMode) {
                dtnex_log("📊 Collected metadata from %d nodes", nodeMetadataCount);
                for (int i = 0; i < nodeMetadataCount; i++) {
                    dtnex_log("  Node %lu: %s", nodeMetadataList[i].nodeId, nodeMetadataList[i].metadata);
                }
            }
            
            // Generate graph if enabled (independent of ION status)
            if (config->createGraph) {
                createGraph(config);
            }
            
            // Schedule next update
            scheduleNextUpdate(config, &nextUpdateTime);
        }
        
        // Generate graph even if ION is not connected, on first run or when the schedule would trigger
        // This ensures graph generation works even when ION is down
        if (config->createGraph && !ionConnected && currentTime >= nextUpdateTime) {
            createGraph(config);
            // Schedule next update even when ION is down
            scheduleNextUpdate(config, &nextUpdateTime);
        }
        
        // Bundle reception is now handled by the dedicated thread
        if (!running) break;
        
        // Update contact info to ensure we have the latest topology (only if ION connected)
        if (ionConnected) {
            getContacts(config);
        }
        
        // Calculate sleep time until next event
        currentTime = time(NULL);
        time_t sleepTime;
        
        if (!ionConnected) {
            // When ION is not connected, sleep until next retry
            sleepTime = nextIonRetry - currentTime;
        } else {
            // When ION is connected, sleep until next update
            sleepTime = nextUpdateTime - currentTime;
        }
        
        if (sleepTime > 0) {
            // Sleep in 1-second increments to allow for responsive Ctrl+C handling
            int actualSleepTime = (sleepTime > 60) ? 60 : (int)sleepTime;
            
            if (!messageReceived && actualSleepTime > 1) {
                dtnex_log("💤 Sleeping %ds until next event (next update in %lds)", 
                    actualSleepTime, sleepTime);
            }
            
            // Sleep in 1-second increments, checking running flag frequently
            // Use a more responsive sleep that can be interrupted by signals
            for (int i = 0; i < actualSleepTime && running; i++) {
                struct timespec req = {1, 0}; // 1 second
                struct timespec rem;
                
                // nanosleep can be interrupted by signals and returns remaining time
                while (nanosleep(&req, &rem) == -1 && running && errno == EINTR) {
                    req = rem; // Continue with remaining time if interrupted
                }
                
                // Bundle reception now handled by dedicated thread
                
                // Check running flag after each second to respond quickly to signals
                if (!running) break;
            }
        } else {
            // Short sleep to avoid busy waiting
            usleep(100000); // 100ms
        }
    }
}

/**
 * Schedule the next update time based on configuration
 */
void scheduleNextUpdate(DtnexConfig *config, time_t *nextUpdateTime) {
    *nextUpdateTime = time(NULL) + config->updateInterval;
    
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(nextUpdateTime));
    dtnex_log("⏰ Next update scheduled for %s (%d minutes)", timeStr, config->updateInterval / 60);
}

/**
 * Completely restart DTNEX when ION restart is detected
 * This is safer than trying to reinitialize ION connections
 */
void restartDtnex(DtnexConfig *config) {
    dtnex_log("🔄 ION restart detected - completely restarting DTNEX...");
    
    // Give ION time to fully restart
    sleep(2);
    
    // Clean shutdown first
    running = 0;
    ionConnected = 0;
    
    // Close file descriptors cleanly if possible
    if (sap != NULL) {
        bp_close(sap);
        sap = NULL;
    }
    
    // Log the restart
    dtnex_log("🔄 Executing DTNEX restart...");
    
    // Execute restart using execv
    if (original_argv != NULL && original_argc > 0) {
        execv(original_argv[0], original_argv);
        
        // If execv fails, log error and exit
        dtnex_log("❌ Failed to restart DTNEX: %s", strerror(errno));
        exit(1);
    } else {
        dtnex_log("❌ Cannot restart - original arguments not stored");
        exit(1);
    }
}


/**
 * Check if ION is running and accessible
 */
IonStatus checkIonStatus(DtnexConfig *config) {
    // Try a simple BP operation to test connectivity
    if (!sap) {
        return ION_STATUS_STOPPED;
    }
    
    // Test if we can still receive (non-blocking)
    BpDelivery testDlv;
    int result = bp_receive(sap, &testDlv, BP_NONBLOCKING);
    
    if (result < 0) {
        if (errno == 22) { // Invalid argument - ION likely restarted
            return ION_STATUS_ERROR;
        }
        return ION_STATUS_UNKNOWN;
    }
    
    // If we got a bundle, release it and return success
    if (testDlv.result == BpPayloadPresent) {
        bp_release_delivery(&testDlv, 1);
    }
    
    return ION_STATUS_RUNNING;
}

/**
 * Process received CBOR message - main entry point for CBOR message handling
 */
void processCborMessage(DtnexConfig *config, unsigned char *buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) {
        debug_log(config, "❌ Invalid CBOR buffer (null or zero size)");
        return;
    }
    
    debug_log(config, "🔍 Processing CBOR message (%d bytes)", bufferSize);
    
    // Debug: Print hex dump of received CBOR data
    if (config->debugMode) {
        printf("📊 CBOR hex dump (%d bytes): ", bufferSize);
        for (int i = 0; i < bufferSize; i++) {
            printf("%02x", buffer[i]);
            if (i < bufferSize - 1) printf(" ");
        }
        printf("\n");
    }
    
    // Decode the CBOR message
    int result = decodeCborMessage(config, buffer, bufferSize);
    if (result < 0) {
        log_message_error(config, "Failed to decode CBOR message - unknown bundle format");
        return;
    }
    
    debug_log(config, "✅ CBOR message processed successfully");
}

/**
 * Skip a single CBOR element manually
 */
/**
 * Manual CBOR string decoder to replace ION's cbor_decode_text_string
 */
int manualDecodeCborString(char *buffer, int maxLen, unsigned char **cursor, unsigned int *bytesBuffered) {
    if (*bytesBuffered < 1) {
        return 0; // No bytes left
    }
    
    unsigned char byte = **cursor;
    unsigned char majorType = byte >> 5;
    unsigned char additionalInfo = byte & 0x1F;
    
    // We expect text strings (major type 3)
    if (majorType != 3) {
        return 0; // Not a text string
    }
    
    (*cursor)++;
    (*bytesBuffered)--;
    
    unsigned long stringLen = 0;
    
    if (additionalInfo < 24) {
        // Length is directly in the additional info
        stringLen = additionalInfo;
    } else if (additionalInfo == 24) {
        // 1-byte follows
        if (*bytesBuffered < 1) return 0;
        stringLen = **cursor;
        (*cursor)++;
        (*bytesBuffered)--;
    } else if (additionalInfo == 25) {
        // 2-bytes follow (big-endian)
        if (*bytesBuffered < 2) return 0;
        stringLen = ((*cursor)[0] << 8) | (*cursor)[1];
        (*cursor) += 2;
        (*bytesBuffered) -= 2;
    } else {
        return 0; // Unsupported format for now
    }
    
    // Check if we have enough bytes and buffer space
    if (*bytesBuffered < stringLen || stringLen >= maxLen) {
        return 0;
    }
    
    // Copy string data
    memcpy(buffer, *cursor, stringLen);
    buffer[stringLen] = '\0';
    
    (*cursor) += stringLen;
    (*bytesBuffered) -= stringLen;
    
    return 1;
}

/**
 * Manual CBOR integer decoder to replace ION's cbor_decode_integer
 */
int manualDecodeCborInteger(unsigned long *value, unsigned char **cursor, unsigned int *bytesBuffered) {
    if (*bytesBuffered < 1) {
        return 0; // No bytes left
    }
    
    unsigned char byte = **cursor;
    unsigned char majorType = byte >> 5;
    unsigned char additionalInfo = byte & 0x1F;
    
    // We expect positive integers (major type 0)
    if (majorType != 0) {
        return 0; // Not a positive integer
    }
    
    (*cursor)++;
    (*bytesBuffered)--;
    
    if (additionalInfo < 24) {
        // Value is directly in the additional info
        *value = additionalInfo;
        return 1;
    } else if (additionalInfo == 24) {
        // 1-byte follows
        if (*bytesBuffered < 1) return 0;
        *value = **cursor;
        (*cursor)++;
        (*bytesBuffered)--;
        return 1;
    } else if (additionalInfo == 25) {
        // 2-bytes follow (big-endian)
        if (*bytesBuffered < 2) return 0;
        *value = ((*cursor)[0] << 8) | (*cursor)[1];
        (*cursor) += 2;
        (*bytesBuffered) -= 2;
        return 1;
    } else if (additionalInfo == 26) {
        // 4-bytes follow (big-endian)
        if (*bytesBuffered < 4) return 0;
        *value = (((unsigned long)(*cursor)[0]) << 24) | 
                 (((unsigned long)(*cursor)[1]) << 16) |
                 (((unsigned long)(*cursor)[2]) << 8) | 
                 (*cursor)[3];
        (*cursor) += 4;
        (*bytesBuffered) -= 4;
        return 1;
    } else if (additionalInfo == 27) {
        // 8-bytes follow (big-endian) - only use lower 32 bits
        if (*bytesBuffered < 8) return 0;
        *value = (((unsigned long)(*cursor)[4]) << 24) | 
                 (((unsigned long)(*cursor)[5]) << 16) |
                 (((unsigned long)(*cursor)[6]) << 8) | 
                 (*cursor)[7];
        (*cursor) += 8;
        (*bytesBuffered) -= 8;
        return 1;
    }
    
    return 0; // Unsupported format
}

int skipCborElement(unsigned char **cursor, unsigned int *bytesBuffered) {
    if (*bytesBuffered < 1) {
        return 0; // No bytes left
    }
    
    unsigned char majorType = (**cursor) >> 5;
    unsigned char additionalInfo = (**cursor) & 0x1F;
    (*cursor)++;
    (*bytesBuffered)--;
    
    int bytesToSkip = 0;
    
    switch (additionalInfo) {
        case 24: // 1-byte follows
            bytesToSkip = 1;
            break;
        case 25: // 2-bytes follow  
            bytesToSkip = 2;
            break;
        case 26: // 4-bytes follow
            bytesToSkip = 4;
            break;
        case 27: // 8-bytes follow
            bytesToSkip = 8;
            break;
        default:
            if (additionalInfo < 24) {
                // Value is in the additional info
                bytesToSkip = 0;
            } else {
                return 0; // Unsupported format
            }
            break;
    }
    
    // For text strings and byte strings (major types 2,3), skip the content
    if (majorType == 2 || majorType == 3) {
        // Get the length
        int length = additionalInfo;
        if (additionalInfo >= 24) {
            if (*bytesBuffered < bytesToSkip) return 0;
            // Read length from following bytes
            length = 0;
            for (int i = 0; i < bytesToSkip; i++) {
                length = (length << 8) | **cursor;
                (*cursor)++;
                (*bytesBuffered)--;
            }
            bytesToSkip = 0; // Already consumed length bytes
        }
        bytesToSkip += length; // Add content bytes
    }
    
    // Skip the required bytes
    if (*bytesBuffered < bytesToSkip) {
        return 0; // Not enough bytes
    }
    
    *cursor += bytesToSkip;
    *bytesBuffered -= bytesToSkip;
    
    return 1; // Success
}

/**
 * Decode and process CBOR message format
 */
int decodeCborMessage(DtnexConfig *config, unsigned char *buffer, int bufferSize) {
    unsigned char *cursor = buffer;
    unsigned int bytesBuffered = bufferSize;
    uvast arraySize = 0;
    uvast version, timestamp, expireTime, origin, from;
    char messageType[2] = {0};
    uvast textSize;
    unsigned char nonce[DTNEX_NONCE_SIZE];
    uvast nonceSize = DTNEX_NONCE_SIZE;
    unsigned char receivedHmac[DTNEX_HMAC_SIZE];
    uvast hmacSize = DTNEX_HMAC_SIZE;
    
    // Decode main array (should have 9 elements)
    if (cbor_decode_array_open(&arraySize, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode CBOR array header");
        return -1;
    }
    
    if (arraySize != 9) {
        debug_log(config, "❌ Invalid CBOR message format - expected 9 elements, got %lu", arraySize);
        return -1;
    }
    
    // 1. Decode version
    if (cbor_decode_integer(&version, CborAny, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode message version");
        return -1;
    }
    
    if (version != DTNEX_PROTOCOL_VERSION) {
        debug_log(config, "❌ Unsupported protocol version %lu (expected %d)", version, DTNEX_PROTOCOL_VERSION);
        return -1;
    }
    
    // 2. Decode message type
    textSize = 1;
    if (cbor_decode_text_string(messageType, &textSize, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode message type");
        return -1;
    }
    
    // 3. Decode timestamp
    if (cbor_decode_integer(&timestamp, CborAny, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode timestamp");
        return -1;
    }
    
    // 4. Decode expire time
    if (cbor_decode_integer(&expireTime, CborAny, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode expire time");
        return -1;
    }
    
    // Check if message has expired
    time_t currentTime = time(NULL);
    if (currentTime > expireTime) {
        debug_log(config, "❌ Message expired (%ld seconds ago)", currentTime - expireTime);
        return -1;
    }
    
    // 5. Decode origin node
    if (cbor_decode_integer(&origin, CborAny, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode origin node");
        return -1;
    }
    
    // 6. Decode from node
    if (cbor_decode_integer(&from, CborAny, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode from node");
        return -1;
    }
    
    // 7. Decode nonce
    if (cbor_decode_byte_string(nonce, &nonceSize, &cursor, &bytesBuffered) <= 0) {
        debug_log(config, "❌ Failed to decode nonce");
        return -1;
    }
    
    if (nonceSize != DTNEX_NONCE_SIZE) {
        debug_log(config, "❌ Invalid nonce size %lu (expected %d)", nonceSize, DTNEX_NONCE_SIZE);
        return -1;
    }
    
    // Debug: Show what we've decoded so far
    debug_log(config, "🔍 Decoded so far: version=%lu, type='%s', timestamp=%lu, expire=%lu, origin=%lu, from=%lu", 
        version, messageType, timestamp, expireTime, origin, from);
    
    // Check for nonce replay
    if (isNonceDuplicate(nonce, origin)) {
        debug_log(config, "❌ Duplicate nonce detected - replay attack or old message");
        return -1;
    }
    
    // We'll set hmacPosition right before HMAC decoding
    unsigned char *hmacPosition;
    // Store the data array position for later processing
    unsigned char *dataArrayPosition = cursor;
    
    // Variables to store extracted contact/metadata data for later processing
    ContactInfo extractedContact = {0};
    StructuredMetadata extractedMetadata = {0};
    int hasExtractedData = 0;
    
    // Skip over the data section to find HMAC
    uvast dataArraySize;
    debug_log(config, "🔍 About to decode data array at cursor position (remaining bytes: %d)", bytesBuffered);
    if (cursor < buffer + bufferSize) {
        debug_log(config, "🔍 Next byte for data array: 0x%02x", *cursor);
    }
    
    // ION's CBOR decoder is failing, implement manual array header decode
    if (bytesBuffered < 1 || cursor >= buffer + bufferSize) {
        debug_log(config, "❌ No bytes left for array decode");
        return -1;
    }
    
    unsigned char arrayByte = *cursor;
    debug_log(config, "🔍 Manual CBOR decode: array byte = 0x%02x", arrayByte);
    
    // Check if it's a CBOR array (major type 4, bits 7-5 = 100)
    if ((arrayByte & 0xE0) == 0x80) {
        // Get array size from lower 5 bits
        dataArraySize = arrayByte & 0x1F;
        if (dataArraySize < 24) {
            // Simple array size (0-23)
            cursor++;
            bytesBuffered--;
            debug_log(config, "✅ Manual array decode successful, size: %lu", dataArraySize);
        } else {
            debug_log(config, "❌ Complex array format (size >= 24) not implemented");
            return -1;
        }
    } else {
        debug_log(config, "❌ Expected CBOR array (0x8x), got 0x%02x", arrayByte);
        return -1;
    }
    
    // Extract data elements based on message type and then skip them for HMAC verification
    if (messageType[0] == 'c') {
        // Contact message: extract 3 elements (nodeA, nodeB, duration)
        debug_log(config, "🔍 Extracting 3 contact elements manually");
        
        // Create a cursor copy for extraction
        unsigned char *extractCursor = cursor;
        unsigned int extractBytesBuffered = bytesBuffered;
        
        // Extract nodeA, nodeB, and duration using manual CBOR decoder
        unsigned long tempNodeA, tempNodeB, tempDuration;
        if (manualDecodeCborInteger(&tempNodeA, &extractCursor, &extractBytesBuffered) &&
            manualDecodeCborInteger(&tempNodeB, &extractCursor, &extractBytesBuffered) &&
            manualDecodeCborInteger(&tempDuration, &extractCursor, &extractBytesBuffered)) {
            
            extractedContact.nodeA = (unsigned long)tempNodeA;
            extractedContact.nodeB = (unsigned long)tempNodeB;
            extractedContact.duration = (unsigned short)tempDuration;
            hasExtractedData = 1;
            debug_log(config, "✅ Extracted contact: %lu↔%lu (duration=%d min)", 
                      extractedContact.nodeA, extractedContact.nodeB, extractedContact.duration);
        } else {
            debug_log(config, "❌ Failed to extract contact elements");
        }
        
        // Now skip the elements for HMAC verification
        for (int i = 0; i < dataArraySize && i < 3; i++) {
            if (!skipCborElement(&cursor, &bytesBuffered)) {
                debug_log(config, "❌ Failed to skip contact element %d", i);
                return -1;
            }
        }
        debug_log(config, "✅ Successfully skipped contact elements for HMAC");
        
    } else if (messageType[0] == 'm') {
        // Metadata message: extract elements and then skip them
        debug_log(config, "🔍 Extracting %lu metadata elements manually", dataArraySize);
        
        // Create a cursor copy for extraction
        unsigned char *extractCursor = cursor;
        unsigned int extractBytesBuffered = bytesBuffered;
        
        // Try to extract metadata based on array size
        unsigned long tempNodeId, tempLat, tempLon;
        
        debug_log(config, "🔍 Metadata array size: %lu", dataArraySize);
        
        if (dataArraySize == 3) {
            // Standard metadata without GPS: [nodeId, name, contact]
            debug_log(config, "🔍 Attempting to decode standard metadata (3 elements)");
            if (manualDecodeCborInteger(&tempNodeId, &extractCursor, &extractBytesBuffered)) {
                debug_log(config, "🔍 Decoded tempNodeId: %lu", tempNodeId);
                if (manualDecodeCborString(extractedMetadata.name, MAX_NODE_NAME_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                    manualDecodeCborString(extractedMetadata.contact, MAX_CONTACT_INFO_LENGTH - 1, &extractCursor, &extractBytesBuffered)) {
                    
                    extractedMetadata.nodeId = (unsigned long)tempNodeId;
                    extractedMetadata.latitude = 0;
                    extractedMetadata.longitude = 0;
                    hasExtractedData = 1;
                    debug_log(config, "✅ Extracted standard metadata: node=%lu, name=%s, contact=%s", 
                              extractedMetadata.nodeId, extractedMetadata.name, extractedMetadata.contact);
                } else {
                    debug_log(config, "❌ Failed to decode name/contact strings");
                }
            } else {
                debug_log(config, "❌ Failed to decode nodeId integer");
            }
        } else if (dataArraySize == 5) {
            // Full metadata with GPS: [nodeId, name, contact, lat, lon]
            debug_log(config, "🔍 Attempting to decode full metadata with GPS (5 elements)");
            if (manualDecodeCborInteger(&tempNodeId, &extractCursor, &extractBytesBuffered)) {
                debug_log(config, "🔍 Decoded tempNodeId: %lu", tempNodeId);
                if (manualDecodeCborString(extractedMetadata.name, MAX_NODE_NAME_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                    manualDecodeCborString(extractedMetadata.contact, MAX_CONTACT_INFO_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                    manualDecodeCborInteger(&tempLat, &extractCursor, &extractBytesBuffered) &&
                    manualDecodeCborInteger(&tempLon, &extractCursor, &extractBytesBuffered)) {
                    
                    extractedMetadata.nodeId = (unsigned long)tempNodeId;
                    // Strings are already null-terminated by manualDecodeCborString
                    extractedMetadata.latitude = (int)tempLat;
                    extractedMetadata.longitude = (int)tempLon;
                    hasExtractedData = 1;
                    debug_log(config, "✅ Extracted full metadata with GPS: node=%lu, name=%s, contact=%s, lat=%d, lon=%d", 
                              extractedMetadata.nodeId, extractedMetadata.name, extractedMetadata.contact,
                              extractedMetadata.latitude, extractedMetadata.longitude);
                } else {
                    debug_log(config, "❌ Failed to decode name/contact/GPS data");
                }
            } else {
                debug_log(config, "❌ Failed to decode nodeId integer in GPS metadata");
            }
        } else if (dataArraySize == 2) {
            // Legacy metadata without nodeId: [name, contact]
            debug_log(config, "🔍 Attempting to decode legacy metadata (2 elements)");
            if (manualDecodeCborString(extractedMetadata.name, MAX_NODE_NAME_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                manualDecodeCborString(extractedMetadata.contact, MAX_CONTACT_INFO_LENGTH - 1, &extractCursor, &extractBytesBuffered)) {
                
                extractedMetadata.nodeId = origin;  // Use origin as nodeId for legacy format
                extractedMetadata.latitude = 0;  // No GPS data
                extractedMetadata.longitude = 0;
                hasExtractedData = 1;
                debug_log(config, "✅ Extracted legacy metadata: node=%lu, name=%s, contact=%s", 
                          extractedMetadata.nodeId, extractedMetadata.name, extractedMetadata.contact);
            } else {
                debug_log(config, "❌ Failed to decode legacy name/contact");
            }
        } else if (dataArraySize == 4) {
            // Legacy GPS metadata without nodeId: [name, contact, latitude, longitude]
            debug_log(config, "🔍 Attempting to decode legacy GPS metadata (4 elements)");
            if (manualDecodeCborString(extractedMetadata.name, MAX_NODE_NAME_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                manualDecodeCborString(extractedMetadata.contact, MAX_CONTACT_INFO_LENGTH - 1, &extractCursor, &extractBytesBuffered) &&
                manualDecodeCborInteger(&tempLat, &extractCursor, &extractBytesBuffered) &&
                manualDecodeCborInteger(&tempLon, &extractCursor, &extractBytesBuffered)) {
                
                extractedMetadata.nodeId = origin;  // Use origin as nodeId for legacy format
                extractedMetadata.latitude = (int)tempLat;
                extractedMetadata.longitude = (int)tempLon;
                hasExtractedData = 1;
                debug_log(config, "✅ Extracted legacy GPS metadata: node=%lu, name=%s, contact=%s, lat=%d, lon=%d", 
                          extractedMetadata.nodeId, extractedMetadata.name, extractedMetadata.contact,
                          extractedMetadata.latitude, extractedMetadata.longitude);
            } else {
                debug_log(config, "❌ Failed to decode legacy name/contact/GPS data");
            }
        } else {
            debug_log(config, "❌ Unsupported metadata array size: %lu (expected 2, 3, 4, or 5)", dataArraySize);
        }
        
        if (!hasExtractedData) {
            debug_log(config, "❌ Failed to extract metadata elements");
        }
        
        // Now skip the elements for HMAC verification
        for (int i = 0; i < dataArraySize; i++) {
            if (!skipCborElement(&cursor, &bytesBuffered)) {
                debug_log(config, "❌ Failed to skip metadata element %d", i);
                return -1;
            }
        }
        debug_log(config, "✅ Successfully skipped metadata elements for HMAC");
    } else {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), "Unknown message type '%c'", messageType[0]);
        log_message_error(config, error_msg);
        return -1;
    }
    
    // 9. Set HMAC position and decode HMAC manually
    hmacPosition = cursor; // Now cursor points to the start of the HMAC field
    debug_log(config, "🔍 Decoding HMAC manually (bytes left: %d)", bytesBuffered);
    if (bytesBuffered < 1) {
        debug_log(config, "❌ No bytes left for HMAC");
        return -1;
    }
    
    unsigned char hmacByte = *cursor;
    if ((hmacByte & 0xE0) == 0x40) { // CBOR byte string (major type 2)
        int hmacLength = hmacByte & 0x1F;
        cursor++;
        bytesBuffered--;
        
        if (hmacLength != DTNEX_HMAC_SIZE) {
            debug_log(config, "❌ Invalid HMAC length %d (expected %d)", hmacLength, DTNEX_HMAC_SIZE);
            return -1;
        }
        
        if (bytesBuffered < hmacLength) {
            debug_log(config, "❌ Not enough bytes for HMAC content");
            return -1;
        }
        
        memcpy(receivedHmac, cursor, hmacLength);
        hmacSize = hmacLength;
        cursor += hmacLength;
        bytesBuffered -= hmacLength;
        
        debug_log(config, "✅ HMAC decoded manually, size: %d", hmacLength);
    } else {
        debug_log(config, "❌ Expected CBOR byte string for HMAC, got 0x%02x", hmacByte);
        return -1;
    }
    
    if (hmacSize != DTNEX_HMAC_SIZE) {
        debug_log(config, "❌ Invalid HMAC size %lu (expected %d)", hmacSize, DTNEX_HMAC_SIZE);
        return -1;
    }
    
    // Verify HMAC (calculate HMAC over message without the HMAC field)
    int messageWithoutHmacSize = hmacPosition - buffer;
    debug_log(config, "🔍 HMAC calculation: message size without HMAC = %d bytes", messageWithoutHmacSize);
    if (!verifyHmac(config, buffer, messageWithoutHmacSize, receivedHmac, config->presSharedNetworkKey)) {
        debug_log(config, "❌ HMAC verification failed - wrong hash or corrupted message");
        return -1;
    }
    
    debug_log(config, "✅ CBOR message authentication verified");
    
    // Add nonce to cache to prevent replay
    addNonceToCache(nonce, origin);
    
    // Process the message based on type using pre-extracted data
    if (messageType[0] == 'c') {
        // Use pre-extracted contact data instead of re-decoding
        if (!hasExtractedData) {
            debug_log(config, "❌ No contact data was extracted during parsing");
            return -1;
        }
        
        debug_log(config, "🔍 Processing extracted contact data: %lu↔%lu (duration=%d)", 
                  extractedContact.nodeA, extractedContact.nodeB, extractedContact.duration);
        
        return processCborContactMessage(config, nonce, timestamp, expireTime, origin, from, &extractedContact);
    } else if (messageType[0] == 'm') {
        // Use pre-extracted metadata instead of re-decoding
        if (!hasExtractedData) {
            debug_log(config, "❌ No metadata was extracted during parsing");
            return -1;
        }
        
        debug_log(config, "🔍 Processing extracted metadata: node=%lu, name=%s, contact=%s", 
                  extractedMetadata.nodeId, extractedMetadata.name, extractedMetadata.contact);
        
        return processCborMetadataMessage(config, nonce, timestamp, expireTime, origin, from, &extractedMetadata);
    }
    
    log_message_error(config, "Unknown message type or failed to extract data");
    return -1;
}

/**
 * Process CBOR contact message
 */
int processCborContactMessage(DtnexConfig *config, unsigned char *nonce, time_t timestamp, time_t expireTime, 
                             unsigned long origin, unsigned long from, ContactInfo *contact) {
    log_message_received(config, origin, from, "contact", contact->nodeA, contact->nodeB, NULL);
    
    // Skip processing our own messages
    if (origin == config->nodeId) {
        debug_log(config, "⏭️ Skipping own contact message");
        return 0;
    }
    
    // Create contact in ION
    char contactCmd[256];
    time_t startTime = timestamp;
    time_t endTime = startTime + (contact->duration * 60);  // Convert minutes to seconds
    
    // Format times for ION contact command
    struct tm *startTm = gmtime(&startTime);
    struct tm *endTm = gmtime(&endTime);
    
    snprintf(contactCmd, sizeof(contactCmd),
        "a contact +%04d/%02d/%02d-%02d:%02d:%02d +%04d/%02d/%02d-%02d:%02d:%02d %lu %lu 100000",
        startTm->tm_year + 1900, startTm->tm_mon + 1, startTm->tm_mday,
        startTm->tm_hour, startTm->tm_min, startTm->tm_sec,
        endTm->tm_year + 1900, endTm->tm_mon + 1, endTm->tm_mday,
        endTm->tm_hour, endTm->tm_min, endTm->tm_sec,
        contact->nodeA, contact->nodeB);
    
    debug_log(config, "🔗 Adding contact: %s", contactCmd);
    
    // Add contact directly using ION's internal API instead of system call
    PsmAddress cxaddr = 0;
    uint32_t regionNbr = 1;  // Default region number (same as _regionNbr(NULL) in ionadmin)
    size_t xmitRate = 100000; // Default transmission rate
    float confidence = 1.0;   // Default confidence
    int announce = 0;         // Don't announce to region
    
    // Add bidirectional contacts as per user requirement (A->B and B->A)
    PsmAddress cxaddr2 = 0;
    int result1 = rfx_insert_contact(regionNbr, startTime, endTime, 
                                     (uvast)contact->nodeA, (uvast)contact->nodeB, 
                                     xmitRate, confidence, &cxaddr, announce);
    
    int result2 = rfx_insert_contact(regionNbr, startTime, endTime, 
                                     (uvast)contact->nodeB, (uvast)contact->nodeA, 
                                     xmitRate, confidence, &cxaddr2, announce);
    
    if (result1 == 0 && result2 == 0) {
        dtnex_log("✅ Bidirectional contacts %lu↔%lu added successfully", contact->nodeA, contact->nodeB);
        
        // Add bidirectional ranges for the contact (as per original bash implementation)
        // Range distance of 1 second OWLT (One-Way Light Time)
        PsmAddress rxaddr1 = 0, rxaddr2 = 0;
        unsigned int owlt = 1;  // 1 second range distance
        int announceRange = 0;  // Don't announce to region
        
        // Add range A->B
        int rangeResult1 = rfx_insert_range(startTime, endTime, 
                                            (uvast)contact->nodeA, (uvast)contact->nodeB, 
                                            owlt, &rxaddr1, announceRange);
        
        // Add range B->A  
        int rangeResult2 = rfx_insert_range(startTime, endTime, 
                                            (uvast)contact->nodeB, (uvast)contact->nodeA, 
                                            owlt, &rxaddr2, announceRange);
        
        if (rangeResult1 == 0 && rangeResult2 == 0) {
            debug_log(config, "✅ Bidirectional ranges %lu↔%lu added successfully", contact->nodeA, contact->nodeB);
        } else {
            debug_log(config, "⚠️ Range addition results: %lu->%lu: %d, %lu->%lu: %d", 
                     contact->nodeA, contact->nodeB, rangeResult1,
                     contact->nodeB, contact->nodeA, rangeResult2);
        }
    } else {
        // Handle partial success or errors
        if (result1 == 0 || result2 == 0) {
            debug_log(config, "⚠️ Partial contact success: %lu->%lu: %d, %lu->%lu: %d", 
                     contact->nodeA, contact->nodeB, result1,
                     contact->nodeB, contact->nodeA, result2);
        }
        
        if (result1 == 9 || result2 == 9) {
            debug_log(config, "ℹ️ Contact %lu↔%lu already exists (overlapping contact ignored)", contact->nodeA, contact->nodeB);
        } else if (result1 == 11 || result2 == 11) {
            debug_log(config, "ℹ️ Contact %lu↔%lu is duplicate (already in region)", contact->nodeA, contact->nodeB);
        } else if (result1 != 0 && result2 != 0) {
            dtnex_log("❌ Failed to add bidirectional contacts %lu↔%lu (errors: %d, %d)", contact->nodeA, contact->nodeB, result1, result2);
        }
    }
    
    // Forward CBOR contact message to all neighbors (except origin and sender)
    forwardCborContactMessage(config, nonce, timestamp, expireTime, origin, from, contact);
    
    return 0;
}

/**
 * Process CBOR metadata message
 */
int processCborMetadataMessage(DtnexConfig *config, unsigned char *nonce, time_t timestamp, time_t expireTime,
                              unsigned long origin, unsigned long from, StructuredMetadata *metadata) {
    log_message_received(config, origin, from, "metadata", metadata->nodeId, 0, metadata->name);
    
    // Skip processing our own messages
    if (origin == config->nodeId) {
        debug_log(config, "⏭️ Skipping own metadata message");
        return 0;
    }
    
    // Convert GPS coordinates back to decimal if present
    if (metadata->latitude != 0 || metadata->longitude != 0) {
        double lat = (double)metadata->latitude / GPS_PRECISION_FACTOR;
        double lon = (double)metadata->longitude / GPS_PRECISION_FACTOR;
        debug_log(config, "🌍 GPS: %.6f, %.6f", lat, lon);
    }
    
    // Update node metadata in our records
    char fullMetadata[MAX_METADATA_LENGTH];
    if (metadata->latitude != 0 || metadata->longitude != 0) {
        double lat = (double)metadata->latitude / GPS_PRECISION_FACTOR;
        double lon = (double)metadata->longitude / GPS_PRECISION_FACTOR;
        snprintf(fullMetadata, sizeof(fullMetadata), "%s,%s,%.6f,%.6f", 
                metadata->name, metadata->contact, lat, lon);
    } else {
        snprintf(fullMetadata, sizeof(fullMetadata), "%s,%s", 
                metadata->name, metadata->contact);
    }
    
    updateNodeMetadata(config, metadata->nodeId, fullMetadata);
    
    dtnex_log("✅ Node %lu metadata updated", metadata->nodeId);
    
    // Forward CBOR metadata message to all neighbors (except origin and sender)
    forwardCborMetadataMessage(config, nonce, timestamp, expireTime, origin, from, metadata);
    
    return 0;
}

/**
 * Forward CBOR contact message to all neighbors (except origin and sender)
 */
void forwardCborContactMessage(DtnexConfig *config, unsigned char *originalNonce, time_t timestamp, 
                              time_t expireTime, unsigned long origin, unsigned long from, ContactInfo *contact) {
    Plan plans[MAX_PLANS];
    int planCount = 0;
    char destEid[MAX_EID_LENGTH];
    unsigned char cborBuffer[MAX_CBOR_BUFFER];
    int messageSize;
    
    // Forwarding contact message - individual forwards logged in the loop
    
    // Get current neighbor list
    getplanlist(config, plans, &planCount);
    
    if (planCount <= 0) {
        debug_log(config, "⏭️ No neighbors to forward contact message to");
        return;
    }
    
    // Forward to all neighbors except origin, sender, and ourselves
    for (int i = 0; i < planCount; i++) {
        unsigned long neighborId = plans[i].planId;
        
        // Skip forwarding to origin, sender, or ourselves
        if (neighborId == origin || neighborId == from || neighborId == config->nodeId) {
            continue;
        }
        
        // Create CBOR forwarded message with new nonce but preserve original timestamp/expireTime
        ContactInfo forwardContact = *contact; // Copy contact info
        
        // Create modified contact message for forwarding (from=our nodeId)
        unsigned char *cursor = cborBuffer;
        unsigned char newNonce[DTNEX_NONCE_SIZE];
        int bytesWritten = 0;
        
        // Generate new nonce for forwarded message
        generateNonce(newNonce);
        
        // Encode forwarded CBOR message
        bytesWritten += cbor_encode_array_open(9, &cursor);
        bytesWritten += cbor_encode_integer(DTNEX_PROTOCOL_VERSION, &cursor);
        bytesWritten += cbor_encode_text_string("c", 1, &cursor);
        bytesWritten += cbor_encode_integer(timestamp, &cursor);
        bytesWritten += cbor_encode_integer(expireTime, &cursor);
        bytesWritten += cbor_encode_integer(origin, &cursor);  // Keep original origin
        bytesWritten += cbor_encode_integer(config->nodeId, &cursor);  // Update "from" to our node
        bytesWritten += cbor_encode_byte_string(newNonce, DTNEX_NONCE_SIZE, &cursor);
        
        // Contact data
        bytesWritten += cbor_encode_array_open(3, &cursor);
        bytesWritten += cbor_encode_integer(forwardContact.nodeA, &cursor);
        bytesWritten += cbor_encode_integer(forwardContact.nodeB, &cursor);
        bytesWritten += cbor_encode_integer(forwardContact.duration, &cursor);
        
        // Calculate HMAC over everything except HMAC itself
        unsigned char hmac[DTNEX_HMAC_SIZE];
        calculateHmac(cborBuffer, bytesWritten, config->presSharedNetworkKey, hmac);
        bytesWritten += cbor_encode_byte_string(hmac, DTNEX_HMAC_SIZE, &cursor);
        
        // Send forwarded CBOR bundle
        sprintf(destEid, "ipn:%lu.%s", neighborId, config->serviceNr);
        sendCborBundle(destEid, cborBuffer, bytesWritten, config->bundleTTL);
        
        log_message_forwarded(config, origin, from, neighborId, "contact", 
                             contact->nodeA, contact->nodeB, NULL);
    }
}

/**
 * Forward CBOR metadata message to all neighbors (except origin and sender)
 */
void forwardCborMetadataMessage(DtnexConfig *config, unsigned char *originalNonce, time_t timestamp,
                               time_t expireTime, unsigned long origin, unsigned long from, StructuredMetadata *metadata) {
    Plan plans[MAX_PLANS];
    int planCount = 0;
    char destEid[MAX_EID_LENGTH];
    unsigned char cborBuffer[MAX_CBOR_BUFFER];
    int messageSize;
    
    // Forwarding metadata message - individual forwards logged in the loop
    
    // Get current neighbor list
    getplanlist(config, plans, &planCount);
    
    if (planCount <= 0) {
        debug_log(config, "⏭️ No neighbors to forward metadata message to");
        return;
    }
    
    // Forward to all neighbors except origin, sender, and ourselves
    for (int i = 0; i < planCount; i++) {
        unsigned long neighborId = plans[i].planId;
        
        // Skip forwarding to origin, sender, or ourselves
        if (neighborId == origin || neighborId == from || neighborId == config->nodeId) {
            continue;
        }
        
        // Create CBOR forwarded message with new nonce but preserve original timestamp/expireTime
        unsigned char *cursor = cborBuffer;
        unsigned char newNonce[DTNEX_NONCE_SIZE];
        int bytesWritten = 0;
        
        // Generate new nonce for forwarded message
        generateNonce(newNonce);
        
        // Encode forwarded CBOR message
        bytesWritten += cbor_encode_array_open(9, &cursor);
        bytesWritten += cbor_encode_integer(DTNEX_PROTOCOL_VERSION, &cursor);
        bytesWritten += cbor_encode_text_string("m", 1, &cursor);
        bytesWritten += cbor_encode_integer(timestamp, &cursor);
        bytesWritten += cbor_encode_integer(expireTime, &cursor);
        bytesWritten += cbor_encode_integer(origin, &cursor);  // Keep original origin
        bytesWritten += cbor_encode_integer(config->nodeId, &cursor);  // Update "from" to our node
        bytesWritten += cbor_encode_byte_string(newNonce, DTNEX_NONCE_SIZE, &cursor);
        
        // Metadata data
        int metadataElements = 3; // nodeId, name, contact always present
        if (metadata->latitude != 0 || metadata->longitude != 0) {
            metadataElements = 5; // nodeId, name, contact, lat, lon
        }
        
        bytesWritten += cbor_encode_array_open(metadataElements, &cursor);
        bytesWritten += cbor_encode_integer(metadata->nodeId, &cursor);
        bytesWritten += cbor_encode_text_string(metadata->name, strlen(metadata->name), &cursor);
        bytesWritten += cbor_encode_text_string(metadata->contact, strlen(metadata->contact), &cursor);
        
        if (metadataElements == 5) {
            bytesWritten += cbor_encode_integer(metadata->latitude, &cursor);
            bytesWritten += cbor_encode_integer(metadata->longitude, &cursor);
        }
        
        // Calculate HMAC over everything except HMAC itself
        unsigned char hmac[DTNEX_HMAC_SIZE];
        calculateHmac(cborBuffer, bytesWritten, config->presSharedNetworkKey, hmac);
        bytesWritten += cbor_encode_byte_string(hmac, DTNEX_HMAC_SIZE, &cursor);
        
        // Send forwarded CBOR bundle
        sprintf(destEid, "ipn:%lu.%s", neighborId, config->serviceNr);
        sendCborBundle(destEid, cborBuffer, bytesWritten, config->bundleTTL);
        
        log_message_forwarded(config, origin, from, neighborId, "metadata", 
                             metadata->nodeId, 0, metadata->name);
    }
}