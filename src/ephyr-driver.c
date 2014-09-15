/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors (from original xf86-video-nested):
 *
 * Paulo Zanoni <pzanoni@mandriva.com>
 * Tuan Bui <tuanbui918@gmail.com>
 * Colin Cornaby <colin.cornaby@mac.com>
 * Timothy Fleck <tim.cs.pdx@gmail.com>
 * Colin Hill <colin.james.hill@gmail.com>
 * Weseung Hwang <weseung@gmail.com>
 * Nathaniel Way <nathanielcw@hotmail.com>
 *
 * Authors
 *
 * Laércio de Sousa <laerciosousa@sme-mogidascruzes.sp.gov.br>
 *
 */

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>
#include <fb.h>
#include <micmap.h>
#include <mipointer.h>
#include <shadow.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86str.h>

#include "compat-api.h"

#define EPHYR_VERSION 0
#define EPHYR_NAME "EPHYR"
#define EPHYR_DRIVER_NAME "ephyr"

#define EPHYR_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define EPHYR_MINOR_VERSION PACKAGE_VERSION_MINOR
#define EPHYR_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

#define TIMER_CALLBACK_INTERVAL 20

static MODULESETUPPROTO(EphyrSetup);
static void EphyrIdentify(int flags);
static const OptionInfoRec *EphyrAvailableOptions(int chipid, int busid);
static Bool EphyrProbe(DriverPtr drv, int flags);
static Bool EphyrDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
                             pointer ptr);

static Bool EphyrPreInit(ScrnInfoPtr pScrn, int flags);
static Bool EphyrScreenInit(SCREEN_INIT_ARGS_DECL);

static Bool EphyrSwitchMode(SWITCH_MODE_ARGS_DECL);
static void EphyrAdjustFrame(ADJUST_FRAME_ARGS_DECL);
static Bool EphyrEnterVT(VT_FUNC_ARGS_DECL);
static void EphyrLeaveVT(VT_FUNC_ARGS_DECL);
static void EphyrFreeScreen(FREE_SCREEN_ARGS_DECL);
static ModeStatus EphyrValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                  Bool verbose, int flags);

static Bool EphyrSaveScreen(ScreenPtr pScreen, int mode);
static Bool EphyrCreateScreenResources(ScreenPtr pScreen);

static void EphyrShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf);
static Bool EphyrCloseScreen(CLOSE_SCREEN_ARGS_DECL);

static void EphyrBlockHandler(pointer data, OSTimePtr wt, pointer LastSelectMask);
static void EphyrWakeupHandler(pointer data, int i, pointer LastSelectMask);

int EphyrValidateModes(ScrnInfoPtr pScrn);
Bool EphyrAddMode(ScrnInfoPtr pScrn, int width, int height);
void EphyrPrintPscreen(ScrnInfoPtr p);
void EphyrPrintMode(ScrnInfoPtr p, DisplayModePtr m);

typedef enum {
    OPTION_DISPLAY,
    OPTION_ORIGIN
} EphyrOpts;

typedef enum {
    EPHYR_CHIP
} EphyrType;

static SymTabRec EphyrChipsets[] = {
    { EPHYR_CHIP, "ephyr" },
    {-1,            NULL }
};

/*
 * Original Xephyr command-line options (for further reference):
 *
 * -parent <XID>        Use existing window as Xephyr root win
 * -sw-cursor           Render cursors in software in Xephyr
 * -fullscreen          Attempt to run Xephyr fullscreen
 * -output <NAME>       Attempt to run Xephyr fullscreen (restricted to given output geometry)
 * -grayscale           Simulate 8bit grayscale
 * -resizeable          Make Xephyr windows resizeable
 *
 * #ifdef GLAMOR
 * -glamor              Enable 2D acceleration using glamor
 * -glamor_gles2        Enable 2D acceleration using glamor (with GLES2 only)
 * #endif
 *
 * -fakexa              Simulate acceleration using software rendering
 * -verbosity <level>   Set log verbosity level
 *
 * #ifdef GLXEXT
 * -nodri               do not use DRI
 * #endif
 *
 * -noxv                do not use XV
 * -name [name]         define the name in the WM_CLASS property
 * -title [title]       set the window title in the WM_NAME property
 */
static OptionInfoRec EphyrOptions[] = {
    { OPTION_DISPLAY, "Display", OPTV_STRING, {0}, FALSE },
    { OPTION_ORIGIN,  "Origin",  OPTV_STRING, {0}, FALSE },
    { -1,             NULL,      OPTV_NONE,   {0}, FALSE }
};

_X_EXPORT DriverRec EPHYR = {
    EPHYR_VERSION,
    EPHYR_DRIVER_NAME,
    EphyrIdentify,
    EphyrProbe,
    EphyrAvailableOptions,
    NULL, /* module */
    0,    /* refCount */
    EphyrDriverFunc,
    NULL, /* DeviceMatch */
    0     /* PciProbe */
};

static XF86ModuleVersionInfo EphyrVersRec = {
    EPHYR_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    EPHYR_MAJOR_VERSION,
    EPHYR_MINOR_VERSION,
    EPHYR_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0} /* checksum */
};

_X_EXPORT XF86ModuleData ephyrModuleData = {
    &EphyrVersRec,
    EphyrSetup,
    NULL, /* teardown */
};

/* These stuff should be valid to all server generations */
typedef struct EphyrPrivate {
    char                        *displayName;
    int                          originX;
    int                          originY;
    EphyrClientPrivatePtr       clientData;
    CreateScreenResourcesProcPtr CreateScreenResources;
    CloseScreenProcPtr           CloseScreen;
    ShadowUpdateProc             update;
} EphyrPrivate, *EphyrPrivatePtr;

#define PEPHYR(p)    ((EphyrPrivatePtr)((p)->driverPrivate))
#define PCLIENTDATA(p) (PEPHYR(p)->clientData)

/*static ScrnInfoPtr EPHYRScrn;*/

static pointer
EphyrSetup(pointer module, pointer opts, int *errmaj, int *errmin) {
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        
        xf86AddDriver(&EPHYR, module, HaveDriverFuncs);
        
        return (pointer)1;
    } else {
        if (errmaj)
            *errmaj = LDR_ONCEONLY;
        
        return NULL;
    }
}

static void
EphyrIdentify(int flags) {
    xf86PrintChipsets(EPHYR_NAME, "Driver for ephyr servers",
                      EphyrChipsets);
}

static const OptionInfoRec *
EphyrAvailableOptions(int chipid, int busid) {
    return EphyrOptions;
}

static Bool
EphyrProbe(DriverPtr drv, int flags) {
    Bool foundScreen = FALSE;
    int numDevSections;
    GDevPtr *devSections;
    int i;

    ScrnInfoPtr pScrn;
    int entityIndex;

    if (flags & PROBE_DETECT)
        return FALSE;

    if ((numDevSections = xf86MatchDevice(EPHYR_DRIVER_NAME,
                                          &devSections)) <= 0) {
        return FALSE;
    }

    if (numDevSections > 0) {
        for(i = 0; i < numDevSections; i++) {
            pScrn = NULL;
            entityIndex = xf86ClaimNoSlot(drv, EPHYR_CHIP, devSections[i],
                                          TRUE);
            pScrn = xf86AllocateScreen(drv, 0);
            if (pScrn) {
                xf86AddEntityToScreen(pScrn, entityIndex);
                pScrn->driverVersion = EPHYR_VERSION;
                pScrn->driverName    = EPHYR_DRIVER_NAME;
                pScrn->name          = EPHYR_NAME;
                pScrn->Probe         = EphyrProbe;
                pScrn->PreInit       = EphyrPreInit;
                pScrn->ScreenInit    = EphyrScreenInit;
                pScrn->SwitchMode    = EphyrSwitchMode;
                pScrn->AdjustFrame   = EphyrAdjustFrame;
                pScrn->EnterVT       = EphyrEnterVT;
                pScrn->LeaveVT       = EphyrLeaveVT;
                pScrn->FreeScreen    = EphyrFreeScreen;
                pScrn->ValidMode     = EphyrValidMode;
                foundScreen = TRUE;
            }
        }
    }

    return foundScreen;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
EphyrDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr) {
    CARD32 *flag;
    xf86Msg(X_INFO, "EphyrDriverFunc\n");

    /* XXX implement */
    switch(op) {
        case GET_REQUIRED_HW_INTERFACES:
            flag = (CARD32*)ptr;
            (*flag) = HW_SKIP_CONSOLE;
            return TRUE;

        case RR_GET_INFO:
        case RR_SET_CONFIG:
        case RR_GET_MODE_MM:
        default:
            return FALSE;
    }
}

static Bool EphyrAllocatePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate != NULL) {
        xf86Msg(X_WARNING, "EphyrAllocatePrivate called for an already "
                "allocated private!\n");
        return FALSE;
    }

    pScrn->driverPrivate = xnfcalloc(sizeof(EphyrPrivate), 1);
    if (pScrn->driverPrivate == NULL)
        return FALSE;
    return TRUE;
}

static void EphyrFreePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Double freeing EphyrPrivate!\n");
        return;
    }

    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* Data from here is valid to all server generations */
static Bool EphyrPreInit(ScrnInfoPtr pScrn, int flags) {
    EphyrPrivatePtr pEphyr;
    char *originString = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrPreInit\n");

    if (flags & PROBE_DETECT)
        return FALSE;

    if (!EphyrAllocatePrivate(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to allocate private\n");
        return FALSE;
    }

    pEphyr = PEPHYR(pScrn);

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support24bppFb | Support32bppFb))
        return FALSE;
 
    xf86PrintDepthBpp(pScrn);

    if (pScrn->depth > 8) {
        rgb zeros = {0, 0, 0};
        if (!xf86SetWeight(pScrn, zeros, zeros)) {
            return FALSE;
        }
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor; /* XXX */

    xf86CollectOptions(pScrn, NULL);
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, EphyrOptions);

    if (xf86IsOptionSet(EphyrOptions, OPTION_DISPLAY)) {
        pEphyr->displayName = xf86GetOptValString(EphyrOptions,
                                                   OPTION_DISPLAY);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using display \"%s\"\n",
                   pEphyr->displayName);
    } else {
        pEphyr->displayName = NULL;
    }

    if (xf86IsOptionSet(EphyrOptions, OPTION_ORIGIN)) {
        originString = xf86GetOptValString(EphyrOptions, OPTION_ORIGIN);
        if (sscanf(originString, "%d %d", &pEphyr->originX,
            &pEphyr->originY) != 2) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Invalid value for option \"Origin\"\n");
            return FALSE;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using origin x:%d y:%d\n",
                   pEphyr->originX, pEphyr->originY);
    } else {
        pEphyr->originX = 0;
        pEphyr->originY = 0;
    }

    xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (!EphyrClientCheckDisplay(pEphyr->displayName)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Can't open display: %s\n",
                   pEphyr->displayName);
        return FALSE;
    }

    if (!EphyrClientValidDepth(pScrn->depth)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Invalid depth: %d\n",
                   pScrn->depth);
        return FALSE;
    }

    /*if (pScrn->depth > 1) {
        Gamma zeros = {0.0, 0.0, 0.0};
        if (!xf86SetGamma(pScrn, zeros))
            return FALSE;
    }*/

    if (EphyrValidateModes(pScrn) < 1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes\n");
        return FALSE;
    }


    if (!pScrn->modes) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
        return FALSE;
    }
    xf86SetCrtcForModes(pScrn, 0);

    pScrn->currentMode = pScrn->modes;

    xf86SetDpi(pScrn, 0, 0);

    if (!xf86LoadSubModule(pScrn, "shadow"))
        return FALSE;
    if (!xf86LoadSubModule(pScrn, "fb"))
        return FALSE;

    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;
    
    return TRUE;
}

int
EphyrValidateModes(ScrnInfoPtr pScrn) {
    DisplayModePtr mode;
    int i, width, height, ret = 0;
    int maxX = 0, maxY = 0;

    /* Print useless stuff */
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Monitor wants these modes:\n");
    for(mode = pScrn->monitor->Modes; mode != NULL; mode = mode->next) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s (%dx%d)\n", mode->name,
                   mode->HDisplay, mode->VDisplay);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Too bad for it...\n");

    /* If user requested modes, add them. If not, use 640x480 */
    if (pScrn->display->modes != NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "User wants these modes:\n");
        for(i = 0; pScrn->display->modes[i] != NULL; i++) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s\n",
                       pScrn->display->modes[i]);
            if (sscanf(pScrn->display->modes[i], "%dx%d", &width,
                       &height) != 2) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                           "This is not the mode name I was expecting...\n");
                return 0;
            }
            if (!EphyrAddMode(pScrn, width, height)) {
                return 0;
            }
        }
    } else {
        if (!EphyrAddMode(pScrn, 640, 480)) {
            return 0;
        }
    }

    pScrn->modePool = NULL;

    /* Now set virtualX, virtualY, displayWidth and virtualFrom */

    if (pScrn->display->virtualX >= pScrn->modes->HDisplay &&
        pScrn->display->virtualY >= pScrn->modes->VDisplay) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        /* XXX: if not specified, make virtualX and virtualY as big as the max X
         * and Y. I'm not sure this is correct */
        mode = pScrn->modes;
        while (mode != NULL) {
            if (mode->HDisplay > maxX)
                maxX = mode->HDisplay;
       
            if (mode->VDisplay > maxY)
                maxY = mode->VDisplay;
          
            mode = mode->next;
        }
        pScrn->virtualX = maxX;
        pScrn->virtualY = maxY;
    }
    pScrn->virtualFrom = X_DEFAULT;
    pScrn->displayWidth = pScrn->virtualX;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Virtual size: %dx%d\n",
               pScrn->virtualX, pScrn->virtualY);

    /* Calculate the return value */
    mode = pScrn->modes;
    while (mode != NULL) {
        mode = mode->next;
        ret++;
    }

    /* Finally, make the mode list circular */
    pScrn->modes->prev->next = pScrn->modes;

    return ret;
}

Bool
EphyrAddMode(ScrnInfoPtr pScrn, int width, int height) {
    DisplayModePtr mode;
    char nameBuf[64];
    size_t len;

    if (snprintf(nameBuf, 64, "%dx%d", width, height) >= 64)
        return FALSE;

    mode = XNFcalloc(sizeof(DisplayModeRec));
    mode->status = MODE_OK;
    mode->type = M_T_DRIVER;
    mode->HDisplay = width;
    mode->VDisplay = height;

    len = strlen(nameBuf);
    mode->name = XNFalloc(len+1);
    strcpy(mode->name, nameBuf);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Adding mode %s\n", mode->name);

    /* Now add mode to pScrn->modes. We'll keep the list non-circular for now,
     * but we'll maintain pScrn->modes->prev to know the last element */
    mode->next = NULL;
    if (!pScrn->modes) {
        pScrn->modes = mode;
        mode->prev = mode;
    } else {
        mode->prev = pScrn->modes->prev;
        pScrn->modes->prev->next = mode;
        pScrn->modes->prev = mode;
    }

    return TRUE;
}

static void
EphyrBlockHandler(pointer data, OSTimePtr wt, pointer LastSelectMask) {
    EphyrClientPrivatePtr pEphyrClient = data;
    EphyrClientCheckEvents(pEphyrClient);
}

static void
EphyrWakeupHandler(pointer data, int i, pointer LastSelectMask) {
}

/* Called at each server generation */
static Bool EphyrScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    EphyrPrivatePtr pEphyr;
    Pixel redMask, greenMask, blueMask;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrScreenInit\n");

    pEphyr = PEPHYR(pScrn);
    /*EPHYRScrn = pScrn;*/

    EphyrPrintPscreen(pScrn);

    /* Save state:
     * EphyrSave(pScrn); */
    
    //Load_Ephyr_Mouse();

    pEphyr->clientData = EphyrClientCreateScreen(pScrn->scrnIndex,
                                                   pEphyr->displayName,
                                                   pScrn->virtualX,
                                                   pScrn->virtualY,
                                                   pEphyr->originX,
                                                   pEphyr->originY,
                                                   pScrn->depth,
                                                   pScrn->bitsPerPixel,
                                                   &redMask, &greenMask, &blueMask);
    
    if (!pEphyr->clientData) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create client screen\n");
        return FALSE;
    }
    
    miClearVisualTypes();
    if (!miSetVisualTypesAndMasks(pScrn->depth,
                                  miGetDefaultVisualMask(pScrn->depth),
                                  pScrn->rgbBits, pScrn->defaultVisual,
                                  redMask, greenMask, blueMask))
        return FALSE;
    
    if (!miSetPixmapDepths())
        return FALSE;

    if (!fbScreenInit(pScreen, EphyrClientGetFrameBuffer(PCLIENTDATA(pScrn)),
                      pScrn->virtualX, pScrn->virtualY, pScrn->xDpi,
                      pScrn->yDpi, pScrn->displayWidth, pScrn->bitsPerPixel))
        return FALSE;

    fbPictureInit(pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);
    xf86SetBackingStore(pScreen);
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());
    
    if (!miCreateDefColormap(pScreen))
        return FALSE;

    pEphyr->update = EphyrShadowUpdate;
    pScreen->SaveScreen = EphyrSaveScreen;

    if (!shadowSetup(pScreen))
        return FALSE;

    pEphyr->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = EphyrCreateScreenResources;

    pEphyr->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = EphyrCloseScreen;

    RegisterBlockAndWakeupHandlers(EphyrBlockHandler, EphyrWakeupHandler, pEphyr->clientData);

    return TRUE;
}

static Bool
EphyrCreateScreenResources(ScreenPtr pScreen) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "EphyrCreateScreenResources\n");
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    EphyrPrivatePtr pEphyr = PEPHYR(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = pEphyr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = EphyrCreateScreenResources;

    if(!shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen),
                  pEphyr->update, NULL, 0, 0)) {
        xf86DrvMsg(pScreen->myNum, X_ERROR, "EphyrCreateScreenResources failed to shadowAdd.\n");
        return FALSE;
    }

    return ret;
}

static void
EphyrShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf) {
    RegionPtr pRegion = DamageRegion(pBuf->pDamage);
    EphyrClientUpdateScreen(PCLIENTDATA(xf86ScreenToScrn(pScreen)),
                             pRegion->extents.x1, pRegion->extents.y1,
                             pRegion->extents.x2, pRegion->extents.y2);
}

static Bool
EphyrCloseScreen(CLOSE_SCREEN_ARGS_DECL) {
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrCloseScreen\n");

    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));

    RemoveBlockAndWakeupHandlers(EphyrBlockHandler, EphyrWakeupHandler, PEPHYR(pScrn)->clientData);
    EphyrClientCloseScreen(PCLIENTDATA(pScrn));

    pScreen->CloseScreen = PEPHYR(pScrn)->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool EphyrSaveScreen(ScreenPtr pScreen, int mode) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "EphyrSaveScreen\n");
    return TRUE;
}

static Bool EphyrSwitchMode(SWITCH_MODE_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrSwitchMode\n");
    return TRUE;
}

static void EphyrAdjustFrame(ADJUST_FRAME_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrAdjustFrame\n");
}

static Bool EphyrEnterVT(VT_FUNC_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrEnterVT\n");
    return TRUE;
}

static void EphyrLeaveVT(VT_FUNC_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrLeaveVT\n");
}

static void EphyrFreeScreen(FREE_SCREEN_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrFreeScreen\n");
}

static ModeStatus EphyrValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                  Bool verbose, int flags) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EphyrValidMode:\n");

    if (!mode)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "NULL MODE!\n");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  name: %s\n", mode->name);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  HDisplay: %d\n", mode->HDisplay);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  VDisplay: %d\n", mode->VDisplay);
    return MODE_OK;
}

void EphyrPrintPscreen(ScrnInfoPtr p) {
    /* XXX: finish implementing this someday? */
    xf86DrvMsg(p->scrnIndex, X_INFO, "Printing pScrn:\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverVersion: %d\n", p->driverVersion);
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverName:    %s\n", p->driverName);
    xf86DrvMsg(p->scrnIndex, X_INFO, "pScreen:       %p\n", p->pScreen);
    xf86DrvMsg(p->scrnIndex, X_INFO, "scrnIndex:     %d\n", p->scrnIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "configured:    %d\n", p->configured);
    xf86DrvMsg(p->scrnIndex, X_INFO, "origIndex:     %d\n", p->origIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "imageByteOrder: %d\n", p->imageByteOrder);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlineUnit: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlinePad: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapBitOrder: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "numFormats: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "formats[]: 0x%x\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "fbFormat: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitsPerPixel: %d\n", p->bitsPerPixel);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "pixmap24: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "depth: %d\n", p->depth);
    EphyrPrintMode(p, p->currentMode);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "depthFrom: %\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "\n");*/
}

void EphyrPrintMode(ScrnInfoPtr p, DisplayModePtr m) {
    xf86DrvMsg(p->scrnIndex, X_INFO, "HDisplay   %d\n",   m->HDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncStart %d\n", m->HSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncEnd   %d\n",   m->HSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HTotal     %d\n",     m->HTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSkew      %d\n",      m->HSkew);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VDisplay   %d\n",   m->VDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncStart %d\n", m->VSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncEnd   %d\n",   m->VSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VTotal     %d\n",     m->VTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VScan      %d\n",      m->VScan);
}
