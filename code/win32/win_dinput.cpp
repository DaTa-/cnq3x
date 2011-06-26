/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file has parts of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../client/client.h"
#include "win_local.h"
#include "win_dinput.h"

#define	DIRECTINPUT_VERSION	0x0300

#include <dinput.h>

/*
============================================================

DIRECT INPUT MOUSE CONTROL

============================================================
*/
/*
#undef DEFINE_GUID

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(GUID_SysMouse,   0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_XAxis,   0xA36D02E0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_YAxis,   0xA36D02E1,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_ZAxis,   0xA36D02E2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
*/

#define DINPUT_BUFFERSIZE           16
#define iDirectInputCreate(a,b,c,d)	pDirectInputCreate(a,b,c,d)

typedef HRESULT (WINAPI *directInputCreateFuncPtr)(HINSTANCE hinst, DWORD dwVersion,
	LPDIRECTINPUT * lplpDirectInput, LPUNKNOWN punkOuter);

directInputCreateFuncPtr pDirectInputCreate;

static HINSTANCE hInstDI;

typedef struct MYDATA {
	LONG  lX;                   // X axis goes here
	LONG  lY;                   // Y axis goes here
	LONG  lZ;                   // Z axis goes here
	BYTE  bButtonA;             // One button goes here
	BYTE  bButtonB;             // Another button goes here
	BYTE  bButtonC;             // Another button goes here
	BYTE  bButtonD;             // Another button goes here
} MYDATA;

static DIOBJECTDATAFORMAT rgodf[] = {
  { &GUID_XAxis,    FIELD_OFFSET(MYDATA, lX),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_YAxis,    FIELD_OFFSET(MYDATA, lY),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_ZAxis,    FIELD_OFFSET(MYDATA, lZ),       0x80000000 | DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonA), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonB), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonC), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonD), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
};

#define NUM_OBJECTS (sizeof(rgodf) / sizeof(rgodf[0]))

// NOTE TTimo: would be easier using c_dfDIMouse or c_dfDIMouse2 
static DIDATAFORMAT	df = {
	sizeof(DIDATAFORMAT),       // this structure
	sizeof(DIOBJECTDATAFORMAT), // size of object data format
	DIDF_RELAXIS,               // absolute axis coordinates
	sizeof(MYDATA),             // device data size
	NUM_OBJECTS,                // number of objects
	rgodf,                      // and here they are
};

static LPDIRECTINPUT		g_pdi;
static LPDIRECTINPUTDEVICE	g_pMouse;

void IN_DIMouse( int *mx, int *my );

/*
========================
IN_InitDIMouse
========================
*/
qboolean IN_InitDIMouse( void ) {
    HRESULT		hr;
	int			x, y;
	DIPROPDWORD	dipdw = {
		{
			sizeof(DIPROPDWORD),        // diph.dwSize
			sizeof(DIPROPHEADER),       // diph.dwHeaderSize
			0,                          // diph.dwObj
			DIPH_DEVICE,                // diph.dwHow
		},
		DINPUT_BUFFERSIZE,              // dwData
	};

	Com_Printf( "Initializing DirectInput...\n");

	if (!hInstDI) {
		hInstDI = LoadLibrary("dinput.dll");
		
		if (hInstDI == NULL) {
			Com_Printf ("Couldn't load dinput.dll\n");
			return qfalse;
		}
	}

	if (!pDirectInputCreate) {
		pDirectInputCreate = (directInputCreateFuncPtr) GetProcAddress(hInstDI,"DirectInputCreateA");

		if (!pDirectInputCreate) {
			Com_Printf ("Couldn't get DI proc addr\n");
			return qfalse;
		}
	}

	// register with DirectInput and get an IDirectInput to play with.
	hr = iDirectInputCreate( g_wv.hInstance, DIRECTINPUT_VERSION, &g_pdi, NULL);

	if (FAILED(hr)) {
		Com_Printf ("iDirectInputCreate failed\n");
		return qfalse;
	}

	// obtain an interface to the system mouse device.
	hr = IDirectInput_CreateDevice(g_pdi, GUID_SysMouse, &g_pMouse, NULL);

	if (FAILED(hr)) {
		Com_Printf ("Couldn't open DI mouse device\n");
		return qfalse;
	}

	// set the data format to "mouse format".
	hr = IDirectInputDevice_SetDataFormat(g_pMouse, &df);

	if (FAILED(hr)) 	{
		Com_Printf ("Couldn't set DI mouse format\n");
		return qfalse;
	}

	// set the cooperativity level.
	hr = IDirectInputDevice_SetCooperativeLevel(g_pMouse, g_wv.hWnd,
			DISCL_EXCLUSIVE | DISCL_FOREGROUND);

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
	if (FAILED(hr)) {
		Com_Printf ("Couldn't set DI coop level\n");
		return qfalse;
	}


	// set the buffer size to DINPUT_BUFFERSIZE elements.
	// the buffer size is a DWORD property associated with the device
	hr = IDirectInputDevice_SetProperty(g_pMouse, DIPROP_BUFFERSIZE, &dipdw.diph);

	if (FAILED(hr)) {
		Com_Printf ("Couldn't set DI buffersize\n");
		return qfalse;
	}

	// clear any pending samples
	IN_DIMouse( &x, &y );
	IN_DIMouse( &x, &y );

	return qtrue;
}

/*
==========================
IN_ShutdownDIMouse
==========================
*/
void IN_ShutdownDIMouse( void ) {
    if (g_pMouse) {
		IDirectInputDevice_Release(g_pMouse);
		g_pMouse = NULL;
	}

    if (g_pdi) {
		IDirectInput_Release(g_pdi);
		g_pdi = NULL;
	}
}

/*
==========================
IN_ActivateDIMouse
==========================
*/
qboolean IN_ActivateDIMouse( void ) {
	HRESULT		hr;

	if (!g_pMouse) {
		return qfalse;
	}

	// we may fail to reacquire if the window has been recreated
	hr = IDirectInputDevice_Acquire( g_pMouse );
	if (FAILED(hr)) {
		return IN_InitDIMouse();
	}

	return qtrue;
}

/*
==========================
IN_DeactivateDIMouse
==========================
*/
void IN_DeactivateDIMouse( void ) {
	if (!g_pMouse) {
		return;
	}
	IDirectInputDevice_Unacquire( g_pMouse );
}


/*
===================
IN_DIMouse
===================
*/
void IN_DIMouse( int *mx, int *my ) {
	DIDEVICEOBJECTDATA	od;
	DIMOUSESTATE		state;
	DWORD				dwElements;
	HRESULT				hr;
  int value;
	static float		oldSysTime;

	if ( !g_pMouse ) {
		return;
	}

	// fetch new events
	for (;;)
	{
		dwElements = 1;

		hr = IDirectInputDevice_GetDeviceData(g_pMouse,
				sizeof(DIDEVICEOBJECTDATA), &od, &dwElements, 0);
		if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED)) {
			IDirectInputDevice_Acquire(g_pMouse);
			return;
		}

		/* Unable to read data or no data available */
		if ( FAILED(hr) ) {
			break;
		}

		if ( dwElements == 0 ) {
			break;
		}

		switch (od.dwOfs) {
		case DIMOFS_BUTTON0:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE1, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE1, qfalse, 0, NULL );
			break;

		case DIMOFS_BUTTON1:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE2, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE2, qfalse, 0, NULL );
			break;
			
		case DIMOFS_BUTTON2:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE3, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE3, qfalse, 0, NULL );
			break;

		case DIMOFS_BUTTON3:
			if (od.dwData & 0x80)
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE4, qtrue, 0, NULL );
			else
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MOUSE4, qfalse, 0, NULL );
			break;      
    // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
		case DIMOFS_Z:
			value = od.dwData;
			if (value == 0) {

			} else if (value < 0) {
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			} else {
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( od.dwTimeStamp, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			}
			break;
		}
	}

	// read the raw delta counter and ignore
	// the individual sample time / values
	hr = IDirectInputDevice_GetDeviceState(g_pMouse,
			sizeof(DIDEVICEOBJECTDATA), &state);
	if ( FAILED(hr) ) {
		*mx = *my = 0;
		return;
	}
	*mx = state.lX;
	*my = state.lY;
}
