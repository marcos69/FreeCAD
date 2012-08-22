/*
 * @file    TdxDeviceWrappers.c
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

#include <unistd.h>
#include <pthread.h>
#include <Carbon/Carbon.h>
#include "TdxDeviceWrappersThreaded.h"
#include "SpaceballEvent.h"
#include <QApplication>
#include <QWidget>
bool axesRotLock = FALSE;
bool button3Pressed = FALSE;

extern "C"
{

/* ========================================================================== */
#pragma mark * Data structures *

typedef struct 
{
    useconds_t      pollLoopTimeDelta;
    SInt16          axes[6];
    UInt16          buttons;
    SInt16          buttonState;
} TdxDeviceInfo;

typedef struct {
	UInt16 				clientID; /* ID assigned by the driver */
	Boolean				showClientEventsOnly;
	EventQueueRef		mainEventQueue;
    EventHandlerRef     carbonEvHandler;
    pthread_mutex_t     mutex;
} TdxDeviceWrapperInfo, *TdxDeviceWrapperInfoPtr;

/* ========================================================================== */
#pragma mark * Global variables Initialization *

TdxDeviceWrapperInfo gDevWrapperInfo;

TdxDeviceInfo gDevInfo = 
{
    /* fps      axes array           btn btnStatus  */
    1000000/50, { 0, 0, 0, 0, 0, 0 }, 0, 0
};

/* ========================================================================== */
#pragma mark * Function prototypes *

static void tdx_drv_handler(io_connect_t connection, 
                            natural_t messageType, 
                            void *messageArgument);

static UInt64 usecs_since_startup();
static OSStatus CopyFrontProcessName(CFStringRef *outFrontAppName);
static OSStatus tdx_create_loop();
static void *tdx_thread_loop(void *param);

/** Installs a carbon events handler on the main queue, allowing all
    driver events to be computed in the main thread. */
static OSStatus tdx_inst_compute_handler(void *inUserData,
                                         EventHandlerRef *outHandler);

/** Actually compute the CE-repackaged driver events */
static pascal OSStatus tdx_compute_handler(EventHandlerCallRef nextHandler, 
                                           EventRef theEvent, 
                                           void *userData);

/* ========================================================================== */
#pragma mark -
#pragma mark * Façade of 3DconnexionClient framework APIs *

/* -------------------------------------------------------------------------- */
long
TdxInitDevice(UInt32 appID,
			  Boolean showOnlyMyClientEvents, 
              UInt16 mode, 
              UInt32 mask)
{
	OSStatus err;
    
	gDevWrapperInfo.showClientEventsOnly = showOnlyMyClientEvents;

	/* must save the main event queue while we're running within the main 
       thread. GetMainEventQueue() is not thread-safe and will cause big 
       problems if called from the spawned thread. */
	gDevWrapperInfo.mainEventQueue = GetMainEventQueue();
	
    /* make sure the framework is installed */
	if (InstallConnexionHandlers ==  NULL)
    {
        fprintf(stderr, "3Dconnexion framework not found!\n");
        return -1;
    }

     
     pthread_mutex_init(&gDevWrapperInfo.mutex, NULL);
    
    /*install 3dx message handler in order to receive driver events */
    err = InstallConnexionHandlers(tdx_drv_handler, 0L, 0L);
	assert(err == 0);
    if (err)
        return err;
    
    /* register our app with the driver */
	gDevWrapperInfo.clientID = RegisterConnexionClient(appID, 0, mode, mask);
    if (gDevWrapperInfo.clientID == 0)
		return -2;
    
    fprintf(stderr, "3Dconnexion device initialized. Client ID: %d\n", 
            gDevWrapperInfo.clientID);
	
    /* install carbon handler to compute the driver events from main thread */
    err = tdx_inst_compute_handler(NULL, &gDevWrapperInfo.carbonEvHandler);
    assert(gDevWrapperInfo.carbonEvHandler);
    assert(err == 0);
    if (err)
        return -3;
    
    /* create the thread that will poll the device status */
    err = tdx_create_loop();
    assert(err == 0);
    if (err)
        return -4;
    
    return err;
}

/* -------------------------------------------------------------------------- */
void 
TdxTerminateDevice()
{
    UInt16 wasConnexionOpen = gDevWrapperInfo.clientID;
    
    /* make sure the framework is installed */
	if (InstallConnexionHandlers == NULL)
        return;
    
    /* this will make the polling thread exit the loop (see tdx_thread_loop) */
    gDevWrapperInfo.clientID = 0;
    
    /* sleep for 2 frames to make sure we have exited the loop  */
    usleep(gDevInfo.pollLoopTimeDelta * 2); /* 1/10 sec */

    if (gDevWrapperInfo.carbonEvHandler)
        RemoveEventHandler(gDevWrapperInfo.carbonEvHandler);
    
    pthread_mutex_destroy(&gDevWrapperInfo.mutex);

    /* close the connection with the 3dx driver */
    if (wasConnexionOpen)
        UnregisterConnexionClient(gDevWrapperInfo.clientID);
	CleanupConnexionHandlers();
    
    fprintf(stderr, "Terminated connection with 3Dconnexion device.\n");
}

#pragma mark -
#pragma mark * Static functions implementation *

/* ----------------------------------------------------------------------------
    Handler for driver events. This function is able to handle the events in
    different ways: (1) re-package the events as Carbon events, (2) compute
    them directly, (3) write the event info in a shared memory location for
    usage by reader threads.
*/
static void 
tdx_drv_handler(io_connect_t connection, 
                natural_t messageType, 
                void *messageArgument)
{
	ConnexionDeviceStatePtr msg = (ConnexionDeviceStatePtr)messageArgument;
	static UInt16 lastBtnPressed = 0;
    
    switch(messageType)
	{
		case kConnexionMsgDeviceState:
			/* Device state messages are broadcast to all clients.  It is up to
			 * the client to figure out if the message is meant for them. This
			 * is done by comparing the "client" id sent in the message to our
             * assigned id when the connection to the driver was established.
			 * 
			 * There is a special mode wherein all events are sent to this 
			 * client regardless if it was meant for it or not.  This mode is 
			 * determined by the showClientEventOnly flag.
			 */
			 
            if (!gDevWrapperInfo.showClientEventsOnly 
			    || msg->client == gDevWrapperInfo.clientID)
			{
				switch (msg->command)
				{
                    case kConnexionCmdHandleAxis:
                    {
                        int err = pthread_mutex_trylock(&gDevWrapperInfo.mutex);
                        if (err == 0)
                        {
                            memcpy(gDevInfo.axes, msg->axis, 
                                   sizeof(gDevInfo.axes));
                            err = pthread_mutex_unlock(&gDevWrapperInfo.mutex);
                            assert(err == 0);
                        }
                        
                        break;
                    }
                        
                    case kConnexionCmdHandleButtons:
                    {
                        SInt16 buttonState;

                        if (msg->value == 0)
                        {
                            buttonState = 0;
                        }
                        else
                        {
                            lastBtnPressed = msg->buttons;
                            buttonState = 1;
                        }
                        
                        if (pthread_mutex_trylock(&gDevWrapperInfo.mutex) == 0)
                        {
                            gDevInfo.buttons     = lastBtnPressed;
                            gDevInfo.buttonState = buttonState;
                            pthread_mutex_unlock(&gDevWrapperInfo.mutex);
                        }
                        
						break;
                    }
                        
                    default:
                        break;

				} /* switch */
            }

			break;

		default:
			/* other messageTypes can happen and should be ignored */
			break;
	}
    
  /*  printf("connection: %X\n", connection);
    printf("messageType: %X\n", messageType);
    printf("version: %d\n", msg->version);
    printf("front app client: %d  ourID: %d\n", 
           msg->client, gDevWrapperInfo.clientID);
    printf("command: %u\n", msg->command);
    printf("value: %ld\n", msg->value);
    printf("param: %hd\n", msg->param);
    for (int i=0; i<8; i++)
        printf("report[%d]: %d\n", i, msg->report[i]);
    printf("buttons: %d\n", msg->buttons);
    printf("TX: %d\n", msg->axis[0]);
    printf("TY: %d\n", msg->axis[1]);
    printf("TZ: %d\n", msg->axis[2]);
    printf("RX: %d\n", msg->axis[3]);
    printf("RY: %d\n", msg->axis[4]);
    printf("RZ: %d\n", msg->axis[5]);
    printf("-----------------------------------------\n\n");
  */  
#ifdef __MWERKS__
	#pragma unused(connection)
#endif	
}

/* ----------------------------------------------------------------------------
    Installs a Carbon handler to process the driver events as normal Carbon
    events.
*/
static OSStatus 
tdx_inst_compute_handler(void *inUserData, 
                         EventHandlerRef *outCarbonHandler)
{
#define             kTdxNumCarbonEvents   3
    EventTypeSpec   evTypes[kTdxNumCarbonEvents];
    EventTargetRef  target;
    EventHandlerUPP upp;
    OSStatus        err;
    
    evTypes[0].eventClass = kEventClassTdxDevice;
    evTypes[0].eventKind  = kTdxDeviceEventMotion;
    evTypes[1].eventClass = kEventClassTdxDevice;
    evTypes[1].eventKind  = kTdxDeviceEventButton;
    evTypes[2].eventClass = kEventClassTdxDevice;
    evTypes[2].eventKind  = kTdxDeviceEventZero;

    upp = NewEventHandlerUPP(tdx_compute_handler);
    target = GetApplicationEventTarget();
    
    err = InstallEventHandler(target, upp, 
                              kTdxNumCarbonEvents, evTypes, 
                              inUserData, outCarbonHandler);
    assert(err == noErr);
    return err;
}

/* ---------------------------------------------------------------------------- 
    Carbon event handler to compute device events that were wrapped as Carbon
    events. (See tdx_thread_loop().)
*/
static pascal OSStatus 
tdx_compute_handler(EventHandlerCallRef nextHandler, 
                    EventRef theEvent, 
                    void *userData)
{
#pragma unused(nextHandler)

    OSStatus err = noErr;
    
    if (GetEventClass(theEvent) != kEventClassTdxDevice)
        return eventNotHandledErr;
        
 		QWidget *currentWindow = QApplication::activeWindow();
		QWidget *currentWidget = currentWindow->focusWidget();
    		if (!currentWidget)
    		{
         		err = TRUE;
         		return err;
			}

    switch (GetEventKind(theEvent))
    {
        case kTdxDeviceEventMotion:
        {
            static SInt16 axes[6];

            GetEventParameter(theEvent, kEventParamTX, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[0]);
            GetEventParameter(theEvent, kEventParamTY, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[1]);
            GetEventParameter(theEvent, kEventParamTZ, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[2]);
            GetEventParameter(theEvent, kEventParamRX, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[3]);
            GetEventParameter(theEvent, kEventParamRY, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[4]);
            GetEventParameter(theEvent, kEventParamRZ, typeSInt16, NULL, 
                              sizeof(*axes), NULL, &axes[5]);

//            TdxComputeAxes(axes);
        	Spaceball::MotionEvent *motionEvent = new Spaceball::MotionEvent();
      		motionEvent->setTranslations(axes[0], axes[1], axes[2]);
      		if ( axesRotLock==FALSE )
        			motionEvent->setRotations(axes[3], axes[4], axes[5]);
        	QApplication::sendEvent(currentWidget, motionEvent);
            break;
        }
            
        case kTdxDeviceEventButton:
        {
            static UInt16 buttons;
            static SInt16 buttonState;

            GetEventParameter(theEvent, kEventParamButtonID, typeSInt16, NULL, 
                              sizeof(buttons), NULL, &buttons);
            GetEventParameter(theEvent, kEventParamButtonDown, typeSInt16, NULL, 
                              sizeof(buttonState), NULL, &buttonState);
                              
            Spaceball::ButtonEvent *buttonEvent = new Spaceball::ButtonEvent();
  		     if (buttons > 0) buttonEvent->setButtonNumber(buttons-1);
  		     else buttonEvent->setButtonNumber(0);
  		     
   		     if (buttonState)
   		     {
      	      buttonEvent->setButtonStatus(Spaceball::BUTTON_PRESSED);
	  		     switch (buttons)
	  		     {
#ifdef COSPNAV_ESCAKEY
     	     		case 1:
      	 	    		{
      	     				QApplication::postEvent(currentWidget, (new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier,0) ) );
       			 		break;
       			 		}
#endif

					case 2:
      	        		{
      	     				QApplication::postEvent(currentWidget, (new QKeyEvent(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier,0) ) );
       		 			break;
       		 			}	

#ifdef COSPNAV_ROTLOCK
     	     		case 3:
      	       			{
  							if ( axesRotLock==FALSE && button3Pressed==FALSE ) 
  									{
  									axesRotLock = TRUE;
  									button3Pressed = TRUE;
  									}
  							else 	axesRotLock = FALSE;
  					     break;
      		 			}
#endif
      		 		}
       		 }
       		 else
       		 {
       		 	   switch (buttons)
	  		     {
#ifdef COSPNAV_ESCAKEY
     	     		case 1:
		      	        {
			       		  QApplication::postEvent(currentWidget, (new QKeyEvent(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier,0) ) );
						break;
						}
#endif

                     case 2:
 		      	        {
			       		  QApplication::postEvent(currentWidget, (new QKeyEvent(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier,0) ) );
						break;
						}

#ifdef COSPNAV_ROTLOCK
     	     		case 3:
		      	        {
 	 					  button3Pressed=FALSE;
						break;
						}
#endif
					}
       		 }
       		 
       		 QApplication::sendEvent(currentWidget, buttonEvent);
            break;
        }
            
        case kTdxDeviceEventZero:
//            TdxComputeEventZero();
            break;
            
        default:
            err = eventNotHandledErr;
            break;
    }
    
    return err;
}

/* ----------------------------------------------------------------------------
    Create the reader thread that will synchronously process the device status.
*/
static OSStatus
tdx_create_loop()
{
    int err;
    pthread_t threads[1];
    
    err = pthread_create(&threads[0], NULL, tdx_thread_loop, NULL);
    assert(err == 0);
    
    return err;
}

/* ----------------------------------------------------------------------------
    Main loop of the reader thread processing the current device status with a 
    given rate. "Processing" is done in two alternative ways, (1) repackage
    the device info as Carbon Events, (2) compute the data immediately from 
    this current thread.
*/
static void *
tdx_thread_loop(void *param)
{
    SInt64 usecs;
    UInt64 t0;
    SInt16 snData[6], zerodata[6], prevButtonState, buttonState = 0;
    UInt16 prevButtons, buttons = 0;
    OSErr err;
    EventRef event = 0;
    Boolean wasZero, isZero = 1;
    CFStringRef frontAppName;
    
    memset(zerodata, 0, sizeof(zerodata));
    while (gDevWrapperInfo.clientID)
    {
        t0 = usecs_since_startup();

        wasZero = isZero;
        prevButtonState = buttonState;
        prevButtons     = buttons;

        err = pthread_mutex_trylock(&gDevWrapperInfo.mutex);
        if (err == 0)
        {
            /* read latest data from device */
            memcpy(snData, gDevInfo.axes, sizeof(gDevInfo.axes));
            buttons     = gDevInfo.buttons;
            buttonState = gDevInfo.buttonState;
            
            err = pthread_mutex_unlock(&gDevWrapperInfo.mutex);
            assert(err == 0);
        }

/*        fprintf(stderr, "[(%hd, %hd, %hd, %hd, %hd, %hd) (%hd %hd)]", 
                snData[0], snData[1], snData[2], 
                snData[3], snData[4], snData[5],
                buttons, buttonState);
 */      
 
        CopyFrontProcessName(&frontAppName);
                if (CFStringCompare(frontAppName, 
                            CFSTR("FreeCAD"), 0) == kCFCompareEqualTo)
          {
            /* Compute the axes */
            if ((isZero = (memcmp(snData, zerodata, sizeof(snData)) == 0)))
            {
                if (wasZero == FALSE /* don't send zero events more than once */
                    && ((err = CreateEvent(0, kEventClassTdxDevice, 
                                           kTdxDeviceEventZero, 0, 
                                           kEventAttributeUserEvent, 
                                           &event)) == noErr)
                    && event)
                {
                    err = PostEventToQueue(gDevWrapperInfo.mainEventQueue, 
                                           event, kEventPriorityHigh);
                    assert(err == 0);
                    ReleaseEvent(event);
                }
            }
            else 
            if (((err = CreateEvent(0, kEventClassTdxDevice, 
                                         kTdxDeviceEventMotion, 0, 
                                         kEventAttributeUserEvent, 
                                         &event)) == noErr)
                     && event)
            {
                SetEventParameter(event, kEventParamTX, typeSInt16, 
                                  sizeof(SInt16), &snData[0]);
                SetEventParameter(event, kEventParamTY, typeSInt16, 
                                  sizeof(SInt16), &snData[1]); 
                SetEventParameter(event, kEventParamTZ, typeSInt16, 
                                  sizeof(SInt16), &snData[2]);
                SetEventParameter(event, kEventParamRX, typeSInt16, 
                                  sizeof(SInt16), &snData[3]);
                SetEventParameter(event, kEventParamRY, typeSInt16, 
                                  sizeof(SInt16), &snData[4]);
                SetEventParameter(event, kEventParamRZ, typeSInt16, 
                                  sizeof(SInt16), &snData[5]);
                
                /* Fire off a message to the main event queue notifying that 
                   an event is available */
                err = PostEventToQueue(gDevWrapperInfo.mainEventQueue, 
                                       event, kEventPriorityHigh);
                assert(err == 0);
                ReleaseEvent(event);
            }

            /* Compute the buttons */
            if (prevButtonState != buttonState || prevButtons != buttons)
            {
                err = CreateEvent(0, kEventClassTdxDevice, 
                                  kTdxDeviceEventButton, 0, 
                                  kEventAttributeUserEvent, &event);
                
                if (err == noErr && event)
                {
                    SetEventParameter(event, kEventParamButtonID, 
                                      typeSInt16, sizeof(SInt16), 
                                      &buttons);
                    SetEventParameter(event, kEventParamButtonDown, 
                                      typeSInt16, sizeof(SInt16), 
                                      &buttonState);
                    
                    err = PostEventToQueue(gDevWrapperInfo.mainEventQueue, 
                                           event, kEventPriorityHigh);
                    assert(err == 0);
                    ReleaseEvent(event);
                }
            }
          }
   
         else 
          {
            // if we're not the front app, reset the shared buffer to avoid
             //  any further spurious processing 
            err = pthread_mutex_trylock(&gDevWrapperInfo.mutex);
            if (err == 0)
            {
                memset(gDevInfo.axes, 0, sizeof(gDevInfo.axes));
                gDevInfo.buttonState = 0;
                
                err = pthread_mutex_unlock(&gDevWrapperInfo.mutex);
              assert(err == 0);
            } 
      }

        CFRelease(frontAppName);
 
        /* otherwise, sleep until the next timeslot */
        usecs = gDevInfo.pollLoopTimeDelta - (usecs_since_startup() - t0);
        if (0 < usecs)
            usleep((useconds_t)usecs);
    }
    
    fprintf(stderr, "Exiting 3Dconnexion status processing synced loop.\n");

    return param;
}

/* -------------------------------------------------------------------------- */
static UInt64 
usecs_since_startup()
{
    static UnsignedWide usecs = {0, 0};
    Microseconds(&usecs);
    return UnsignedWideToUInt64(usecs);
}

/* -------------------------------------------------------------------------- */
static OSStatus 
CopyFrontProcessName(CFStringRef *outFrontAppName)
{
    ProcessSerialNumber psn;
    OSStatus err;
    
    GetFrontProcess(&psn);
    err = CopyProcessName(&psn, outFrontAppName);
    
    return err;
}
}
