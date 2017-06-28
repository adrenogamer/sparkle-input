#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sparkleinput.h"

#include <linux/version.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <xf86.h>
#include <xf86Xinput.h>
#include <xorgVersion.h>
#include <xkbsrv.h>

#include <X11/Xatom.h>
#include <xserver-properties.h>

#include "sion_keymap.h"


//=============================================================================

static int EvdevProc(DeviceIntPtr device, int what);
static int EvdevOpenDevice(InputInfoPtr pInfo);
static void EvdevCloseDevice(InputInfoPtr pInfo);
static int EvdevOn(DeviceIntPtr);
static int EvdevOff(DeviceIntPtr);

//=============================================================================

static void
EvdevKbdCtrl(DeviceIntPtr device, KeybdCtrl *ctrl)
{
}

static int EvdevSwitchMode(ClientPtr client, DeviceIntPtr device, int mode)
{
    return Success;
}

static void
EvdevApplyCalibration(EvdevPtr pEvdev, ValuatorMask *mask)
{
    int i;

    int pixmapWidth = pEvdev->shared->pixmapWidth;
    int pixmapHeight = pEvdev->shared->pixmapHeight;
    int surfaceWidth = pEvdev->shared->surfaceWidth;
    int surfaceHeight = pEvdev->shared->surfaceHeight;

    if (pixmapWidth == 0 || pixmapHeight == 0 || surfaceWidth == 0 || surfaceHeight == 0)
    {
        return;
    }


    for (i = 0; i <= 1; i++)
    {
        int val;
        int max1, max2;

        if (!valuator_mask_isset(mask, i))
            continue;

        val = valuator_mask_get(mask, i);

        if (i == 0) { //X
            max1 = surfaceWidth;
            max2 = pixmapWidth;
        } else { //Y
            max1 = surfaceHeight;
            max2 = pixmapHeight;
        }

        val = xf86ScaleAxis(val, max2, 0, max1, 0);

        valuator_mask_set(mask, i, val);
    }
}

//=============================================================================

static void
EvdevReadInput(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = pInfo->private;

    struct event_t
    {
        uint32_t type;
        uint32_t action;
        uint32_t arg1;
        uint32_t arg2;
    };

    struct event_t _event;

    read(pInfo->fd, &_event, sizeof(struct event_t));

    if (_event.type == 0)
    {
        valuator_mask_set(pEvdev->abs_vals, 0, _event.arg1);
        valuator_mask_set(pEvdev->abs_vals, 1, _event.arg2);
        EvdevApplyCalibration(pEvdev, pEvdev->abs_vals);

        if (_event.action == 0)
        {
            xf86PostMotionEventM(pInfo->dev, Absolute, pEvdev->abs_vals);
            xf86PostButtonEvent(pInfo->dev, Absolute, 1, 1, 0, 0);
        }
        else if (_event.action == 1)
        {
            xf86PostMotionEventM(pInfo->dev, Absolute, pEvdev->abs_vals);
            xf86PostButtonEvent(pInfo->dev, Absolute, 1, 0, 0, 0);
        }
        else if (_event.action == 2)
        {
            xf86PostMotionEventM(pInfo->dev, Absolute, pEvdev->abs_vals);
        }
        else if (_event.action == 261)
        {
            xf86PostButtonEvent(pInfo->dev, Absolute, 3, 1, 0, 0);
        }
        else if (_event.action == 262)
        {
            xf86PostButtonEvent(pInfo->dev, Absolute, 3, 0, 0, 0);
        }
        else
        {
            xf86IDrvMsg(pInfo, X_INFO, "Unknown action %d\n", _event.action);
        }

        valuator_mask_zero(pEvdev->abs_vals);
    }
    else if (_event.type == 1)
    {
        xf86IDrvMsg(pInfo, X_INFO, "Keymap %d -> %d\n", _event.arg1, sion_keymap[_event.arg1]);

        if (_event.action == 0)
        {
            xf86PostKeyboardEvent(pInfo->dev, sion_keymap[_event.arg1], 1);
        }
        else if (_event.action == 1)
        {
            xf86PostKeyboardEvent(pInfo->dev, sion_keymap[_event.arg1], 0);
        }
    }
    else if (_event.type == 2)
    {
        if (_event.action == 0)
        {
            xf86PostKeyboardEvent(pInfo->dev, _event.arg1, 1);
        }
        else if (_event.action == 1)
        {
            xf86PostKeyboardEvent(pInfo->dev, _event.arg1, 0);
        }
    }

    //xf86PostKeyboardEvent(pInfo->dev, pEvdev->queue[i].detail.key, pEvdev->queue[i].val);
    //xf86PostButtonEvent(pInfo->dev, Absolute, pEvdev->queue[i].detail.key, pEvdev->queue[i].val, 0, 0);
    //xf86PostTouchEvent(pInfo->dev, pEvdev->queue[i].detail.touch, pEvdev->queue[i].val, 0, pEvdev->queue[i].touchMask);

}

//=============================================================================

static int
EvdevAddKeyClass(DeviceIntPtr device)
{
    int rc = Success;
    XkbRMLVOSet rmlvo = {0},
                defaults;
    InputInfoPtr pInfo;

    pInfo = device->public.devicePrivate;

    XkbGetRulesDflts(&defaults);

    xf86ReplaceStrOption(pInfo->options, "xkb_rules", "sparkleinput");
    rmlvo.rules = xf86SetStrOption(pInfo->options, "xkb_rules", NULL);
    rmlvo.model = xf86SetStrOption(pInfo->options, "xkb_model", defaults.model);
    rmlvo.layout = xf86SetStrOption(pInfo->options, "xkb_layout", defaults.layout);
    rmlvo.variant = xf86SetStrOption(pInfo->options, "xkb_variant", defaults.variant);
    rmlvo.options = xf86SetStrOption(pInfo->options, "xkb_options", defaults.options);

    if (!InitKeyboardDeviceStruct(device, &rmlvo, NULL, EvdevKbdCtrl))
        rc = !Success;

    XkbFreeRMLVOSet(&rmlvo, FALSE);
    XkbFreeRMLVOSet(&defaults, FALSE);

    return rc;
}

static int
EvdevAddAbsValuatorClass(DeviceIntPtr device, int num_scroll_axes)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;
    int axis;
    Atom *atoms;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    pEvdev->abs_vals = valuator_mask_new(2);

    if (!pEvdev->abs_vals)
    {
        xf86IDrvMsg(pInfo, X_ERROR, "failed to allocate valuator masks.\n");
        goto out;
    }

    atoms = malloc(2 * sizeof(Atom));

    if (!InitValuatorClassDeviceStruct(device, 2, atoms, GetMotionHistorySize(), Absolute))
    {
        xf86IDrvMsg(pInfo, X_ERROR, "failed to initialize valuator class device.\n");
        goto out;
    }

    for (axis = 0; axis < 2; axis++)
    {
        int min = 0;
        int max = 0;

        xf86InitValuatorAxisStruct(device, axis, atoms[axis], min, max, 1, 0, 1, Absolute);

        xf86InitValuatorDefaults(device, axis);
    }

    free(atoms);

    return Success;

out:
    valuator_mask_free(&pEvdev->abs_vals);
    return !Success;
}

static int
EvdevAddButtonClass(DeviceIntPtr device)
{
    //InputInfoPtr pInfo;
    //EvdevPtr pEvdev;

    //pInfo = device->public.devicePrivate;
    //pEvdev = pInfo->private;

    if (!InitButtonClassDeviceStruct(device, 0, NULL, NULL))
        return !Success;

    return Success;
}

//=============================================================================

static int
EvdevPreInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    EvdevPtr pEvdev;
    int rc = BadAlloc;

    pEvdev = calloc(sizeof(EvdevRec), 1);
    if (!pEvdev)
        goto error;

    pInfo->private = pEvdev;
    pInfo->type_name = "UNKNOWN";
    pInfo->device_control = EvdevProc;
    pInfo->read_input = EvdevReadInput;
    pInfo->switch_mode = EvdevSwitchMode;

    rc = EvdevOpenDevice(pInfo);
    if (rc != Success)
        goto error;


    return Success;

error:
    EvdevCloseDevice(pInfo);
    return rc;
}

static void
EvdevUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    EvdevPtr pEvdev = pInfo->private;
    free(pEvdev);
    pInfo->private = NULL;
    xf86DeleteInput(pInfo, flags);
}

//=============================================================================

static int
EvdevInit(DeviceIntPtr device)
{
    //InputInfoPtr pInfo;
    //EvdevPtr pEvdev;

    //pInfo = device->public.devicePrivate;
    //pEvdev = pInfo->private;

	EvdevAddKeyClass(device);
	EvdevAddButtonClass(device);
    EvdevAddAbsValuatorClass(device, 0);

    return Success;
}

static int
EvdevOn(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    //EvdevPtr pEvdev;
    int rc = Success;

    pInfo = device->public.devicePrivate;
    //pEvdev = pInfo->private;

    /* after PreInit fd is still open */
    rc = EvdevOpenDevice(pInfo);
    if (rc != Success)
        return rc;

    xf86FlushInput(pInfo->fd);
    xf86AddEnabledDevice(pInfo);

    device->public.on = TRUE;

    return Success;
}

static int
EvdevOff(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    //EvdevPtr pEvdev;

    pInfo = device->public.devicePrivate;

    if (pInfo->fd != -1)
    {
        xf86RemoveEnabledDevice(pInfo);
        EvdevCloseDevice(pInfo);
    }

    device->public.on = FALSE;

    return Success;
}

static int
EvdevOpenDevice(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = pInfo->private;

    if (pInfo->fd < 0)
    {
        pEvdev->shared_info = shared_resource_open("/dev/sparkle_info", sizeof(struct sparkle_shared_t), 0, (void **)&pEvdev->shared);
        if (pEvdev->shared_info == NULL)
        {
            xf86IDrvMsg(pInfo, X_ERROR, "Failed to open shared resources\n");
            return !Success;
        }

        mkfifo("/dev/sparkle_input", 0666);

        do {
            pInfo->fd = open("/dev/sparkle_input", O_RDWR | O_NONBLOCK, 0);
        } while (pInfo->fd < 0 && errno == EINTR);

        if (pInfo->fd < 0)
        {
            xf86IDrvMsg(pInfo, X_ERROR, "Unable to open evdev device\n");
            return BadValue;
        }

        fchmod(pInfo->fd, 0666);

        //XXX Clear pipe?
    }

    return Success;
}

static void
EvdevCloseDevice(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = pInfo->private;

    if (pInfo->fd >= 0)
    {
        close(pInfo->fd);
        pInfo->fd = -1;
        shared_resource_close(pEvdev->shared_info);
    }
}

static int
EvdevProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    switch (what)
    {
    case DEVICE_INIT:
	    return EvdevInit(device);

    case DEVICE_ON:
        return EvdevOn(device);

    case DEVICE_OFF:
        EvdevOff(device);
	    break;

    case DEVICE_CLOSE:
	    xf86IDrvMsg(pInfo, X_INFO, "Close\n");
        EvdevCloseDevice(pInfo);
        valuator_mask_free(&pEvdev->abs_vals);
	    break;

    default:
        return BadValue;
    }

    return Success;
}

//=============================================================================

_X_EXPORT InputDriverRec SPARKLEINPUT = {
    1,
    "sparkleinput",
    NULL,
    EvdevPreInit,
    EvdevUnInit,
    NULL,
    NULL,
#ifdef XI86_DRV_CAP_SERVER_FD
    XI86_DRV_CAP_SERVER_FD
#endif
};

//=============================================================================

static void
EvdevUnplug(pointer	p)
{
}

static pointer
EvdevPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
    xf86AddInputDriver(&SPARKLEINPUT, module, 0);
    return module;
}

static XF86ModuleVersionInfo EvdevVersionRec =
{
    "sparkleinput",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData sparkleinputModuleData =
{
    &EvdevVersionRec,
    EvdevPlug,
    EvdevUnplug
};

//=============================================================================
