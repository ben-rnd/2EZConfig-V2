/****************************************************************************/
/**                                                                        **/
/**                              Hardlock                                  **/
/**                    API-Structures and definitions                      **/
/**                                                                        **/
/**   This file contains some helpful defines to access a Hardlock using   **/
/**   the application programming interface (API) for Hardlock.            **/
/**                                                                        **/
/**                           Aladdin Germany                              **/
/**                                                                        **/
/**  Revision history                                                      **/
/**  ----------------
***  $Log: fastapi.h,v $
***  Revision 1.52  2003/04/30 12:21:14  chris
***  fix structure packing for Borland
***
***  Revision 1.51  2003/02/24 08:00:28  werner
***  Added RUS-Flag: DISABLE_TS_CHECK for Terminal Server detection
***
***  Revision 1.50  2003/01/30 09:48:13  axel
***  added functions API_GETHLSADDR 108, API_GETHLSTEXT 109
***  added error NO_LOCAL_FUNCTION 61
***
***  Revision 1.49  2002/08/23 10:47:10  axel
***  added API_READ_HLS, API_CALC_HLS and NO_REMOTE_FUNCTION
***  (used for detecting HL-Server Hardlock licenses)
***
***  Revision 1.48  2002/08/22 16:15:27  alex
***  added define _AKS_QT_APPLICATION_ if you want to compile with Qt,
***  because slots from lic structure is a Qt keyword
***
***  Revision 1.47  2002/03/18 13:24:34  chris
***  Win64 changes
***
***  Revision 1.46  2000/12/19 16:37:41  chris
***  detect MacOS X
***
***  Revision 1.45  2000/07/30 22:22:17  chris
***  ia64 detection
***
***  Revision 1.44  2000/07/10 09:45:09  chris
***  Module2 field
***
***  Revision 1.43  2000/05/25 14:11:43  chris
***  added some HASP stuff
***
***  Revision 1.42  2000/03/21 14:18:28  chris
***  HL_SIS and HL_LIS structure definitions
***
***  Revision 1.41  2000/02/18 14:04:44  chris
***  fixed pascal define for CygWin & MingW32
***
***  Revision 1.40  1999/12/06 13:06:11  chris
***  fixed structure packing for MSC compiler
***
***  Revision 1.39  1999/11/28 01:39:46  chris
***  added 64bit support (only tested with AlphaLinux currently)
***
***  Revision 1.38  1999/10/07 11:28:45  chris
***  Duplicate revision
***
***  Revision 1.37  1999/10/07 11:28:45  Henri
***  Removed uneeded TLV defines.
***
***  Revision 1.36  1999/10/07 10:47:04  Henri
***  Removed unused flags.
***
***  Revision 1.35  1999/09/30 09:27:46  Henri
***  Added PORT_BUSY.
***
***  Revision 1.34  1999/09/24 07:49:43  Werner
***  Added RUS_RTB_EXPIRED and RUS_SERIAL_MISMATCH
***  error codes.
***
***  Revision 1.33  1999/09/21 12:06:57  Henri
***  Arranged error codes.
***
***  Revision 1.32  1999/09/20 12:56:28  Werner
***  Added FORCE_ALF_CREATE constant.
***
***  Revision 1.31  1999/09/15 17:04:18  Henri
***  Changed WriteLicense.
***
***  Revision 1.30  1999/09/01 15:06:44  Adi
***  Added special handling of global expiration date.
***
***  Revision 1.29  1999/08/16 13:03:58  chris
***  restore previous structure packing after HL_API definition
***  (for MSVC)
***
***  Revision 1.28  1999/08/08 23:10:55  chris
***  added 2 bytes to reserved field: API structure was 2 bytes too short
***
***  Revision 1.27  1999/08/04 13:04:41  chris
***  API_FFS_GETRUSINFO define
***
***  Revision 1.26  1999/08/04 11:03:33  chris
***  API_FFS_WRITE_LIC definition and some more status codes
***
***  Revision 1.25  1999/08/03 20:36:15  chris
***  renamed FIB structure to RUS_FIB to avoid clash
***  with api_defs.h
***
***  Revision 1.24  1999/07/26 10:58:28  Henri
***  Added FIB structure.
***
***  Revision 1.23  1999/07/19 10:29:35  Henri
***  Renamed define for BUFFER_TOO_SMALL
***
***  Revision 1.22  1999/07/19 10:11:30  Henri
***  Added RUS functionality.
***
***  Revision 1.21  1998/10/21 15:56:53  Henri
***  Changed defines for Borland Builder.
***
***  Revision 1.20  1998/08/14 11:33:54  Henri
***  Changed driver comment.
***
***  Revision 1.19  1998/07/10 12:34:05  Henri
***  Added define for Borland Builder.
***
***  Revision 1.18  1998/06/29 09:01:36  Henri
***  Extended API struc.
***
***  Revision 1.17  1998/06/08 16:36:31  chris
***  fixed structure packing on gcc version 2.7 and above
***
***  Revision 1.16  1998/05/08 14:11:33  Henri
***  Added defines for HL_READID.
***
***  Revision 1.15  1998/04/07 13:14:59  chris
***  added API_READ_ID function code
***
***  Revision 1.14  1998/02/17 21:56:19  Henri
***  Added pragma pack(1) for Watcom 11/DOS
***
***  Revision 1.13  1997/07/01 13:56:54  henri
***  Fixed defines for LabView.
***
***  Revision 1.12  1997/04/28 15:30:53  chris
***  define UNIX32 ifdef __QNX__
***
***  Revision 1.11  1997/02/03 18:08:36  henri
***  Renamed error 17
***
***  Revision 1.10  1997/01/30 17:16:55  henri
***  Added LM return codes.
***
***  Revision 1.9  1997/01/28 08:23:30  henri
***  Missed a semicolon ;-)
***
***  Revision 1.8  1997/01/27 17:57:11  henri
***  Added slot number in API structure.
***
***  Revision 1.7  1997/01/16 18:18:11  henri
***  Added API_LMINIT function code.
***
***  Revision 1.6  1996/11/13 16:55:49  chris
***  added SOLARIS & UNIX32 define
***
***  Revision 1.5  1996/08/12 16:23:43  henri
***  Added VCS log.
***
**/
/****************************************************************************/

#if !defined(_FASTAPI_H_)
#define _FASTAPI_H_

#if defined(LINUX) || defined(SOLARIS) || defined(SCO) || defined(__QNX__) || defined(DARWIN) || defined(MACOSX)
#define UNIX32
#if defined(__alpha__) || defined(__ia64__)
#ifndef __64BIT__
#define __64BIT__
#endif
#define NO_UNALIGN
#endif
#endif

#ifdef __OS2__
#ifdef INTERNAL_16BITDLL
#define LOAD_DS
#else
#ifdef __WATCOMC__
#ifdef __386__      /* not the 16bit compiler */
#include <os2.h>
#endif
#else
#include <os2.h>
#endif
#endif
#ifdef OS_16
#define RET_        Word
#define FAR_        far pascal
#define DATAFAR_    far
#else
#define RET_        APIRET
#define FAR_
#define CALL_       APIENTRY
#define DATAFAR_
#endif
#pragma pack(2)
#endif

#ifdef UNIX32
#define __386__
#define pascal
#pragma pack(1)
#endif

#ifdef __GNUC__
#define __386__
#if !defined(__CYGWIN__) && !defined(__MINGW32__)
#define pascal
#endif
#if ((__GNUC__==2) && (__GNUC_MINOR__>=7)) || (__GNUC__>2)
#define ALIGN_GCC __attribute__ ((__packed__))
#ifdef NO_UNALIGN
#define AS_ALIGN __attribute__ ((__aligned__(8)))
#endif
#else
#pragma pack(1)
#endif
#endif

#ifdef _MSC_VER
#if _MSC_VER >= 900
#pragma pack(push,_fastapi_h_,1)
#else
#pragma pack(1)
#endif
#endif

#ifdef __BORLANDC__
#pragma pack(1)
#endif

#if defined(WINNT) || defined(__WIN32__) || defined(_WIN32)
#if !defined(_WIN64) && !defined(WIN64)
#ifndef __386__       /* Watcom doesnt like it */
#define __386__
#endif
#endif
#ifdef DLL
#define CALL_ __stdcall
#else
#define CALL_
#endif
#endif

#if defined(_WIN64) || defined(WIN64)
#ifndef __64BIT__
#define __64BIT__
#endif
#define DATAFAR_
#define FAR_
#define pascal __stdcall
#endif

#ifdef DOS386           /* Symantec C            */
#define __386__
#pragma pack(2)
#endif

#ifdef __HIGHC__        /* Metaware High C       */
#define __386__
#define _PACKED _Packed
#endif

#ifdef __ZTC__          /* Zortech C             */
#define __386__
#endif

#ifdef SALFORD          /* Salford C             */
#define ALIGN_ 8
#endif

#ifdef __WATCOMC__
#pragma pack(1)
#ifndef __386__
#ifndef OS_16
#define CALL_ cdecl
#endif
#endif
#endif

#ifdef _CVI_            /* LabWindows/CVI        */
#define RET_     Word
#ifndef _NI_mswin32_
#define CALL_    pascal
#else                 /* No pascal in WIN32-Version of LabWindows/CVI 4.0.1 */
#define CALL_    _stdcall
#endif
#ifndef __386__       /* __386__ defined by LabWindows/CVI */
#define FAR_     far
#define DATAFAR_ far
#endif
#endif

#ifdef __386__
#define DATAFAR_
#define FAR_
#endif

#ifdef HLHIGH_DLL
#define CALL_ pascal _export
#endif

#ifdef LOAD_DS
#define CALL_ _loadds
#endif

#ifndef CALL_
#define CALL_
#endif

#ifndef _PACKED
#define _PACKED
#endif

#ifndef ALIGN_GCC
#  define ALIGN_GCC
#endif

#ifndef DATAFAR_
#define DATAFAR_ far
#endif

#ifndef FAR_
#define FAR_ far
#endif

#ifndef RET_
#define RET_ Word
#endif

#ifndef ALIGN_
#define ALIGN_
#endif

#ifndef AS_ALIGN
#define AS_ALIGN
#endif

/* -------------------------------- */
/* Definitions and API structures : */
/* -------------------------------- */
#ifdef __64BIT__
typedef unsigned int  Long;
#if !defined(_WIN64) && !defined(WIN64)
typedef unsigned long Int64;
#else
typedef unsigned __int64 Int64;   /* stupid Windows convention */
#endif
#else
typedef unsigned long Long;
#endif
#ifndef __BCPLUSPLUS__
typedef unsigned char  Byte;
typedef unsigned short Word;
#else
#ifndef VCL_H
typedef unsigned char  Byte;
typedef unsigned short Word;
#endif
#endif
#ifndef __64BIT__
#define set_data_ptr(api,buf) (api)->Data=(Byte DATAFAR_ *)(buf)
#define get_data_ptr(api) ((void *)((api)->Data))
#else  /* above macros for <=32 bit, below macros for >32 bit */
#define set_data_ptr(api,buf) do { (api)->Data=(((Long)(buf)) & 0xffffffffu); \
                                     (api)->DataHigh=(((Long)(((Int64)(buf))>>32)) \
                                                               & 0xffffffffu);} while (0)
#define get_data_ptr(api) ((void *)((Int64)((api)->Data) | \
                              (((Int64)((api)->DataHigh))<<32)))
#endif

typedef struct
{
    Word Use_Key;
    Byte Key[8];
} ALIGN_GCC DES_MODE;

typedef struct
{
    Word ModAd;                           /* Hardlock module address */
    Word Reg;                             /* Memory register adress  */
    Word Value;                           /* Memory value            */
    Byte Reserved[4];
} ALIGN_GCC EYE_MODE;

typedef struct
{
    Long PW1;                             /* HASP passwords */
    Long PW2;
    Word P1;
} ALIGN_GCC HASP_MODE;

typedef struct
{
    Word LT_Reserved;
    Word Reg;                             /* Memory register adress       */
    Word Value;                           /* Memory value                 */
    Word Password[2];                     /* Access passwords             */
} ALIGN_GCC LT_MODE;

typedef union
{
    DES_MODE  Des;
    EYE_MODE  Eye;
    LT_MODE   Lt;
    HASP_MODE Hasp;
} HARDWARE;

typedef struct
{
    Word P2;
    Word P3;
} ALIGN_GCC HASP_MODE2;

typedef union
{
    HASP_MODE2 Hasp2;
} HARDWARE2;

typedef struct rus_fib
{
    Byte MARKER[2];
    Long SERIAL_ID;
    Byte VERSION[2];
    Word FIXED;
    Word VAR;
    Word CRC;
} ALIGN_GCC RUS_FIB;

typedef _PACKED struct ALIGN_ hl_api
{
    Byte          API_Version_ID[2];      /* Version                    */
    Word          API_Options[2];         /* API Optionflags            */
    Word          ModID;                  /* Modul-ID (EYE = 0...)      */
    HARDWARE      Module;                 /* Hardware type              */

#ifdef __OS2__                          /* Pointer to cipher data     */
    #ifdef OS_16
    void far* Data;
#else
#ifdef __BORLANDC__
    void FAR16PTR Data;
#else
    void* _Seg16 Data;
#endif
#endif
#else
#ifndef __64BIT__
    unsigned int Data;
#else
    Long         Data;                   /* low part only               */
#endif
#endif

    Word          Bcnt;                   /* Number of blocks            */
    Word          Function;               /* Function number             */
    Word          Status;                 /* Actual status               */
    Word          Remote;                 /* Remote or local??           */
    Word          Port;                   /* Port address if local       */
    Word          Speed;                  /* Speed of port if local      */
    Word          NetUsers;               /* Current Logins (HL-Server)  */
    Byte          ID_Ref[8];              /* Referencestring             */
    Byte          ID_Verify[8];           /* Encrypted ID_Ref            */
    Long          Task_ID;                /* Multitasking program ID     */
    Word          MaxUsers;               /* Maximum Logins (HL-Server)  */
    Long          Timeout;                /* Login Timeout in minutes    */
    Word          ShortLife;              /* (multiple use)              */
    Word          Application;            /* Application number          */
    Word          Protocol;               /* Protocol flags              */
    Word          PM_Host;                /* DOS Extender type           */
    Long          OSspecific;             /* ptr to OS specific data     */
    Word          PortMask;               /* Default local search (in)   */
    Word          PortFlags;              /* Default local search (out)  */
    Word          EnvMask;                /* Use env string search (in)  */
    Word          EnvFlags;               /* Use env string search (out) */
    Byte          EEFlags;                /* EE type flags               */
    Word          Prot4Info;              /* (internal use)              */
    Byte          FuncOptions;            /* Enable add. functionality   */
    Word          Slot_ID;                /* Licence slot number         */
    Word          Slot_ID_HIGH;           /* Licence slot High value     */
    Word          RUS_ExpDate;            /* RUS Expiration date         */
    Long          DataHigh;               /* Pointer to data high value  */
#ifndef __64BIT__
    unsigned int VendorKey;             /* Pointer to RUS vendor key   */
#else
    Long          VendorKey;              /* dto. */
#endif
    Long          VendorKeyHigh;          /* Vendor key high value       */
    Long          OSspecificHigh;         /* ptr to OS specific data     */
    Long          RUS_MaxInfo;            /* RUS max user/counter        */
    Long          RUS_CurInfo;            /* RUS current user/counter    */
    RUS_FIB       RUS_Fib;                /* RUS FIB structure           */
    HARDWARE2     Module2;                /* 2nd hw dependend fields     */
    Byte          Reserved2[52];         /* Reserved area               */
    Word          CryptVersion;         /* Denotes Packet Encryption Version   */
    Word          CryptSeed;         /* Denotes Packet Encryption Seed      */
    Byte          Reserved3[64];         /* Reserved Area      */
    Byte          Tail[2];         /* Packet Tail */
} ALIGN_GCC AS_ALIGN HL_API, LT_API, HS_API;

typedef _PACKED struct ALIGN_ {  /* HL_LIS slot information */
    Long max_user;
    Long cur_user;
    Word exp_date;
    Byte flag;      /* singularity flag */
    Byte res;       /* filler to make structure size multiple of 4 bytes */
} ALIGN_GCC HL_SIS;

/* License Information Structure (HL_LIS) */
typedef _PACKED struct ALIGN_ {
    Word current_date;
    Word res;
    Long num_slots;
    Word glob_exp_date;
    Word res2;      /* filler to make size multiple of 4 bytes */
#ifdef __AKS_QT_APPLICATION__
    HL_SIS slot[1]; /* slots is a keyword in Qt application, renamed array */
#else
    HL_SIS slots[1];
#endif
} ALIGN_GCC HL_LIS;

#ifdef UNIX32
#pragma pack()
#endif

#ifdef __OS2__
#pragma pack()
#endif

#ifdef __BORLANDC__
#pragma pack(1)
#endif

#ifdef _MSC_VER
#if _MSC_VER >= 900
#pragma pack(pop,_fastapi_h_)
#else
#pragma pack()
#endif
#endif


/* ------------- */
/* Module-ID's : */
/* ------------- */
#define EYE_DONGLE       0              /* Hardlock E-Y-E             */
#define DES_DONGLE       1              /* FAST DES                   */
#define LT_DONGLE        3              /* Hardlock LT                */
#define HASP_DONGLE      4              /* HASP                       */

/* --------------------- */
/* API function calls  : */
/* --------------------- */
#define API_INIT            0           /* Init API structure          */
#define API_DOWN            1           /* Free API structure          */
#define API_FORCE_DOWN      31          /* Force deinintialization     */
#define API_MULTI_SHELL_ON  2           /* MTS is enabled              */
#define API_MULTI_SHELL_OFF 3           /* MTS is disabled             */
#define API_MULTI_ON        4           /* Enable MTS                  */
#define API_MULTI_OFF       5           /* Disable  MTS                */
#define API_AVAIL           6           /* Dongle available?           */
#define API_LOGIN           7           /* Login dongle server         */
#define API_LOGOUT          8           /* Logout dongle server        */
#define API_INFO            9           /* Get API informations        */
#define API_CRYPT           14          /* Undoc'd Crypto Function     */
#define API_CODE            17          /* Migrated API_KEYE Command   */
#define API_GET_TASKID      32          /* Get TaskID from API         */
#define API_LOGIN_INFO      34          /* Get API Login informations  */

/* --------------------------- */
/* Data and memory functions : */
/* --------------------------- */
#define API_KEYE             11         /* Use KEYE for encryption         */
#define API_READ             20         /* Read one word of dongle EEPROM  */
#define API_WRITE            21         /* Write one word of dongle EEPROM */
#define API_READ_BLOCK       23         /* Read EEPROM in one block        */
#define API_WRITE_BLOCK      24         /* Write EEPROM in one block       */
#define API_READ_ID          29         /* Read USB ID memory              */
#define API_ABORT            51         /* Critical Error Abort            */

/* -------------- */
/* LM functions : */
/* -------------- */
#define API_LMINIT           40         /* LM compatible API_INIT replacement               */
#define API_LMPING           41         /* checks if LM dongle and slot is available        */
#define API_LMINFO           42         /* info about currently used LIMA                   */

#define API_READ_HLS         78         /* get number of licences for USB server HL         */
#define API_CALC_HLS         79         /* calculate num of licenses for parallel server HL */


#define API_GETHLSADDR         108      /* get addr struc of currently used HLS          */
#define API_GETHLSTEXT         109      /* get text addr of currently used HLS           */


/* --------------- */
/* RUS functions : */
/* --------------- */
#define API_FFS_INIT           256      /* RUS init function, downed with API_DOWN  */
#define API_FFS_ISRUSHL        257      /* Is RUS HL ?                              */
#define API_FFS_LOGIN          258      /* RUS Login to Hardlock server             */
#define API_FFS_CHECK_LIC      259      /* RUS Create LIS                           */
#define API_FFS_READ_LICBLOCK  260      /* RUS Read LIC Block                       */
#define API_FFS_QUERY_SLOT     261      /* RUS query slot function                  */
#define API_FFS_FREE_SLOT      262      /* RUS free slot                            */
#define API_FFS_OCCUPY_SLOT    263      /* RUS occupies a slot                      */
#define API_FFS_INC_CNTR       264      /* RUS counter increment                    */
#define API_FFS_PARSERTB       265      /* RUS Parse RTB                            */
#define API_FFS_GET_HWDEP_INFO 266      /* RUS get hardware dependent information   */
#define API_FFS_WRITE_LIC      267      /* RUS write updated license information    */
#define API_FFS_GETRUSINFO     269      /* get RUS info                             */

/* -------------------- */
/* Dongle access mode : */
/* -------------------- */
#define LOCAL_DEVICE    1               /* Query local HL only         */
#define NET_DEVICE      2               /* Query remote HL only        */
#define DONT_CARE       3               /* Query local or remote HL    */

/* -------------------- */
/* EnvMask/Port Flags : */
/* -------------------- */
#define USB_DEVICE         256          /* Port flag for USB use       */
#define IGNORE_ENVIRONMENT 0x8000       /* Ignore HL_SEARCH            */
#define EEF_NOAUTOUSB      8            /* No automatic USB search     */

/* ---------- */
/* RUS flags: */
/* ---------- */
#define FORCE_RUS        1              /* Enable RUS init without VK   */
#define DISABLE_TS_CHECK 2              /* Disable Terminal Server Detection */
#define FORCE_ALF_CREATE 1              /* Force creation of ALF file in HLM_WRITELICENSE */

/* ------------------ */
/* API PM_Host ID's : */
/* ------------------ */
#define API_XTD_DETECT    0
#define API_XTD_DPMI      1             /* QDPMI, Borland, Windows ... */
#define API_XTD_PHAR386   2
#define API_XTD_PHAR286   3
#define API_XTD_CODEBLDR  4             /* Intel Code Builder          */
#define API_XTD_COBOLXM   5

/* ------------------ */
/* API Status Codes : */
/* ------------------ */
#define STATUS_OK                 0     /* API call was succesfull                */
#define NOT_INIT                  1     /* DONGLE not initialized                 */
#define ALREADY_INIT              2     /* Already initialized                    */
#define UNKNOWN_DONGLE            3     /* Device not supported                   */
#define UNKNOWN_FUNCTION          4     /* Function not supported                 */
#define HLS_FULL                  6     /* HL-Server login table full             */
#define NO_DONGLE                 7     /* No device available                    */
#define NETWORK_ERROR             8     /* A network error occured                */
#define NO_ACCESS                 9     /* No device available                    */
#define INVALID_PARAM            10     /* A wrong parameter occured              */
#define VERSION_MISMATCH         11     /* HL-Server not API version              */
#define DOS_ALLOC_ERROR          12     /* Error on memory allocation             */
#define CANNOT_OPEN_DRIVER       14     /* Can not open Hardlock driver           */
#define INVALID_ENV              15     /* Invalid environment string             */
#define DYNALINK_FAILED          16     /* Unable to get a function entry         */
#define INVALID_LIC              17     /* No valid licence info (LM)             */
#define NO_LICENSE               18     /* Slot/licence not enabled (LM)          */
#define PORT_BUSY                19     /* Cannot acquire port                    */
#define RUS_NO_DEVICE            20     /* Key is no Hardlock RUS key             */
#define RUS_INVALID_LIC          21     /* Invalid RUS license                    */
#define RUS_SYNC_ERR             22     /* FIB in key and api struc mismatch      */
#define NOT_IMPLEMENTED          23     /* not (yet) implemented                  */
#define BUFFER_TOO_SMALL         24     /* Buffer for function too small          */
#define UNKNOWN_HW_TYPE          25     /* unknown hardware descriptor            */
#define RUS_INV_FBPOS            26     /* unknown fixed block position           */
#define RUS_INVALID_SLOT         27     /* Non-existing slot number given         */
#define RUS_DATE_FAKE            28     /* RUS Date fake detected                 */
#define RUS_COUNT_DOWN           29     /* RUS dead counter limit reached         */
#define RUS_INVALID_VK           30     /* RUS Vendor key is invalid              */
#define RUS_NO_LIC_FILE          31     /* RUS License file not found             */
#define RUS_INV_VBLOCK           32     /* RUS invalid variable block             */
#define RUS_LIC_FILE_WRITE_ERR   33     /* error writing (updated) license file   */
#define RUS_NO_INFO_AVAILABLE    34     /* GET_HWDEP_INFO: no info there          */
#define RUS_INFO_PACK_ERR        35     /*    "  "  "  " : cannot TLV encode data */
#define RUS_LIC_WRITE_ERR        36     /* write license failed                   */
#define RUS_DATE_EXPIRED         37     /* RUS Expiration Date reached.           */
#define TS_DETECTED              38     /* Term. Server / Citrix Winframe detected*/
#define RUS_INVALID_RTB          39     /* Invalid updated data (RTB)             */
#define RUS_RTB_EXPIRED          40     /* Update data (RTB) has expired.         */
#define RUS_SERIAL_MISMATCH      41     /* Update data serial does not match      */
#define NO_REMOTE_FUNCTION       60     /* function is available locally only     */
#define NO_LOCAL_FUNCTION        61     /* function is available remotely only    */

#define TOO_MANY_USERS          256     /* Login table full (remote)              */
#define SELECT_DOWN             257     /* Printer not On-line                    */
#define NO_SERIALID             258     /* Serial ID not readable or n/a          */

#endif /*_FASTAPI_H_*/
/* eof */

