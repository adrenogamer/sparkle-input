#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef EVDEV_H
#define EVDEV_H

#include <linux/types.h>

#include <xorg-server.h>
#include <xf86Xinput.h>
#include <xf86_OSproc.h>

#include "shared_resource.h"
#include "sparkle_shared.h"


typedef struct
{
    ValuatorMask *abs_vals;
    struct shared_resource_t *shared_info;
    struct sparkle_shared_t *shared;
} EvdevRec, *EvdevPtr;

#endif

