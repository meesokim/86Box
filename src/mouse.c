/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common driver module for MOUSE devices.
 *
 * TODO:	Add the Genius bus- and serial mouse.
 *		Remove the '3-button' flag from mouse types.
 *
 * Version:	@(#)mouse.c	1.0.18	2017/12/14
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "mouse.h"


typedef struct {
    const char  *internal_name;
    device_t    *device;
} mouse_t;


int	mouse_type = 0;
int	mouse_x,
	mouse_y,
	mouse_z,
	mouse_buttons;


static device_t mouse_none_device = {
    "None",
    0, MOUSE_TYPE_NONE,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};
static device_t mouse_internal_device = {
    "Internal Mouse",
    0, MOUSE_TYPE_INTERNAL,
    NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};


static mouse_t mouse_devices[] = {
    { "none",		&mouse_none_device	},
    { "internal",	&mouse_internal_device	},
    { "logibus",	&mouse_logibus_device	},
    { "msbus",		&mouse_msinport_device	},
#if 0
    { "genibus",	&mouse_genibus_device	},
#endif
    { "mssystems",	&mouse_mssystems_device	},
    { "msserial",	&mouse_msserial_device	},
    { "lserial",	&mouse_lserial_device	},
    { "mswheel",	&mouse_mswheel_device	},
    { "ps2",		&mouse_ps2_device	},
    { NULL,		NULL			}
};


static device_t	*mouse_curr;
static void	*mouse_priv;
static int	mouse_nbut;


/* Initialize the mouse module. */
void
mouse_init(void)
{
    /* Initialize local data. */
    mouse_x = mouse_y = mouse_z = 0;
    mouse_buttons = 0x00;

    mouse_type = MOUSE_TYPE_NONE;
    mouse_curr = NULL;
    mouse_priv = NULL;
    mouse_nbut = 0;
}


void
mouse_close(void)
{
    if (mouse_curr == NULL) return;

    mouse_curr = NULL;
    mouse_priv = NULL;
    mouse_nbut = 0;
}


void
mouse_reset(void)
{
    pclog("MOUSE: reset(type=%d, '%s')\n",
	mouse_type, mouse_devices[mouse_type].device->name);

    /* Close previous mouse, if open. */
    mouse_close();

    /* Clear local data. */
    mouse_x = mouse_y = mouse_z = 0;
    mouse_buttons = 0x00;

    /* If no mouse configured, we're done. */
    if (mouse_type == 0) return;

    mouse_curr = mouse_devices[mouse_type].device;

    if (mouse_curr != NULL)
	mouse_priv = device_add(mouse_curr);
}


/* Callback from the hardware driver. */
void
mouse_set_buttons(int buttons)
{
    mouse_nbut = buttons;
}


void
mouse_process(void)
{
    static int poll_delay = 2;

    if (mouse_curr == NULL) return;

    if (--poll_delay) return;

    mouse_poll();

    if (mouse_curr->available != NULL) {
    	mouse_curr->available(mouse_x,mouse_y,mouse_z,mouse_buttons, mouse_priv);

	/* Reset mouse deltas. */
	mouse_x = mouse_y = mouse_z = 0;
    }

    poll_delay = 2;
}


void
mouse_set_poll(int (*func)(int,int,int,int,void *), void *arg)
{
    if (mouse_type != MOUSE_TYPE_INTERNAL) return;

    mouse_curr->available = func;
    mouse_priv = arg;
}


char *
mouse_get_name(int mouse)
{
    return((char *)mouse_devices[mouse].device->name);
}


char *
mouse_get_internal_name(int mouse)
{
    return((char *)mouse_devices[mouse].internal_name);
}


int
mouse_get_from_internal_name(char *s)
{
    int c = 0;

    while (mouse_devices[c].internal_name != NULL) {
	if (! strcmp((char *)mouse_devices[c].internal_name, s)) {
		return(c);
	}
	c++;
    }

    return(0);
}


device_t *
mouse_get_device(int mouse)
{
    return(mouse_devices[mouse].device);
}


int
mouse_get_buttons(void)
{
    return(mouse_nbut);
}


/* Return number of MOUSE types we know about. */
int
mouse_get_ndev(void)
{
    return((sizeof(mouse_devices)/sizeof(mouse_t)) - 1);
}
