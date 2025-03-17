/**
 * Simple test receiver for DTNEX
 * Accepts incoming bundles on a specific endpoint and displays them
 */

#include <bp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
// For direct endpoint management
#include "../ione-code/bpv7/library/bpP.h"
// For SDR functions
#include <zco.h>

#define MAX_LINE_LENGTH 1024

BpSAP sap;
static int running = 1;

void handleQuit(int signum)
{
    // Re-arm signal handler
    isignal(SIGINT, handleQuit);
    
    // Set flag to exit cleanly
    printf("\nReceived interrupt signal, shutting down...\n");
    running = 0;
    
    // Interrupt BP operations
    bp_interrupt(sap);
}

int main(int argc, char **argv)
{
    char eid[64];
    BpDelivery dlv;
    
    // Set up signal handler
    isignal(SIGINT, handleQuit);
    
    // Check command line arguments
    if (argc != 2) {
        printf("Usage: %s <service_number>\n", argv[0]);
        printf("Example: %s 12162\n", argv[0]);
        return 1;
    }
    
    // Connect to BP
    if (bp_attach() < 0) {
        printf("Error: Could not attach to BP\n");
        return 1;
    }
    
    // Get our node number from ION
    Sdr ionsdr = getIonsdr();
    if (ionsdr == NULL) {
        printf("Error: Can't get ION SDR\n");
        bp_detach();
        return 1;
    }
    
    unsigned long nodeNbr = 0;
    
    // Start transaction to read ION configuration
    if (sdr_begin_xn(ionsdr) < 0) {
        printf("Error: Can't begin ION transaction\n");
        bp_detach();
        return 1;
    }
    
    // Get the node number from ION
    IonDB iondb;
    Object iondbObject = getIonDbObject();
    if (iondbObject == 0) {
        printf("Error: Can't get ION DB object\n");
        sdr_exit_xn(ionsdr);
        bp_detach();
        return 1;
    }
    
    // Read the iondb object
    sdr_read(ionsdr, (char *)&iondb, iondbObject, sizeof(IonDB));
    nodeNbr = iondb.ownNodeNbr;
    sdr_exit_xn(ionsdr);
    
    if (nodeNbr == 0) {
        printf("Error: Invalid node number (0) from ION\n");
        bp_detach();
        return 1;
    }
    
    // Format our EID
    sprintf(eid, "ipn:%lu.%s", nodeNbr, argv[1]);
    printf("Using endpoint: %s\n", eid);
    
    // Register the endpoint (if not already registered)
    if (addEndpoint(eid, EnqueueBundle, NULL) < 0) {
        printf("Note: Could not register endpoint - it may already be registered\n");
    }
    
    // Open our endpoint
    if (bp_open(eid, &sap) < 0) {
        printf("Error: Could not open endpoint\n");
        bp_detach();
        return 1;
    }
    
    printf("Ready to receive bundles. Press Ctrl+C to exit.\n");
    
    // Main receiving loop
    while (running) {
        // Wait for a bundle
        if (bp_receive(sap, &dlv, BP_BLOCKING) < 0) {
            if (running) { // Only show error if not shutting down
                printf("Error receiving bundle: %d\n", errno);
            }
            continue;
        }
        
        // Process the received bundle
        if (dlv.result == BpPayloadPresent) {
            printf("\n\033[32m[RECEIVED] Bundle from %s\033[0m\n", 
                dlv.bundleSourceEid ? dlv.bundleSourceEid : "unknown");
            
            // Get the SDR
            Sdr sdr = bp_get_sdr();
            
            // Get content length
            if (sdr_begin_xn(sdr) < 0) {
                printf("Error starting SDR transaction\n");
                bp_release_delivery(&dlv, 1);
                continue;
            }
            
            int contentLength = zco_source_data_length(sdr, dlv.adu);
            sdr_exit_xn(sdr);
            
            // Only process if content length is reasonable
            if (contentLength < MAX_LINE_LENGTH) {
                // Initialize ZCO reader
                ZcoReader reader;
                zco_start_receiving(dlv.adu, &reader);
                
                // Buffer for content
                char buffer[MAX_LINE_LENGTH];
                
                // Read the content
                if (sdr_begin_xn(sdr) < 0) {
                    printf("Error starting SDR read transaction\n");
                    bp_release_delivery(&dlv, 1);
                    continue;
                }
                
                int len = zco_receive_source(sdr, &reader, contentLength, buffer);
                if (sdr_end_xn(sdr) < 0 || len < 0) {
                    printf("Error reading bundle content\n");
                    bp_release_delivery(&dlv, 1);
                    continue;
                }
                
                // Null-terminate the buffer
                buffer[len] = '\0';
                
                // Print the message with color based on type
                if (strstr(buffer, " c ")) {
                    printf("\033[36m[CONTACT] %s\033[0m\n", buffer);
                } else if (strstr(buffer, " m ")) {
                    printf("\033[35m[METADATA] %s\033[0m\n", buffer);
                } else {
                    printf("\033[33m[MESSAGE] %s\033[0m\n", buffer);
                }
            } else {
                printf("\033[31m[ERROR] Bundle content too large (%d bytes)\033[0m\n", contentLength);
            }
        } else if (dlv.result == BpReceptionInterrupted) {
            if (running) { // Only show if not shutting down
                printf("Bundle reception interrupted\n");
            }
        } else if (dlv.result == BpEndpointStopped) {
            printf("Endpoint stopped\n");
            running = 0;
        }
        
        // Release the delivery
        bp_release_delivery(&dlv, 1);
    }
    
    // Clean up
    bp_close(sap);
    bp_detach();
    
    printf("Test receiver terminated.\n");
    return 0;
}