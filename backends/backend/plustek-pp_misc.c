/* @file plustek-pp_misc.c
 * @brief here we have some helpful functions
*
 * based on sources acquired from Plustek Inc.
 * Copyright (C) 1998 Plustek Inc.
 * Copyright (C) 2000-2013 Gerhard Jaeger <gerhard@gjaeger.de>
 * also based on the work done by Rick Bronson
 *
 * History:
 * - 0.30 - initial version
 * - 0.31 - no changes
 * - 0.32 - moved the parport functions inside this module
 *        - now using the information, the parport-driver provides
 *        - for selecting the port-mode this driver uses
 * - 0.33 - added code to use faster portmodes
 * - 0.34 - added sample code for changing from ECP to PS/2 bidi mode
 * - 0.35 - added Kevins' changes (new function miscSetFastMode())
 *        - moved function initPageSettings() to module models.c
 * - 0.36 - added random generator
 *        - added additional debug messages
 *        - changed prototype of MiscInitPorts()
 *        - added miscPreemptionCallback()
 * - 0.37 - changed inb_p/outb_p to macro calls (kernel-mode)
 *        - added MiscGetModelName()
 *        - added miscShowPortModes()
 * - 0.38 - fixed a small bug in MiscGetModelName()
 * - 0.39 - added forceMode support
 * - 0.40 - no changes
 * - 0.41 - merged Kevins' patch to make EPP(ECP) work
 * - 0.42 - changed get_fast_time to _GET_TIME
 *        - changed include names
 * - 0.43 - added LINUX_26 stuff
 *        - minor fixes
 *        - removed floating point stuff
 * - 0.44 - fix format string issues, as Long types default to int32_t
 *          now
 * .
 * <hr>
 * This file is part of the SANE package.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As a special exception, the authors of SANE give permission for
 * additional uses of the libraries contained in this release of SANE.
 *
 * The exception is that, if you link a SANE library with other files
 * to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public
 * License.  Your use of that executable is in no way restricted on
 * account of linking the SANE library code into it.
 *
 * This exception does not, however, invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * If you submit changes to SANE to the maintainers to be included in
 * a subsequent release, you agree by submitting the changes that
 * those changes may be distributed with this exception intact.
 *
 * If you write modifications of your own for SANE, it is your choice
 * whether to permit this exception to apply to your modifications.
 * If you do not wish that, delete this exception notice.
 * <hr>
 */
#include "plustek-pp_scan.h"

/*************************** some definitions ********************************/

# define PPA_PROBE_SPP   0x0001
# define PPA_PROBE_PS2   0x0002
# define PPA_PROBE_ECR   0x0010
# define PPA_PROBE_EPP17 0x0100
# define PPA_PROBE_EPP19 0x0200

#define _PP_A 16807         /**< multiplier */
#define _PP_M 2147483647L   /**< 2**31 - 1  */

/*************************** some local vars *********************************/

static int  port_feature = 0;
static long randomnum    = 1;

static int portIsClaimed[_MAX_PTDEVS] = { 0, 0, 0, 0 };

/*************************** local functions *********************************/

/** miscNextLongRand() -- generate 2**31-2 random numbers
**
**  public domain by Ray Gardner
**
**  based on "Random Number Generators: Good Ones Are Hard to Find",
**  S.K. Park and K.W. Miller, Communications of the ACM 31:10 (Oct 1988),
**  and "Two Fast Implementations of the 'Minimal Standard' Random
**  Number Generator", David G. Carta, Comm. ACM 33, 1 (Jan 1990), p. 87-88
**
**  linear congruential generator f(z) = 16807 z mod (2 ** 31 - 1)
**
**  uses L. Schrage's method to avoid overflow problems
*/
static Long miscNextLongRand( Long seed )
{
	ULong lo, hi;

    lo = _PP_A * (Long)(seed & 0xFFFF);
    hi = _PP_A * (Long)((ULong)seed >> 16);
    lo += (hi & 0x7FFF) << 16;
    if (lo > _PP_M) {

		lo &= _PP_M;
        ++lo;
	}
    lo += hi >> 15;
    if (lo > _PP_M) {
		lo &= _PP_M;
        ++lo;
	}

	return (Long)lo;
}

/** initialize the random number generator
 */
static void miscSeedLongRand( long seed )
{
	randomnum = seed ? (seed & _PP_M) : 1;  /* nonzero seed */
}

/************************ exported functions *********************************/

/** allocate and initialize some memory for the scanner structure
 */
_LOC pScanData MiscAllocAndInitStruct( void )
{
	pScanData ps;

	ps = (pScanData)_KALLOC(sizeof(ScanData), GFP_KERNEL);

	if( NULL != ps ) {
		MiscReinitStruct( ps );
	}

	DBG( DBG_HIGH, "ScanData = 0x%08lx\n", (unsigned long)ps );
	return ps;
}

/** re-initialize the memory for the scanner structure
 */
_LOC int MiscReinitStruct( pScanData ps )
{
	if( NULL == ps )
		return _E_NULLPTR;

	memset( ps, 0, sizeof(ScanData));

	/* first init all constant stuff in ScanData
	 */
	ps->bCurrentSpeed = 1;
	ps->pbMapRed      =  ps->a_bMapTable;
	ps->pbMapGreen    = &ps->a_bMapTable[256];
	ps->pbMapBlue     = &ps->a_bMapTable[512];
	ps->sCaps.wIOBase = _NO_BASE;

	/* use memory address to seed the generator */
	miscSeedLongRand((long)ps);

	DBG( DBG_HIGH, "Init settings done\n" );
	return _OK;
}

/** in USER-Mode:   probe the specified port and try to get the port-mode
 *  in KERNEL-Mode: only use the modes, the driver returns
 */
_LOC int MiscInitPorts( pScanData ps, int port )
{
	int mode, mts;

	if( NULL == ps )
		return _E_NULLPTR;

	if( SANE_STATUS_GOOD != sanei_pp_getmodes( ps->pardev, &mode )) {
		DBG( DBG_HIGH, "Cannot get port mode!\n" );
		return _E_NO_PORT;
	}

	ps->IO.portMode = _PORT_NONE;
	mts             = -1;
	if( mode & SANEI_PP_MODE_SPP ) {
		DBG( DBG_LOW, "Setting SPP-mode\n" );
		ps->IO.portMode = _PORT_SPP;
		mts = SANEI_PP_MODE_SPP;
	}
	if( mode & SANEI_PP_MODE_BIDI ) {
		DBG( DBG_LOW, "Setting PS/2-mode\n" );
		ps->IO.portMode = _PORT_BIDI;
		mts = SANEI_PP_MODE_BIDI;
	}
	if( mode & SANEI_PP_MODE_EPP ) {
		DBG( DBG_LOW, "Setting EPP-mode\n" );
		ps->IO.portMode = _PORT_EPP;
		mts = SANEI_PP_MODE_EPP;
	}
	if( mode & SANEI_PP_MODE_ECP ) {
		DBG( DBG_HIGH, "ECP detected --> not supported\n" );
	}

	if( sanei_pp_uses_directio()) {
		DBG( DBG_LOW, "We're using direct I/O\n" );
	} else {
		DBG( DBG_LOW, "We're using libIEEE1284 I/O\n" );
	}

	if( ps->IO.portMode == _PORT_NONE ) {
		DBG( DBG_HIGH, "None of the portmodes is supported.\n" );
		return _E_NOSUPP;
	}

	sanei_pp_setmode( ps->pardev, mts );
	_VAR_NOT_USED( port );
	return _OK;
}

/** Function to restore the port
 */
_LOC void MiscRestorePort( pScanData ps )
{
    DBG(DBG_LOW,"MiscRestorePort()\n");

	/* don't restore if not necessary */
	if( 0xFFFF == ps->IO.lastPortMode ) {
	    DBG(DBG_LOW,"- no need to restore portmode !\n");
		return;
	}

    /*Restore Port-Mode*/
    if( port_feature & PPA_PROBE_ECR ){
		_OUTB_ECTL(ps,ps->IO.lastPortMode);
    }
}

/** Initializes a timer.
 * @param timer - pointer to the timer to start
 * @param us    - timeout value in micro-seconds
 */
_LOC void MiscStartTimer( TimerDef *timer , unsigned long us)
{
    struct timeval start_time;

	gettimeofday(&start_time, NULL);

    *timer = (TimerDef)start_time.tv_sec * 1000000 + (TimerDef)start_time.tv_usec + us;
}

/** Checks if a timer has been expired or not. In Kernel-mode, the scheduler
 *  will also be triggered, if the timer has not been expired.
 * @param timer - pointer to the timer to check
 * @return Function returns _E_TIMEOUT when the timer has been expired,
 *         otherwise _OK;
 */
_LOC int MiscCheckTimer( TimerDef *timer )
{
    struct timeval current_time;

	gettimeofday(&current_time, NULL);

    if ((TimerDef)current_time.tv_sec * 1000000 + (TimerDef)current_time.tv_usec > *timer) {
		return _E_TIMEOUT;
    } else {
/*#else
		sched_yield();
*/
		return _OK;
	}
}

/** Checks the function pointers
 * @param ps - pointer to the scanner data structure.
 * @return Function returns _TRUE if everything is okay and _FALSE if a NULL
 *         ptr has been detected.
 */
#ifdef DEBUG
_LOC Bool MiscAllPointersSet( pScanData ps )
{
	int  i;
	unsigned long *ptr;

	for( ptr = (unsigned long *)&ps->OpenScanPath, i = 1;
		 ptr <= (unsigned long *)&ps->ReadOneImageLine; ptr++, i++ ) {

		if( NULL == (pVoid)*ptr ) {
			DBG( DBG_HIGH, "Function pointer not set (pos = %d) !\n", i );
			return _FALSE;
		}
	}

	return _TRUE;
}
#endif

/** registers this driver to use port "portAddr" (KERNEL-Mode only)
 * @param ps       - pointer to the scanner data structure.
 * @param portAddr -
 */
_LOC int MiscRegisterPort( pScanData ps, int portAddr )
{
	DBG( DBG_LOW, "Assigning port handle %i\n", portAddr );
    ps->pardev = portAddr;

	portIsClaimed[ps->devno] = 0;
	return _OK;
}

/** unregisters the port from driver
 */
_LOC void MiscUnregisterPort( pScanData ps )
{
	sanei_pp_close( ps->pardev );
}

/** Try to claim the port
 * @param ps - pointer to the scanner data structure.
 * @return Function returns _OK on success, otherwise _E_BUSY.
 */
_LOC int MiscClaimPort( pScanData ps )
{
	if( 0 == portIsClaimed[ps->devno] ) {

		DBG( DBG_HIGH, "Try to claim the parport\n" );
		if( SANE_STATUS_GOOD != sanei_pp_claim( ps->pardev )) {
			return _E_BUSY;
		}
	}
	portIsClaimed[ps->devno]++;
	return _OK;
}

/** Release previously claimed port
 * @param ps - pointer to the scanner data structure
 */
_LOC void MiscReleasePort( pScanData ps )
{
	if( portIsClaimed[ps->devno] > 0 ) {
		portIsClaimed[ps->devno]--;

		if( 0 == portIsClaimed[ps->devno] ) {
			DBG( DBG_HIGH, "Releasing parport\n" );
			sanei_pp_release( ps->pardev );
		}
	}
}

/** Get random number
 * @return a random number.
 */
_LOC Long MiscLongRand( void )
{
	randomnum = miscNextLongRand( randomnum );

	return randomnum;
}

/** According to the id, the function returns a pointer to the model name
 * @param id - internal id of the various scanner models.
 * @return a pointer to the model-string.
 */
_LOC const char *MiscGetModelName( UShort id )
{
	DBG( DBG_HIGH, "MiscGetModelName - id = %i\n", id );

	if( MODEL_OP_PT12 < id )
		return ModelStr[0];

	return ModelStr[id];
}

/* END PLUSTEK-PP_MISC.C ....................................................*/
