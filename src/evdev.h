#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef EVDEV_H
#define EVDEV_H

#include <linux/types.h>

#include <xorg-server.h>
#include <xf86Xinput.h>
#include <xf86_OSproc.h>


typedef struct
{
    ValuatorMask *abs_vals;
} EvdevRec, *EvdevPtr;

#endif
