/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows raw keyboard input handler.
 *
 * Version:	@(#)win_keyboard.c	1.0.4	2017/10/24
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 */
#define UNICODE
#define  _WIN32_WINNT 0x0501
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../86box.h"
#include "../device.h"
#include "../keyboard.h"
#include "../plat.h"
#include "win.h"


static uint16_t	scancode_map[65536];


/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
static UINT16
convert_scan_code(UINT16 scan_code)
{
    switch (scan_code) {
	case 0xE001:
		return 0xF001;
	case 0xE002:
		return 0xF002;
	case 0xE0AA:
		return 0xF003;
	case 0xE005:
		return 0xF005;
	case 0xE006:
		return 0xF006;
	case 0xE007:
		return 0xF007;
	case 0xE071:
		return 0xF008;
	case 0xE072:
		return 0xF009;
	case 0xE07F:
		return 0xF00A;
	case 0xE0E1:
		return 0xF00B;
	case 0xE0EE:
		return 0xF00C;
	case 0xE0F1:
		return 0xF00D;
	case 0xE0FE:
		return 0xF00E;
	case 0xE0EF:
		return 0xF00F;

	default:
		return scan_code;
    }
}


void
keyboard_getkeymap(void)
{
    WCHAR *keyName = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout";
    WCHAR *valueName = L"Scancode Map";
    unsigned char buf[32768];
    DWORD bufSize;
    HKEY hKey;
    int j;
    UINT32 *bufEx2;
    int scMapCount;
    UINT16 *bufEx;
    int scancode_unmapped;
    int scancode_mapped;

    /* First, prepare the default scan code map list which is 1:1.
     * Remappings will be inserted directly into it.
     * 65536 bytes so scan codes fit in easily and it's easy to find
     * what each maps too, since each array element is a scan code
     * and provides for E0, etc. ones too.
     */
    for (j = 0; j < 65536; j++)
	scancode_map[j] = convert_scan_code(j);

    /* Get the scan code remappings from:
    HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout */
    bufSize = 32768;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, 1, &hKey) == ERROR_SUCCESS) {
	if (RegQueryValueEx(hKey, valueName, NULL, NULL, buf, &bufSize) == ERROR_SUCCESS) {
		bufEx2 = (UINT32 *) buf;
		scMapCount = bufEx2[2];
		if ((bufSize != 0) && (scMapCount != 0)) {
			bufEx = (UINT16 *) (buf + 12);
			for (j = 0; j < scMapCount*2; j += 2) {
 				/* Each scan code is 32-bit: 16 bits of remapped scan code,
 				   and 16 bits of original scan code. */
  				scancode_unmapped = bufEx[j + 1];
  				scancode_mapped = bufEx[j];

				scancode_mapped = convert_scan_code(scancode_mapped);

				/* Fixes scan code map logging. */
				scancode_map[scancode_unmapped] = scancode_mapped;
			}
		}
	}
	RegCloseKey(hKey);
    }
}


void
keyboard_handle(LPARAM lParam, int infocus)
{
    uint32_t ri_size = 0;
    UINT size;
    RAWINPUT *raw;
    USHORT scancode;

    if (! infocus) return;

    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL,
		    &size, sizeof(RAWINPUTHEADER));

    raw = malloc(size);
    if (raw == NULL) return;

    /* Here we read the raw input data for the keyboard */
    ri_size = GetRawInputData((HRAWINPUT)(lParam), RID_INPUT,
			      raw, &size, sizeof(RAWINPUTHEADER));
    if (ri_size != size) return;

    /* If the input is keyboard, we process it */
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
	RAWKEYBOARD rawKB = raw->data.keyboard;
	scancode = rawKB.MakeCode;

	/* If it's not a scan code that starts with 0xE1 */
	if (!(rawKB.Flags & RI_KEY_E1)) {
		if (rawKB.Flags & RI_KEY_E0) {
			scancode |= (0xE0 << 8);
		}

		/* Remap it according to the list from the Registry */
		scancode = scancode_map[scancode];

		if ((scancode >> 8) == 0xF0) {
			/* Extended key code in disambiguated format */
			scancode |= 0x100;
		} else if ((scancode >> 8) == 0xE0) {
			/* Normal extended key code */
			scancode |= 0x80;
		}

		/* If it's not 0 (therefore not 0xE1, 0xE2, etc),
		   send it to the PC keyboard. */
		if (!(scancode & 0xf00))
			keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode & 0x1ff);
	} else {
		if (rawKB.MakeCode == 0x1D) {
			scancode = 0xFF;
		}
		if (!(scancode & 0xf00))
			keyboard_input(!(rawKB.Flags & RI_KEY_BREAK), scancode & 0x1ff);
	}
    }

    free(raw);
}
