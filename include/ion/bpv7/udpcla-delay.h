/*
 	udpcla-delay.h:	common definitions for UDP delay-based convergence layer
			adapter modules.

	Based on: udpcla.h by Ted Piotrowski, APL and Scott Burleigh, JPL

	Copyright (c) 2025
									*/
#ifndef _UDPCLA_DELAY_H_
#define _UDPCLA_DELAY_H_

#include "bpP.h"
#include "udpcla.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UDPCLA_BUFSZ		((256 * 256) - 1)
#define SPEED_OF_LIGHT		299792.458 /* km/s */

/* Constants for astronomical calculations */
#define EARTH_ORBITAL_RADIUS  149598000  /* km, 1 AU */
#define MARS_ORBITAL_RADIUS   227939200  /* km, 1.52 AU */
#define MARS_ORBITAL_PERIOD   686.971    /* Earth days */
#define EARTH_ORBITAL_PERIOD  365.256    /* Earth days */
#define MOON_MIN_DISTANCE     356500     /* km - perigee */
#define MOON_MAX_DISTANCE     406700     /* km - apogee */
#define MOON_AVG_DISTANCE     384400     /* km - average */
#define MOON_ORBITAL_PERIOD   27.322     /* Earth days */

/* Delay calculation functions */
extern int	calculateDelay(char *delaySpec, float *calculatedDelay);
extern float	calculateMarsDistance(void);
extern float	calculateMoonDistance(void);
extern char*    extractDelayParam(char *endpointSpec, char *cleanEndpointSpec, int cleanEndpointSize);

/* UDP delay functions - inbound */
extern int	receiveBytesByUDPDelay(int bundleSocket,
			struct sockaddr_in *fromAddr, char *into, int length, 
			float delay);

/* UDP delay functions - outbound */
extern int	sendBytesByUDPDelay(int *bundleSocket, char *from, int length,
			struct sockaddr *socketName, float delay);
extern int	sendBundleByUDPDelay(struct sockaddr *socketName, int *bundleSocket,
			unsigned int bundleLength, Object bundleZco,
			unsigned char *buffer, float delay);

#ifdef __cplusplus
}
#endif

#endif	/* _UDPCLA_DELAY_H */