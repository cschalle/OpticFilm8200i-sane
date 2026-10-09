/* @file plustek-pp_ptdrv.c
 * @brief this is the driver interface
 *
 * based on sources acquired from Plustek Inc.
 * Copyright (C) 1998 Plustek Inc.
 * Copyright (C) 2000-2013 Gerhard Jaeger <gerhard@gjaeger.de>
 * also based on the work done by Rick Bronson
 *
 * History:
 * - 0.30 - initial version
 * - 0.31 - Added some comments
 *        - added claiming/release of parallel port resources for this driver
 *        - added scaling function for high resolution modes where dpix < dpiy
 * - 0.32 - Revised lamp-off behaviour
 *        - removed function ptdrvIsLampOn
 *        - fixed misbehaviour when using cat /dev/pt_drv
 *        - moved parport-functions to module misc.c
 * - 0.33 - added parameter lOffonEnd
 *        - revised parport concurrency
 *        - removed calls to ps->PositionLamp
 * - 0.34 - no changes
 * - 0.35 - removed _PTDRV_PUT_SCANNER_MODEL from ioctl interface
 *        - added Kevins' changes (MiscRestorePort)
 *        - added parameter legal and function PtDrvLegalRequested()
 * - 0.36 - removed a bug in the shutdown function
 *        - removed all OP600P specific stuff because of the Primax tests
 *        - added version code to ioctl interface
 *        - added new parameter mov - model override
 *        - removed parameter legal
 *        - removed function PtDrvLegalRequested
 *        - changes, due to define renaming
 *        - patch for OpticPro 4800P
 *        - added multiple device support
 *        - added proc fs support/also for Kernel2.4
 * - 0.37 - cleanup work, moved the procfs stuff to file procfs.c
 *        - and some definitions to plustek_scan.h
 *        - moved MODELSTR to misc.c
 *        - output of the error-code after initialization
 * - 0.38 - added P12 stuff
 *        - removed function ptdrvIdleMode
 *        - moved function ptdrvP96Calibration() to p48xxCalibration
 *        - moved function ptdrvP98Calibration() to p9636Calibration
 *        - added devfs support (patch by Gordon Heydon <gjheydon@bigfoot.com>)
 * - 0.39 - added schedule stuff after reading one line to have a better
 *          system response in SPP modes
 *        - added forceMode switch
 * - 0.40 - added MODULE_LICENSE stuff
 * - 0.41 - added _PTDRV_ADJUST functionality
 *        - changed ioctl call to PutImage
 * - 0.42 - added _PTDRV_SETMAP functionality
 *        - improved the cancel functionality
 * - 0.43 - added LINUX_26 stuff
 *        - changed include names
 *        - changed version string stuff
 * - 0.44 - added support for more recent kernels
 *        - fix format string issues, as Long types default to int32_t
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

/****************************** static vars **********************************/

/* default port is at 0x378 */
static int port[_MAX_PTDEVS] = { 0x378, 0, 0, 0 };

static pScanData PtDrvDevices[_MAX_PTDEVS]= { NULL,   NULL,   NULL,   NULL   };
static int       lampoff[_MAX_PTDEVS]     = { 180,    180,    180,    180    };
static int       warmup[_MAX_PTDEVS]      = { 30,     30,     30,     30     };
static int       lOffonEnd[_MAX_PTDEVS]   = { 0,      0,      0,      0      };
static UShort    mov[_MAX_PTDEVS]         = { 0,      0,      0,      0      };
static UShort    forceMode[_MAX_PTDEVS]   = { 0,      0,      0,      0      };

/* timers for warmup checks */
static TimerDef toTimer[_MAX_PTDEVS];

static Bool	PtDrvInitialized = _FALSE;
#ifdef HAVE_SETITIMER
static struct itimerval saveSettings;
#endif


/****************************** some prototypes ******************************/

static void ptdrvStartLampTimer( pScanData ps );

/****************************** local functions ******************************/

/** copy user-space data into kernel memory
 */
static int getUserPtr(const pVoid useraddr, pVoid where, UInt size )
{
	int err = _OK;

	/* do parameter checks */
	if((NULL == useraddr) || ( 0 == size))
		return _E_INVALID;

	switch (size) {
	default:
		memcpy( where, useraddr, size );
	}
	return err;
}

/** copy kernel data into user mode address space
 */
static int putUserPtr( const pVoid ptr, pVoid useraddr, UInt size )
{
	int err = _OK;

	if (NULL == useraddr)
    	return _E_INVALID;

	memcpy( useraddr, ptr, size );

	return err;
}

static unsigned long copy_from_user( pVoid dest, pVoid src, unsigned long len )
{
	memcpy( dest, src, len );
	return 0;
}

static unsigned long copy_to_user( pVoid dest, pVoid src, unsigned long len )
{
	memcpy( dest, src, len );
	return 0;
}

/**
 */
static int putUserVal(const ULong value, pVoid useraddr, UInt size)
{
	if (NULL == useraddr)
    	return _E_INVALID;

	switch (size) {

	case sizeof(UChar):
		*(pUChar)useraddr = (UChar)value;
		break;
	case sizeof(UShort):
		*(pUShort)useraddr = (UShort)value;
		break;
	case sizeof(ULong):
		*(pULong)useraddr = (ULong)value;
		break;

  	default:
    	return _E_INVALID;
	}
	return 0;
}

/** switch lamp 0 on
 */
static void ptDrvSwitchLampOn( pScanData ps )
{
	DBG( DBG_LOW, "Switching lamp 0 on.\n" );

	if( _IS_ASIC98(ps->sCaps.AsicID)) {

		ps->AsicReg.RD_ScanControl |= _SCAN_NORMALLAMP_ON;

		ps->bLastLampStatus = _SCAN_NORMALLAMP_ON;

	} else {

		ps->AsicReg.RD_ScanControl |= ps->bLampOn;
		ps->bLastLampStatus = ps->bLampOn;
	}

	IOCmdRegisterToScanner(ps, ps->RegScanControl, ps->AsicReg.RD_ScanControl);
}

/** check the lamp warmup
 */
static void ptdrvLampWarmup( pScanData ps )
{
	Bool	 warmupNeeded;
	TimerDef timer;

	if( 0 == ps->warmup )
		return;

	warmupNeeded = _FALSE;

	/*
	 * do we have to warmup again ? Timer has not elapsed...
	 */
	if( _OK == MiscCheckTimer( &toTimer[ps->devno] )) {

		DBG( DBG_LOW, "Startup warmup needed!\n" );
		warmupNeeded = _TRUE;
	} else {

		warmupNeeded = ps->fWarmupNeeded;
	}

	if( warmupNeeded ) {

		/*
		 * correct lamp should have been switched on but
		 * before doing anything else wait until warmup has been done
		 */
		DBG( DBG_LOW, "Waiting on warmup - %u s\n", ps->warmup );

		MiscStartTimer( &timer, _SECOND * ps->warmup );
		while( !MiscCheckTimer( &timer )) {

			/* on break, we setup the initial timer again... */
			if( _FALSE == ps->fScanningStatus ) {
				MiscStartTimer( &toTimer[ps->devno], (_SECOND * ps->warmup));
				return;
			}
		};

	}
#ifdef DEBUG
	else {
		DBG( DBG_LOW, "No warm-up needed \n" );
	}
#endif

	/*
	 * start a timer here again with only a second timeout
	 * because we need this one only for startup (Force timeout!!)
	 */
	MiscStartTimer( &toTimer[ps->devno], _SECOND );
}

/**
 */
static void ptdrvLampTimerIrq( int sig_num )
{
	pScanData ps;

	DBG( DBG_HIGH, "!! IRQ !! Lamp-Timer stopped.\n" );

    _VAR_NOT_USED( sig_num );
	ps = PtDrvDevices[0];

	/*
	 * paranoia check!
	 */
	if( NULL == ps )
		return;

	if( _NO_BASE == ps->sCaps.wIOBase )
		return;

	if( _IS_ASIC98(ps->sCaps.AsicID)) {
	    ps->AsicReg.RD_ScanControl &= ~_SCAN_LAMPS_ON;
	} else {
		ps->AsicReg.RD_ScanControl &= ~_SCAN_LAMP_ON;
	}

	/* force warmup... */
	ps->bLastLampStatus = 0xFF;

	/*
	 * claim parallel port if necessary...
	 * if the port is busy, restart the timer
	 */
	if( _OK != MiscClaimPort(ps)) {
		ptdrvStartLampTimer( ps );
		return;
	}

	IOCmdRegisterToScanner( ps, ps->RegScanControl,
							    ps->AsicReg.RD_ScanControl );
	MiscReleasePort(ps);
}

/**
 */
static void ptdrvStartLampTimer( pScanData ps )
{
	sigset_t 		 block, pause_mask;
	struct sigaction s;
#ifdef HAVE_SETITIMER
	struct itimerval interval;
#endif

	/* block SIGALRM */
	sigemptyset( &block );
	sigaddset  ( &block, SIGALRM );
	sigprocmask( SIG_BLOCK, &block, &pause_mask );

	/* setup handler */
	sigemptyset( &s.sa_mask );
	sigaddset  (  &s.sa_mask, SIGINT );
	s.sa_flags   = 0;
	s.sa_handler = ptdrvLampTimerIrq;

	if(	sigaction( SIGALRM, &s, NULL ) < 0 ) {
		DBG(DBG_HIGH,"pt_drv%u: Can't setup timer-irq handler\n",ps->devno);
	}

	sigprocmask( SIG_UNBLOCK, &block, &pause_mask );

#ifdef HAVE_SETITIMER
	/*
	 * define a one-shot timer
	 */
	interval.it_value.tv_usec = 0;
	interval.it_value.tv_sec  = ps->lampoff;
	interval.it_interval.tv_usec = 0;
	interval.it_interval.tv_sec  = 0;

	if( 0 != ps->lampoff )
		setitimer( ITIMER_REAL, &interval, &saveSettings );
#else
	alarm( ps->lampoff );
#endif

	DBG( DBG_HIGH, "Lamp-Timer started!\n" );
}

/**
 */
static void ptdrvStopLampTimer( pScanData ps )
{
	sigset_t block, pause_mask;

	/* block SIGALRM */
	sigemptyset( &block );
	sigaddset  ( &block, SIGALRM );
	sigprocmask( SIG_BLOCK, &block, &pause_mask );
#ifdef HAVE_SETITIMER
	if( 0 != ps->lampoff )
		setitimer( ITIMER_REAL, &saveSettings, NULL );
#else
	_VAR_NOT_USED( ps );
	alarm(0);
#endif

	DBG( DBG_HIGH, "Lamp-Timer stopped!\n" );
}

/** claim and initialize the requested port
 */
static int ptdrvOpen( pScanData ps, int portBase )
{
	int retval;

	DBG( DBG_HIGH, "ptdrvOpen(port=0x%x)\n", (int32_t)portBase );
	if( NULL == ps )
		return _E_NULLPTR;

	/*
	 * claim port resources...
	 */
	retval = MiscClaimPort(ps);

	if( _OK != retval )
		return retval;

	return MiscInitPorts( ps, portBase );
}

/** free used memory (if necessary)
 * restore the parallel port settings and release the port
 */
static int ptdrvClose( pScanData ps )
{
	DBG( DBG_HIGH, "ptdrvClose()\n" );
	if( NULL == ps )
		return _E_NULLPTR;

	/*
	 * should be cleared by ioctl(close)
	 */
    if ( NULL != ps->driverbuf ) {
		DBG( DBG_LOW, "*** cleanup buffers ***\n" );
        _VFREE( ps->driverbuf );
        ps->driverbuf = NULL;
    }

    if ( NULL != ps->Shade.pHilight ) {
        _VFREE( ps->Shade.pHilight );
        ps->Shade.pHilight = NULL;
    }

	/*
	 * restore/release port resources...
	 */
	MiscRestorePort( ps );
	MiscReleasePort( ps );

	return _OK;
}

/** will be called during OPEN_DEVICE ioctl call
 */
static int ptdrvOpenDevice( pScanData ps )
{
	int    retval, iobase;
	UShort asic;
	UChar  lastStat;
	UShort lastMode;
	ULong  devno;

    int pd;

	/*
	 * push some values from the struct
     */
	pd       = ps->pardev;
	iobase   = ps->sCaps.wIOBase;
	asic     = ps->sCaps.AsicID;
	lastStat = ps->bLastLampStatus;
	lastMode = ps->IO.lastPortMode;
	devno    = ps->devno;

	/*
	 * reinit the show
	 */
	ptdrvStopLampTimer( ps );
	MiscReinitStruct  ( ps );

	/*
	 * pop the val(s)
	 */
	ps->pardev          = pd;
	ps->bLastLampStatus = lastStat;
	ps->IO.lastPortMode = lastMode;
	ps->devno           = devno;
	ps->ModelOverride = mov[devno];
	ps->warmup        = warmup[devno];
	ps->lampoff		  = lampoff[devno];
	ps->lOffonEnd	  = lOffonEnd[devno];
	ps->IO.forceMode  = forceMode[devno];

	/*
	 * try to find scanner again
	 */
	retval = ptdrvOpen( ps, iobase );

	if( _OK == retval )
		retval = DetectScanner( ps, asic );
	else
		ptdrvStartLampTimer( ps );

	return retval;
}

/*.............................................................................
 * initialize the driver
 * allocate memory for the ScanData structure and do some presets
 */
static int ptdrvInit( int devno )
{
	int       retval;
	pScanData ps;

	DBG( DBG_HIGH, "ptdrvInit(%u)\n", devno );

	if( devno >= _MAX_PTDEVS )
		return _E_NO_DEV;

	/*
	 * allocate memory for our large ScanData-structure
	 */
	ps = MiscAllocAndInitStruct();
	if( NULL == ps ) {
		return _E_ALLOC;
	}

	ps->ModelOverride = mov[devno];
	ps->warmup        = warmup[devno];
	ps->lampoff       = lampoff[devno];
	ps->lOffonEnd     = lOffonEnd[devno];
	ps->IO.forceMode  = forceMode[devno];
	ps->devno         = devno;

	/* assign it right here, to allow correct shutdown */
	PtDrvDevices[devno] = ps;

	/*
	 * try to register the port
	 */
	retval = MiscRegisterPort( ps, port[devno] );

	if( _OK == retval ) {
		retval = ptdrvOpen( ps, port[devno] );
	}

	/*
	 * try to detect a scanner...
	 */
	if( _OK == retval ) {
		retval = DetectScanner( ps, 0 );

		/* do this here before releasing the port */
		if( _OK == retval ) {
			ptDrvSwitchLampOn( ps );
		}
		ptdrvClose( ps );
	}

	if( _OK == retval ) {

		DBG( DBG_LOW, "pt_drv%u: %s found\n",
									 devno, MiscGetModelName(ps->sCaps.Model));

		/*
		 * initialize the timespan timer
	     */
		MiscStartTimer( &toTimer[ps->devno], (_SECOND * ps->warmup));

		if( 0 == ps->lampoff )
		DBG( DBG_LOW,
					"pt_drv%u: Lamp-Timer switched off.\n", devno );
		else {
		DBG( DBG_LOW,
					"pt_drv%u: Lamp-Timer set to %u seconds.\n",
														devno, ps->lampoff );
		}

		DBG( DBG_LOW,
				"pt_drv%u: WarmUp period set to %u seconds.\n",
														devno, ps->warmup );

		if( 0 == ps->lOffonEnd ) {
		DBG( DBG_LOW,
				"pt_drv%u: Lamp untouched on driver unload.\n", devno );
		} else {
		DBG( DBG_LOW,
				"pt_drv%u: Lamp switch-off on driver unload.\n", devno );
		}

		ptdrvStartLampTimer( ps );
	}

	return retval;
}

/*.............................................................................
 * shutdown the driver:
 * switch the lights out
 * stop the motor
 * free memory
 */
static int ptdrvShutdown( pScanData ps )
{
	int devno;

	DBG( DBG_HIGH, "ptdrvShutdown()\n" );

	if( NULL == ps )
		return _E_NULLPTR;

	devno = ps->devno;

	DBG( DBG_HIGH, "cleanup device %u\n", devno );

	if( _NO_BASE != ps->sCaps.wIOBase ) {

		ptdrvStopLampTimer( ps );

		if( _OK == MiscClaimPort(ps)) {

			ps->PutToIdleMode( ps );

			if( 0 != ps->lOffonEnd ) {
				if( _IS_ASIC98(ps->sCaps.AsicID)) {
		            ps->AsicReg.RD_ScanControl &= ~_SCAN_LAMPS_ON;
				} else {
					ps->AsicReg.RD_ScanControl &= ~_SCAN_LAMP_ON;
	        	}
				IOCmdRegisterToScanner( ps, ps->RegScanControl,
											  ps->AsicReg.RD_ScanControl );
			}
		}
		MiscReleasePort( ps );
	}

	/* unregister the driver
	 */
	MiscUnregisterPort( ps );

	_KFREE( ps );
	if( devno < _MAX_PTDEVS )
		PtDrvDevices[devno] = NULL;

	return _OK;
}

/*.............................................................................
 * the IOCTL interface
 */
static int ptdrvIoctl( pScanData ps, UInt cmd, pVoid arg )
{
	UShort dir;
	UShort version;
	UInt   size;
	ULong  argVal;
	int    cancel;
	int    retval;

	/*
 	 * do the preliminary stuff here
	 */
	if( NULL == ps )
		return _E_NULLPTR;

	retval = _OK;

	dir  = _IOC_DIR(cmd);
	size = _IOC_SIZE(cmd);

	if ((_IOC_WRITE == dir) && size && (size <= sizeof(ULong))) {

    	if (( retval = getUserPtr( arg, &argVal, size))) {
			DBG( DBG_HIGH, "ioctl() failed - result = %i\n", retval );
      		return retval;
		}
	}

	switch( cmd ) {

	/* open */
    case _PTDRV_OPEN_DEVICE:
		DBG( DBG_LOW, "ioctl(_PTDRV_OPEN_DEVICE)\n" );
   	  	if (copy_from_user(&version, arg, sizeof(UShort)))
			return _E_FAULT;

		if( _PTDRV_IOCTL_VERSION != version ) {
			DBG( DBG_HIGH, "Version mismatch: Backend=0x%04X(0x%04X)",
							version, _PTDRV_IOCTL_VERSION );
			return _E_VERSION;
		}

		retval = ptdrvOpenDevice( ps );
      	break;

	/* close */
	case _PTDRV_CLOSE_DEVICE:
		DBG( DBG_LOW,  "ioctl(_PTDRV_CLOSE_DEVICE)\n" );

	    if ( NULL != ps->driverbuf ) {
			DBG( DBG_LOW, "*** cleanup buffers ***\n" );
	        _VFREE( ps->driverbuf );
    	    ps->driverbuf = NULL;
	    }

    	if ( NULL != ps->Shade.pHilight ) {
        	_VFREE( ps->Shade.pHilight );
	        ps->Shade.pHilight = NULL;
    	}

		ps->PutToIdleMode( ps );
		ptdrvStartLampTimer( ps );
      	break;

	/* get caps - no scanner connection necessary */
    case _PTDRV_GET_CAPABILITIES:
		DBG( DBG_LOW, "ioctl(_PTDRV_GET_CAPABILITES)\n" );

    	return putUserPtr( &ps->sCaps, arg, size);
      	break;

	/* get lens-info - no scanner connection necessary */
    case _PTDRV_GET_LENSINFO:
		DBG( DBG_LOW, "ioctl(_PTDRV_GET_LENSINFO)\n" );

      	return putUserPtr( &ps->LensInf, arg, size);
      	break;

	/* put the image info - no scanner connection necessary */
    case _PTDRV_PUT_IMAGEINFO:
      	{
            short  tmpcx, tmpcy;
      		ImgDef img;

			DBG( DBG_LOW, "ioctl(_PTDRV_PUT_IMAGEINFO)\n" );
			if (copy_from_user( &img, (pImgDef)arg, size))
				return _E_FAULT;

            tmpcx = (short)img.crArea.cx;
            tmpcy = (short)img.crArea.cy;

			if(( 0 >= tmpcx ) || ( 0 >= tmpcy )) {
				DBG( DBG_LOW, "CX or CY <= 0!!\n" );
				return _E_INVALID;
			}

			_ASSERT( ps->GetImageInfo );
	      	ps->GetImageInfo( ps, &img );
		}
      	break;

	/* get crop area - no scanner connection necessary */
    case _PTDRV_GET_CROPINFO:
    	{
      		CropInfo	outBuffer;
      		pCropInfo	pcInf = &outBuffer;

			DBG( DBG_LOW, "ioctl(_PTDRV_GET_CROPINFO)\n" );

			memset( pcInf, 0, sizeof(CropInfo));

	      	pcInf->dwPixelsPerLine = ps->DataInf.dwAppPixelsPerLine;
    	  	pcInf->dwBytesPerLine  = ps->DataInf.dwAppBytesPerLine;
      		pcInf->dwLinesPerArea  = ps->DataInf.dwAppLinesPerArea;
      		return putUserPtr( pcInf, arg, size );
      	}
      	break;

	/* adjust the driver settings */
	case _PTDRV_ADJUST:
		{
			PPAdjDef adj;

			DBG( DBG_LOW, "ioctl(_PTDRV_ADJUST)\n" );

			if (copy_from_user(&adj, (pPPAdjDef)arg, sizeof(PPAdjDef)))
				return _E_FAULT;

			DBG( DBG_LOW, "Adjusting device %u\n", ps->devno );
			DBG( DBG_LOW, "warmup:       %i\n", adj.warmup );
			DBG( DBG_LOW, "lampOff:      %i\n", adj.lampOff );
			DBG( DBG_LOW, "lampOffOnEnd: %i\n", adj.lampOffOnEnd );

			if( ps->devno < _MAX_PTDEVS ) {

				if( adj.warmup >= 0 ) {
					warmup[ps->devno] = adj.warmup;
					ps->warmup        = adj.warmup;
				}

				if( adj.lampOff >= 0 ) {
					lampoff[ps->devno] = adj.lampOff;
					ps->lampoff        = adj.lampOff;
				}

				if( adj.lampOffOnEnd >= 0 ) {
					lOffonEnd[ps->devno] = adj.lampOffOnEnd;
					ps->lOffonEnd        = adj.lampOffOnEnd;
				}
			}
		}
		break;

	/* set a specific map (r,g,b or gray) */
	case _PTDRV_SETMAP:
		{
			int     i, x_len;
			MapDef  map;

			DBG( DBG_LOW, "ioctl(_PTDRV_SETMAP)\n" );

			if (copy_from_user( &map, (pMapDef)arg, sizeof(MapDef)))
				return _E_FAULT;

			DBG( DBG_LOW, "maplen=%u, mapid=%u, addr=0x%08lx\n",
							map.len, map.map_id, (u_long)map.map );

			x_len = 256;
			if( _IS_ASIC98(ps->sCaps.AsicID))
				x_len = 4096;

			/* check for 0 pointer and len */
			if((NULL == map.map) || (x_len != map.len)) {
				DBG( DBG_LOW, "map pointer == 0, or map len invalid!!\n" );
				return _E_INVALID;
			}

    		if( _MAP_MASTER == map.map_id ) {

				for( i = 0; i < 3; i++ ) {
					if (copy_from_user((pVoid)&ps->a_bMapTable[x_len * i],
					                    map.map, x_len )) {
						return _E_FAULT;
					}
				}
			} else {

				u_long idx = 0;
				if( map.map_id == _MAP_GREEN )
					idx = 1;
				if( map.map_id == _MAP_BLUE )
					idx = 2;

				if (copy_from_user((pVoid)&ps->a_bMapTable[x_len * idx],
				                   map.map, x_len )) {
						return _E_FAULT;
				}
			}

			/* here we adjust the maps according to
			 * the brightness and contrast settings
			 */
			MapAdjust( ps, map.map_id );
		}
		break;

	/* set environment - no scanner connection necessary */
    case _PTDRV_SET_ENV:
      	{
			ScanInfo sInf;

			DBG( DBG_LOW, "ioctl(_PTDRV_SET_ENV)\n" );

			if (copy_from_user(&sInf, (pScanInfo)arg, sizeof(ScanInfo)))
				return _E_FAULT;

			/*
			 * to make the OpticPro 4800P work, we need to invert the
			 * Inverse flag
			 */
			if( _ASIC_IS_96001 == ps->sCaps.AsicID ) {
				if( SCANDEF_Inverse & sInf.ImgDef.dwFlag )
					sInf.ImgDef.dwFlag &= ~SCANDEF_Inverse;
				else
					sInf.ImgDef.dwFlag |= SCANDEF_Inverse;
			}

			_ASSERT( ps->SetupScanSettings );
      		retval = ps->SetupScanSettings( ps, &sInf );

			/* CHANGE preset map here */
			if( _OK == retval ) {
				MapInitialize ( ps );
				MapSetupDither( ps );

				ps->DataInf.dwVxdFlag |= _VF_ENVIRONMENT_READY;

				if (copy_to_user((pScanInfo)arg, &sInf, sizeof(ScanInfo)))
					return _E_FAULT;
			}
		}
		break;

	/* start scan */
	case _PTDRV_START_SCAN:
		{
			StartScan  outBuffer;
			pStartScan pstart = (pStartScan)&outBuffer;

			DBG( DBG_LOW, "ioctl(_PTDRV_START_SCAN)\n" );

			retval = IOIsReadyForScan( ps );
			if( _OK == retval ) {

				ps->dwDitherIndex      = 0;
				ps->fScanningStatus    = _TRUE;
				pstart->dwBytesPerLine = ps->DataInf.dwAppBytesPerLine;
				pstart->dwLinesPerScan = ps->DataInf.dwAppLinesPerArea;
				pstart->dwFlag 		   = ps->DataInf.dwScanFlag;

				ps->DataInf.dwVxdFlag |= _VF_FIRSTSCANLINE;
				ps->DataInf.dwScanFlag&=~(_SCANNER_SCANNING|_SCANNER_PAPEROUT);

				if (copy_to_user((pStartScan)arg, pstart, sizeof(StartScan)))
					return _E_FAULT;
			}
		}
		break;

	/* stop scan */
    case _PTDRV_STOP_SCAN:

		DBG( DBG_LOW, "ioctl(_PTDRV_STOP_SCAN)\n" );

		if (copy_from_user(&cancel, arg, sizeof(short)))
			return _E_FAULT;

		/* we may use this to abort scanning! */
		ps->fScanningStatus = _FALSE;

		/* when using this to cancel, then that's all */
		if( _FALSE == cancel ) {

			MotorToHomePosition( ps );

    		ps->DataInf.dwAppLinesPerArea = 0;
      		ps->DataInf.dwScanFlag &= ~_SCANNER_SCANNING;

			/* if environment was never set */
    	  	if (!(ps->DataInf.dwVxdFlag & _VF_ENVIRONMENT_READY))
        		retval = _E_SEQUENCE;

	      	ps->DataInf.dwVxdFlag &= ~_VF_ENVIRONMENT_READY;

		} else {
			DBG( DBG_LOW, "CANCEL Mode set\n" );
		}
		retval = putUserVal(retval, arg, size);
      	break;

	/* read the flag status register, when reading the action button, you must
	 * only do this call and none of the other ioctl's
     * like open, etc or it will always show up as "1"
	 */
	case _PTDRV_ACTION_BUTTON:
		DBG( DBG_LOW, "ioctl(_PTDRV_ACTION_BUTTON)\n" );
		IODataRegisterFromScanner( ps, ps->RegStatus );
      	retval = putUserVal( argVal, arg, size );
		break;

	default:
		retval = _E_NOSUPP;
      	break;
	}

	return retval;
}

/*.............................................................................
 * read the data
 */
static int ptdrvRead( pScanData ps, pUChar buffer, int count )
{
	pUChar	scaleBuf;
	ULong	dwLinesRead = 0;
	int 	retval      = _OK;

#ifdef _ASIC_98001_SIM
		DBG( DBG_LOW,
					"pt_drv : Software-Emulation active, can't read!\n" );
	return _E_INVALID;
#endif

	if((NULL == buffer) || (NULL == ps)) {
		DBG( DBG_HIGH,
						"pt_drv :  Internal NULL-pointer!\n" );
		return _E_NULLPTR;
	}

	if( 0 == count ) {
		DBG( DBG_HIGH,
			"pt_drv%u: reading 0 bytes makes no sense!\n", ps->devno );
		return _E_INVALID;
	}

	if( _FALSE == ps->fScanningStatus )
		return _E_ABORT;

	/*
	 * has the environment been set ?
	 * this should prevent the driver from causing a seg-fault
	 * when using the cat /dev/pt_drv command!
	 */
   	if (!(ps->DataInf.dwVxdFlag & _VF_ENVIRONMENT_READY)) {
		DBG( DBG_HIGH,
			"pt_drv%u:  Cannot read, driver not initialized!\n",ps->devno);
		return _E_SEQUENCE;
	}

	/*
	 * get some memory
	 */
	ps->Scan.bp.pMonoBuf = _KALLOC( ps->DataInf.dwAppPhyBytesPerLine, GFP_KERNEL);

	if ( NULL == ps->Scan.bp.pMonoBuf ) {
		DBG( DBG_HIGH,
			"pt_drv%u:  Not enough memory available!\n", ps->devno );
    	return _E_ALLOC;
	}

	/* if we have to do some scaling, we need another buffer... */
	if( ps->DataInf.XYRatio > 1000 ) {

		scaleBuf = _KALLOC( ps->DataInf.dwAppPhyBytesPerLine, GFP_KERNEL);
		if ( NULL == scaleBuf ) {
			_KFREE( ps->Scan.bp.pMonoBuf );
		DBG( DBG_HIGH,
			"pt_drv%u:  Not enough memory available!\n", ps->devno );
    		return _E_ALLOC;
		}
	} else {
		scaleBuf = NULL;
	}

	DBG( DBG_LOW, "PtDrvRead(%u bytes)*****************\n", count );
	DBG( DBG_LOW, "MonoBuf = 0x%08lx[%u], scaleBuf = 0x%lx\n",
			(unsigned long)ps->Scan.bp.pMonoBuf,
            ps->DataInf.dwAppPhyBytesPerLine, (unsigned long)scaleBuf );

	/*
	 * in case of a previous problem, move the sensor back home
	 */
  	MotorToHomePosition( ps );

	if( _FALSE == ps->fScanningStatus ) {
		retval = _E_ABORT;
		goto ReadFinished;
	}

	dwLinesRead = 0;

	/*
	 * first of all calibrate the show
	 */
	ps->bMoveDataOutFlag   = _DataInNormalState;
   	ps->fHalfStepTableFlag = _FALSE;
    ps->fReshaded          = _FALSE;
   	ps->fScanningStatus    = _TRUE;

    if( _ASIC_IS_98003 == ps->sCaps.AsicID )
        ps->Scan.fRefreshState = _FALSE;
    else
        ps->Scan.fRefreshState = _TRUE;

    ptdrvLampWarmup( ps );

	if( _FALSE == ps->fScanningStatus ) {
		retval = _E_ABORT;
		goto ReadFinished;
	}

    retval = ps->Calibration( ps );
	if( _OK != retval ) {
		DBG( DBG_HIGH,
			"pt_drv%u: calibration failed, result = %i\n",
														ps->devno, retval );
		goto ReadFinished;
	}

    if( _ASIC_IS_98003 == ps->sCaps.AsicID ) {

        ps->OpenScanPath( ps );

    	MotorP98003ForceToLeaveHomePos( ps );
    }

	_ASSERT(ps->SetupScanningCondition);
  	ps->SetupScanningCondition(ps);

    if( _ASIC_IS_98003 != ps->sCaps.AsicID ) {
    	ps->SetMotorSpeed( ps, ps->bCurrentSpeed, _TRUE );
        IOSetToMotorRegister( ps );
    } else {

        ps->WaitForPositionY( ps );
    	_DODELAY( 70 );
    	ps->Scan.bOldScanState = IOGetScanState( ps, _TRUE ) & _SCANSTATE_MASK;
    }

    ps->DataInf.dwScanFlag |= _SCANNER_SCANNING;

	if( _FALSE == ps->fScanningStatus ) {
		DBG( DBG_HIGH, "read aborted!\n" );
		retval = _E_ABORT;
		goto ReadFinished;
	}

	/*
	 * now get the picture data
	 */
	DBG( DBG_HIGH, "dwAppLinesPerArea = %d\n", ps->DataInf.dwAppLinesPerArea);
	DBG( DBG_HIGH, "dwAppBytesPerLine = %d\n", ps->DataInf.dwAppBytesPerLine);

/* HEINER: A3I
	ps->bMoveDataOutFlag = _DataFromStopState;
*/
  	if ( 0 != ps->DataInf.dwAppLinesPerArea ) {

	   	ps->Scan.dwLinesToRead = count / ps->DataInf.dwAppBytesPerLine;

    	if( ps->Scan.dwLinesToRead ) {

        	DBG( DBG_HIGH, "dwLinesToRead = %d\n", ps->Scan.dwLinesToRead );

      		if( ps->Scan.dwLinesToRead > ps->DataInf.dwAppLinesPerArea )
        		ps->Scan.dwLinesToRead = ps->DataInf.dwAppLinesPerArea;

      		ps->DataInf.dwAppLinesPerArea -= ps->Scan.dwLinesToRead;

      		if (ps->DataInf.dwScanFlag & SCANDEF_BmpStyle)
        		buffer += ((ps->Scan.dwLinesToRead - 1) *
                   				ps->DataInf.dwAppBytesPerLine);

      		if (ps->DataInf.dwVxdFlag & _VF_DATATOUSERBUFFER)
        		ps->DataInf.pCurrentBuffer = ps->Scan.bp.pMonoBuf;

      		while(ps->fScanningStatus && ps->Scan.dwLinesToRead) {

        		_ASSERT(ps->ReadOneImageLine);
		   		if (!ps->ReadOneImageLine(ps)) {
        			ps->fScanningStatus = _FALSE;
            		DBG( DBG_HIGH, "ReadOneImageLine() failed at line %u!\n",
                                    dwLinesRead );
					break;
				}

				/*
				 * as we might scan images that exceed the CCD-capabilities
				 * in x-resolution, we have to enlarge the line data
				 * i.e.: scanning at 1200dpi generates on a P9636 600 dpi in
				 *       x-direction but 1200dpi in y-direction...
				 */
				if( NULL != scaleBuf ) {
					ScaleX( ps, ps->Scan.bp.pMonoBuf, scaleBuf );
	    	    	if (copy_to_user( buffer, scaleBuf,
									ps->DataInf.dwAppPhyBytesPerLine)) {
						return _E_FAULT;
					}
				} else {
	    	    	if (copy_to_user( buffer, ps->Scan.bp.pMonoBuf,
								  ps->DataInf.dwAppPhyBytesPerLine)) {
						return _E_FAULT;
					}
				}

				buffer += ps->Scan.lBufferAdjust;
				dwLinesRead++;
				ps->Scan.dwLinesToRead--;

				/* needed, esp. to avoid freezing the system in SPP mode */
/*#else
				sched_yield();
*/
        	}

			if (ps->fScanningStatus) {

            	if( _IS_ASIC96(ps->sCaps.AsicID))
          			MotorP96SetSpeedToStopProc(ps);

			} else {
        		if (ps->DataInf.dwScanFlag & (SCANDEF_StopWhenPaperOut |
                    	                         SCANDEF_UnlimitLength)) {
          			ps->DataInf.dwAppLinesPerArea = 0;
				} else {
        			if (ps->DataInf.dwScanFlag & SCANDEF_BmpStyle)
            			buffer -= (ps->DataInf.dwAppBytesPerLine *
                	    		   (ps->Scan.dwLinesToRead - 1));
          			memset( buffer, 0xff,
          		   			ps->Scan.dwLinesToRead * ps->DataInf.dwAppBytesPerLine );
        	  		dwLinesRead += ps->Scan.dwLinesToRead;
    	    	}
	        }

      	} else {
      		retval = _E_INTERNAL;
		}
	}

	if( _FALSE == ps->fScanningStatus ) {
		DBG( DBG_HIGH, "read aborted!\n" );
		retval = _E_ABORT;
	}

ReadFinished:


	if( _ASIC_IS_98003 == ps->sCaps.AsicID )
		ps->CloseScanPath( ps );

	if( NULL != ps->Scan.bp.pMonoBuf )
		_KFREE( ps->Scan.bp.pMonoBuf );

	if( NULL != scaleBuf )
		_KFREE( scaleBuf );

	/*
	 * on success return number of bytes red
	 */
	if ( _OK == retval )
    	return (ps->DataInf.dwAppPhyBytesPerLine * dwLinesRead);

   	return retval;
}

/*.............................................................................
 * here we only have wrapper functions
 */
static int PtDrvInit( const char *dev_name, UShort model_override )
{
	int fd;
	int result = _OK;

	if( _TRUE == PtDrvInitialized )
		return _OK;

	result = sanei_pp_open( dev_name, &fd );
	if( SANE_STATUS_GOOD != result )
		return result;

	port[0] = fd;
	mov[0]  = model_override;

	result = ptdrvInit( 0 );

	if( _OK == result ) {
		PtDrvInitialized = _TRUE;
	} else {
		ptdrvShutdown( PtDrvDevices[0] );
	}

	return result;
}

static int PtDrvShutdown( void )
{
	int result;

	if( _FALSE == PtDrvInitialized )
		return _E_NOT_INIT;

	result = ptdrvShutdown( PtDrvDevices[0] );

	PtDrvInitialized = _FALSE;

	return result;
}

static int PtDrvOpen( void )
{
	if( _FALSE == PtDrvInitialized )
		return _E_NOT_INIT;

	return _OK;
}

static int PtDrvClose( void )
{
	if( _FALSE == PtDrvInitialized )
		return _E_NOT_INIT;

	return ptdrvClose( PtDrvDevices[0] );
}

static int PtDrvIoctl( UInt cmd, pVoid arg )
{
	if( _FALSE == PtDrvInitialized )
		return _E_NOT_INIT;

	return ptdrvIoctl( PtDrvDevices[0], cmd, arg);
}

static int PtDrvRead ( pUChar buffer, int count )
{
	if( _FALSE == PtDrvInitialized )
		return _E_NOT_INIT;

	return ptdrvRead( PtDrvDevices[0], buffer, count );
}

/* END PLUSTEK-PP_PTDRV.C ...................................................*/
