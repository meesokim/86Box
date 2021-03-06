/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the network module.
 *
 * NOTE		The definition of the netcard_t is currently not optimal;
 *		it should be malloc'ed and then linked to the NETCARD def.
 *		Will be done later.
 *
 * Version:	@(#)network.c	1.0.19	2017/11/04
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../plat.h"
#include "../ui.h"
#include "network.h"
#include "net_ne2000.h"


static netcard_t net_cards[] = {
    { "None",			"none",		NULL,
      NULL,			NULL					},
#if defined(DEV_BRANCH) && defined(USE_NE1000)
    { "Novell NE1000",		"ne1k",		&ne1000_device,
      NULL,			NULL					},
#endif
    { "Novell NE2000",		"ne2k",		&ne2000_device,
      NULL,			NULL					},
    { "Realtek RTL8029AS",	"ne2kpci",	&rtl8029as_device,
      NULL,			NULL					},
    { "",			"",		NULL,
      NULL,			NULL					}
};


/* Global variables. */
int		network_type;
int		network_ndev;
int		network_card;
netdev_t	network_devs[32];
char		network_pcap[512];
#ifdef ENABLE_NIC_LOG
int		nic_do_log = ENABLE_NIC_LOG;
#endif
static mutex_t	*network_mutex;


static struct {
    volatile int	busy,
			queue_in_use;

    event_t		*wake_poll_thread,
			*poll_complete,
			*queue_not_in_use;
} poll_data;


void
network_wait(uint8_t wait)
{
    if (wait)
	thread_wait_mutex(network_mutex);
      else
	thread_release_mutex(network_mutex);
}


void
network_poll(void)
{
    while (poll_data.busy)
	thread_wait_event(poll_data.wake_poll_thread, -1);

    thread_reset_event(poll_data.wake_poll_thread);
}


void
network_busy(uint8_t set)
{
    poll_data.busy = !!set;

    if (! set)
	thread_set_event(poll_data.wake_poll_thread);
}


void
network_end(void)
{
    thread_set_event(poll_data.poll_complete);
}


/*
 * Initialize the configured network cards.
 *
 * This function gets called only once, from the System
 * Platform initialization code (currently in pc.c) to
 * set our local stuff to a known state.
 */
void
network_init(void)
{
    int i;

    /* Initialize to a known state. */
    network_type = NET_TYPE_NONE;
    network_card = 0;

    /* Create a first device entry that's always there, as needed by UI. */
    strcpy(network_devs[0].device, "none");
    strcpy(network_devs[0].description, "None");
    network_ndev = 1;

    /* Initialize the Pcap system module, if present. */
    i = net_pcap_prepare(&network_devs[network_ndev]);
    if (i > 0)
	network_ndev += i;
}


/*
 * Attach a network card to the system.
 *
 * This function is called by a hardware driver ("card") after it has
 * finished initializing itself, to link itself to the platform support
 * modules.
 */
void
network_attach(void *dev, uint8_t *mac, NETRXCB rx)
{
    if (network_card == 0) return;

    /* Save the card's info. */
    net_cards[network_card].priv = dev;
    net_cards[network_card].rx = rx;
    net_cards[network_card].mac = mac;

    /* Create the network events. */
    poll_data.wake_poll_thread = thread_create_event();
    poll_data.poll_complete = thread_create_event();

    /* Activate the platform module. */
    switch(network_type) {
	case NET_TYPE_PCAP:
		(void)net_pcap_reset(&net_cards[network_card]);
		break;

	case NET_TYPE_SLIRP:
		(void)net_slirp_reset(&net_cards[network_card]);
		break;
    }
}


/* Stop any network activity. */
void
network_close(void)
{
    /* If already closed, do nothing. */
    if (network_mutex == NULL) return;

    /* Force-close the PCAP module. */
    net_pcap_close();

    /* Force-close the SLIRP module. */
    net_slirp_close();
 
    /* Close the network events. */
    if (poll_data.wake_poll_thread != NULL) {
	thread_destroy_event(poll_data.wake_poll_thread);
	poll_data.wake_poll_thread = NULL;
    }
    if (poll_data.poll_complete != NULL) {
	thread_destroy_event(poll_data.poll_complete);
	poll_data.poll_complete = NULL;
    }

    /* Close the network thread mutex. */
    thread_close_mutex(network_mutex);
    network_mutex = NULL;

    pclog("NETWORK: closed.\n");
}


/*
 * Reset the network card(s).
 *
 * This function is called each time the system is reset,
 * either a hard reset (including power-up) or a soft reset
 * including C-A-D reset.)  It is responsible for connecting
 * everything together.
 */
void
network_reset(void)
{
    int i = -1;

    pclog("NETWORK: reset (type=%d, card=%d)\n", network_type, network_card);
    ui_sb_update_icon(SB_NETWORK, 0);

    /* Just in case.. */
    network_close();

    /* If no active card, we're done. */
    if ((network_type==NET_TYPE_NONE) || (network_card==0)) return;

    network_mutex = thread_create_mutex(L"86Box.NetMutex");

    /* Initialize the platform module. */
    switch(network_type) {
	case NET_TYPE_PCAP:
		i = net_pcap_init();
		break;

	case NET_TYPE_SLIRP:
		i = net_slirp_init();
		break;
    }

    if (i < 0) {
	/* Tell user we can't do this (at the moment.) */
	ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2139);

	// FIXME: we should ask in the dialog if they want to
	//	  reconfigure or quit, and throw them into the
	//	  Settings dialog if yes.

	/* Disable network. */
	network_type = NET_TYPE_NONE;

	return;
    }

    pclog("NETWORK: set up for %s, card='%s'\n",
	(network_type==NET_TYPE_SLIRP)?"SLiRP":"Pcap",
			net_cards[network_card].name);

    /* Add the (new?) card to the I/O system. */
    if (net_cards[network_card].device) {
	pclog("NETWORK: adding device '%s'\n",
		net_cards[network_card].name);
	device_add(net_cards[network_card].device);
    }
}


/* Transmit a packet to one of the network providers. */
void
network_tx(uint8_t *bufp, int len)
{
    ui_sb_update_icon(SB_NETWORK, 1);

    switch(network_type) {
	case NET_TYPE_PCAP:
		net_pcap_in(bufp, len);
		break;

	case NET_TYPE_SLIRP:
		net_slirp_in(bufp, len);
		break;
    }

    ui_sb_update_icon(SB_NETWORK, 0);
}


int
network_dev_to_id(char *devname)
{
    int i = 0;

    for (i=0; i<network_ndev; i++) {
	if (! strcmp(network_devs[i].device, devname)) {
		return(i);
	}
    }

    /* If no match found, assume "none". */
    return(0);
}


/* UI */
int
network_available(void)
{
    if ((network_type == NET_TYPE_NONE) || (network_card == 0)) return(0);

    return(1);
}


/* UI */
int
network_card_available(int card)
{
    if (net_cards[card].device)
	return(device_available(net_cards[card].device));

    return(1);
}


/* UI */
char *
network_card_getname(int card)
{
    return(net_cards[card].name);
}


/* UI */
device_t *
network_card_getdevice(int card)
{
    return(net_cards[card].device);
}


/* UI */
int
network_card_has_config(int card)
{
    if (! net_cards[card].device) return(0);

    return(net_cards[card].device->config ? 1 : 0);
}


/* UI */
char *
network_card_get_internal_name(int card)
{
    return(net_cards[card].internal_name);
}


/* UI */
int
network_card_get_from_internal_name(char *s)
{
    int c = 0;
	
    while (strlen(net_cards[c].internal_name)) {
	if (! strcmp(net_cards[c].internal_name, s))
			return(c);
	c++;
    }
	
    return(-1);
}
