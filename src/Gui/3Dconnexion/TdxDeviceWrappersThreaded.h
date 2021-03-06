/*
 * @file    TdxDeviceWrappers.h
 * @brief   Wrappers for 3Dx driver events and callbacks
 *
 * Copyright (c) 1998-2007 3Dconnexion. All rights reserved. 
 *
 * Permission to use, copy, modify, and distribute this software for all
 * purposes and without fees is hereby granted provided that this copyright
 * notice appears in all copies.  Permission to modify this software is granted
 * and 3Dconnexion will support such modifications only if said modifications
 * are approved by 3Dconnexion.
 *
 */

#ifndef __TdxDeviceWrappers_h__
#define __TdxDeviceWrappers_h__

#include "3DconnexionClient/ConnexionClientAPI.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
#pragma mark * Weakly linked symbols *
    
/* Weak link 3Dx framework so this app can run without crashing even on 
 * systems that don't have the 3Dx driver installed. For more info on 
 * weak linking, see:
 * http://developer.apple.com/documentation/MacOSX/Conceptual/BPFrameworks/Concepts/WeakLinking.html 
 */
    extern OSErr InstallConnexionHandlers(ConnexionMessageHandlerProc messageHandler, ConnexionAddedHandlerProc addedHandler, ConnexionRemovedHandlerProc removedHandler) __attribute__((weak_import));

      
/* ========================================================================== */
#pragma mark * 3Dx Events Types *

/* 3Dconnexion event class */
enum 
{
	kEventClassTdxDevice = 'tdxE'	
};

/* 3Dconnexion event kinds */
typedef enum 
{
	kTdxDeviceEventButton = 1,
	kTdxDeviceEventMotion,
	kTdxDeviceEventZero,
} TdxDeviceEventKind;

/* 3Dconnexion event parameters */
typedef enum {
	kEventParamTX             = 'Tpnx', /* typeSInt16 */
	kEventParamTY             = 'Tpny', /* typeSInt16 */
	kEventParamTZ             = 'Tpnz', /* typeSInt16 */
	kEventParamRX             = 'Trtx', /* typeSInt16 */
	kEventParamRY             = 'Trty', /* typeSInt16 */
	kEventParamRZ             = 'Trtz', /* typeSInt16 */
	kEventParamButtonID       = 'Tbtn', /* typeSInt16 */
	kEventParamButtonDown     = 'Tbts', /* typeSInt16 */
} TdxDeviceEventParam;

typedef SInt16 TdxDeviceAxes[6];

/* ========================================================================== */
#pragma mark * Façade of 3DconnexionClient framework APIs *

/** Initialization routine for starting a data link to the 3Dconnexion driver.
 *	@param appID    signature of the parent application calling this function
 *  @param showOnlyMyClientEvents   this flag determines whether only events 
 *                                  meant for this registered client are 
 *                                  processed or if all events from the device 
 *                                  will be sent to this client
 *  @param mode     possible values are kConnexionClientModeTakeOver or 
 *                  kConnexionClientModePlugin
 *  @param mask     Possible values are kConnexionMask* as listed 
 *                  in ConnexionClient.h
 */
long TdxInitDevice(UInt32 appID,
                   Boolean showOnlyMyClientEvents, 
                   UInt16 mode, 
                   UInt32 mask);

/** Cleanup rountine to close down the data link with the driver */
void TdxTerminateDevice();

#ifdef __cplusplus
}
#endif

#endif
