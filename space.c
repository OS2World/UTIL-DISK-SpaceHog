/*---------------------------------------------------------------------------*/
/* File: SPACE.C                                          Date: 01/20/2000   */
/*---------------------------------------------------------------------------*/
/* Description: This PUBLIC DOMAIN program helps you maintain the amount     */
/*              of free space in the specified drive to under 2G-512 bytes   */
/*              Thus stopping programs using 32-bit signed integer math      */
/*              from seeing space above 2Gb as negative.                     */
/*                                                                           */
/*              See Welcome() for details.                                   */
/* Assumptions:                                                              */
/*                                                                           */
/* Programmers: Wing F Yuen, wfyuen@bestweb.net                              */
/*                                                                           */
/* Compiler   : IBM VisualAge C++ compiles with:                             */
/*                icc space.c                                                */
/*                                                                           */
/*              Microsoft C6 compiles with:                                  */
/*                cl /AS /Lp space.c os2.lib /link /pm:vio                   */
/*                                                                           */
/*              Watcom C/C++ v11.0 compiles with:                            */
/*                wcl386 -3 space.c                                          */
/*                wcl -i=\watcom\h\os21x -x -2 -lp -bt=os2 space.c           */
/*                                                                           */
/*              EMX/GCC v2.7.2.1 compiles (minimal testing) with:            */
/*                gcc space.c                                                */
/*                                                                           */
/*              A batch file BUILD.CMD is provided for your convenience.     */
/*                                                                           */
/* Notes      : All local   variables begin with a Capital letter            */
/*              All global  variables are prefixed with the letter 'g'       */
/*              All pointer variables are prefixed with the letter 'p'       */
/*              All user-supplied functions begin with a Capital letter;     */
/*                the only exception is main()                               */
/*---------------------------------------------------------------------------*/
/* Change Log:                                                               */
/* 1.00  - First release.                                                    */
/* 1.01  - ArgV[0] doesn't include the path of the EXE except under 4OS2.    */
/*       - Added support for Watcom C/C++ v11                                */
/* 1.02  - Clean up conditional compile preprocessor directives.             */
/*       - Watcom version not reading Drive Serial Number properly.          */
/*       - Use double to track freespace and file sizes, D-U-H !!!           */
/* 1.03  - Change Advantis email address                                     */
/* 1.04  - Keep maximum free space to 2G-2 bytes. Seem to compile okay       */
/*         with EMX/GCC v2.7.2.1 without any change!                         */
/* 1.05  - Keep maximum free space to 2G-512 bytes.                          */
/* 1.06  - Fixed space calculation for partitions above 4GB.                 */
/* 1.06a - Changed email address to wfyuen@bestweb.net                       */
/* 1.07  - Display file system type and/or LAN alias                         */
/*---------------------------------------------------------------------------*/
#define NDEBUG                                   /* define for production    */

#ifdef NDEBUG
  #define MAX_BYTES_FREE (0x7FFFFE00*1.0)        /* 2G - 512                 */
  #define MAX_FILE_SIZE  (0x7FFFFFFF*1.0)        /* 2G - 1                   */
#else
  #define MAX_BYTES_FREE (0x20000000*1.0)        /* 0.5G for testing         */
  #define MAX_FILE_SIZE  (100.0*1024*1024)       /* 100 MB for testing       */
#endif

#define INCL_DOSERRORS                           /* Error codes              */
#define INCL_DOSFILEMGR                          /* File Manager values      */
#include <os2.h>
#include <stdio.h>                               /* fileno()                 */
#include <ctype.h>                               /* toupper()                */
#include <string.h>
#include <io.h>                                  /* filelength()             */
#include <errno.h>                               /* errno                    */

/*---------------------------------------------------------------------------*/
/* Identify the compiler                                                     */
/*                                                                           */
/* Microsoft C                #ifdef  _MSC_VER                               */
/* IBM Visuage C++            #ifdef  __IBMC__                               */
/* Watcom C/C++               #ifdef  __WATCOMC__                            */
/* Watcom C/C++ 32bit         #ifdef  __386__         in addition to above   */
/*---------------------------------------------------------------------------*/
#if defined(__WATCOMC__)
  #if defined(__386__)
    #define WC32_
  #else
    #define WC16_
  #endif
#endif

/*---------------------------------------------------------------------------*/
/* Global variables are used extensively to reduce the stack size and make   */
/* it cheaper to separate code blocks into functions. This is okay because   */
/* this is not a multithreading program.                                     */
/*---------------------------------------------------------------------------*/
#if (defined(_MSC_VER) || defined(WC16_))

static USHORT     gDriveNumber=0;                /* Drive number             */
static USHORT     gFSInfoLevel;                  /* File system data required*/
static USHORT     gRcApi;
#else

static ULONG      gDriveNumber=0;                /* Drive number             */
static ULONG      gFSInfoLevel;                  /* File system data required*/
static APIRET     gRcApi;                        /* Return code              */
#endif

static FSALLOCATE gFSInfoBuf;                    /* File system info buffer  */
static FSINFO     gFSVolBuf;
static ULONG      gCluster;
static FILE       *gHog;
static double     gTotal, gUsed, gFree;
static double     gOldHogSize, gNewHogSize;      /* old and new sizes        */
static USHORT     gOldHogCount;                  /* old hog file count       */
static char       gDriveVolume[256];
static char       gDriveSerial[10];
static char       gDriveFormat[8];               /* FAT, HPFS, or LAN        */
static char       gDriveLetter;

/*---------------------------------------------------------------------------*/
/* I reserve 256 bytes here so that those without compilers can move the     */
/* holding directory by patching here with a hex editor. It must *NOT*       */
/* include a trailing backslash.                                             */
/*---------------------------------------------------------------------------*/
static char       gHogFilePath[256] = {"?:\\spacehog"};  /* holding directory*/

/*---------------------------------------------------------------------------*/
/* Local function prototypes                                                 */
/*---------------------------------------------------------------------------*/
int    CollectFileInfo  (void);
void   CollectVolInfo   (void);
long   FileSize         (const char *FileSpec);
void   QueryFileSystem  (char driveLetter);
char   *Split000        (double Number, char Separator);
double TotalHogSize     (void);
void   Welcome          (char *MyName);
int    WriteNewHogFiles (void);

/*---------------------------------------------------------------------------*/
/* main()                                                                    */
/*---------------------------------------------------------------------------*/
int main (int ArgC, char *ArgV[])
{
  ULONG      DriveMap;
  FILESTATUS PathBuf;

  if (ArgC < 2) {
      #if (defined(_MSC_VER) || defined(WC16_))

      gRcApi = DosQCurDisk( &gDriveNumber, &DriveMap);
      #else

      gRcApi = DosQueryCurrentDisk( &gDriveNumber, &DriveMap);
      #endif
      if (gRcApi)
           gDriveLetter = '\0';
      else gDriveLetter = 'A' + gDriveNumber - 1;
      gDriveNumber = 0L;                         /* current partition        */
  }
  else if (!strcmp( "/?", ArgV[1])) {
      Welcome( ArgV[0]);
      return 0;
  }                                              /* End if                   */
  else {
      /*---------------------------------------------------------------------*/
      /* Assume the user entered a drive letter                              */
      /*---------------------------------------------------------------------*/
      gDriveLetter = toupper( *ArgV[1]);
      if (NULL == strchr( "ABCDEFGHIJKLMNOPQRSTUVWXYZ", gDriveLetter)) {
          Welcome( ArgV[0]);
          return 0;
      }
      gDriveNumber = (ULONG)(gDriveLetter - 'A' + 1);
  }                                              /* End if                   */

  if (CollectFileInfo()) {
      return gRcApi;
  }
  gCluster = gFSInfoBuf.cSectorUnit * gFSInfoBuf.cbSector;
  gFree    = (gFSInfoBuf.cUnitAvail  * 1.0) * gCluster;

  gHogFilePath[0] = gDriveLetter;
  /*-------------------------------------------------------------------------*/
  /* Did the user create the \SPACEHOG path?                                 */
  /*-------------------------------------------------------------------------*/
  #if (defined(_MSC_VER) || defined(WC16_))

  gRcApi = DosQPathInfo( gHogFilePath,
                         FIL_STANDARD,
                         (PBYTE)&PathBuf,
                         sizeof PathBuf,
                         0L);                    /* reserved                 */
  #else

  gRcApi = DosQueryPathInfo( gHogFilePath,
                             FIL_STANDARD,
                             &PathBuf,
                             sizeof PathBuf);
  #endif

  if (NO_ERROR == gRcApi) {
      gOldHogSize = TotalHogSize();
      /*---------------------------------------------------------------------*/
      /* The actual amount of free disk space is (gFree + gOldHogSize)       */
      /*---------------------------------------------------------------------*/
      gNewHogSize = (gOldHogSize + gFree) - MAX_BYTES_FREE;
      if (gNewHogSize < 0.0) {
          gNewHogSize = 0.0;
      }
      #ifndef NDEBUG

      printf( "OldHogSize     = %.0f\n", gOldHogSize);
      printf( "NewHogSize     = %.0f\n", gNewHogSize);
      printf( "Free           = %.0f\n", gFree      );
      printf( "MAX_BYTES_FREE = %.0f\n", MAX_BYTES_FREE);
      #endif

      if (gNewHogSize != gOldHogSize) {
          /*-----------------------------------------------------------------*/
          /* Resize HogFileName                                              */
          /*-----------------------------------------------------------------*/
          if (WriteNewHogFiles()) {
              printf( __FILE__"/main(): Can't resize holding " \
                      "files in %s\n", gHogFilePath);
          }
      }
  }
  /*-------------------------------------------------------------------------*/
  /* Refresh the space usage                                                 */
  /*-------------------------------------------------------------------------*/
  if (CollectFileInfo()) {
      return gRcApi;
  }
  gFree  = (gFSInfoBuf.cUnitAvail * 1.0) * gCluster;
  gTotal = (gFSInfoBuf.cUnit      * 1.0) * gCluster;
  gUsed  = gTotal - gFree;

  CollectVolInfo();

  QueryFileSystem( gDriveLetter);

  printf( "\n SpaceHog v1.07 by Wing Yuen                            " \
          "compiled on %s", __DATE__);
  printf( "\n %s Volume in drive %c is %-11s\n Serial number is %s",
          gDriveFormat, gDriveLetter, gDriveVolume, gDriveSerial);
  printf( "\n --------------------------------------------------------------" \
          "----------------");
  printf( "\n%16s bytes per sector",       Split000( gFSInfoBuf.cbSector, ','));
  printf( "\n%16s bytes per cluster",      Split000( gCluster,            ','));
  printf( "\n%16s bytes total disk space", Split000( gTotal,              ','));
  printf( "\n%16s bytes used",             Split000( gUsed ,              ','));
  if (0.0 != gNewHogSize) {
      printf( "\n*%15s bytes reserved in %s directory",
              Split000( gNewHogSize, ','), gHogFilePath);
  }
  printf( "\n%16s bytes free\n",           Split000( gFree ,              ','));

  return 0;
}                                                /* main()                   */

/*---------------------------------------------------------------------------*/
/* CollectFileInfo()                                      Date: 03/15/1999   */
/*---------------------------------------------------------------------------*/
/* Description: Collect file information of gDriveNumber                     */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
int CollectFileInfo (void)
{
  gFSInfoLevel = FSIL_ALLOC;          /* requests file system allocation info*/
  #if (defined(_MSC_VER) || defined(WC16_))

  gRcApi = DosQFSInfo( gDriveNumber,
                       gFSInfoLevel,
                       (PBYTE)&gFSInfoBuf,
                       sizeof gFSInfoBuf);
  if (gRcApi) {
      printf( "DosQFSInfo error: Rc%ld", gRcApi);
      return 2;
  }
  #else

  gRcApi = DosQueryFSInfo( gDriveNumber,
                           gFSInfoLevel,
                           &gFSInfoBuf,
                           sizeof gFSInfoBuf);
  if (gRcApi) {
      printf( "DosQueryFSInfo error: Rc%ld", gRcApi);
      return 2;
  }
  #endif

  /*-------------------------------------------------------------------------*/
  /* On successful return, the data buffer gFSInfoBuf contains a set of      */
  /* information about space allocation within the specified file system.    */
  /*-------------------------------------------------------------------------*/
  #if 0

  printf( "\nCollectFileInfo():\n");
  printf( "  gFSInfoBuf.cSectorUnit  = %lu\n", gFSInfoBuf.cSectorUnit );
  printf( "  gFSInfoBuf.cUnit        = %lu\n", gFSInfoBuf.cUnit       );
  printf( "  gFSInfoBuf.cUnitAvail   = %lu\n", gFSInfoBuf.cUnitAvail  );
  printf( "  gFSInfoBuf.cbSector     = %hu\n", gFSInfoBuf.cbSector    );
  #endif
  return 0;
}                                                /* CollectFileInfo()        */

/*---------------------------------------------------------------------------*/
/* CollectVolInfo()                                       Date: 04/16/1998   */
/*---------------------------------------------------------------------------*/
/* Description: Collect volume information of gDriveNumber                   */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
void CollectVolInfo (void)
{
  #if (defined(_MSC_VER) || defined(WC16_))

  ULONG  VolSer;
  #else

  UINT   VolSer;
  #endif

  gFSInfoLevel = FSIL_VOLSER;           /* requests volume/serial number info*/
  #if (defined(_MSC_VER) || defined(WC16_))

  gRcApi = DosQFSInfo( gDriveNumber,
                       gFSInfoLevel,
                       (PBYTE)&gFSVolBuf,
                       sizeof gFSVolBuf);
  if (gRcApi) {
      printf( "DosQFSInfo error: Rc%ld", gRcApi);
      strcpy( gDriveSerial, "unknown");
      strcpy( gDriveVolume, "unknown");
      return;
  }

    #if defined(WC16_)                        /* see \watcom\h\os21x\bsedos.h*/

  VolSer = *((ULONG *)(&gFSVolBuf.fdateCreation));
    #else

  VolSer = *((ULONG *)(&gFSVolBuf.ulVSN));       /* Microsoft C              */
    #endif

  sprintf( gDriveSerial, "%04X:%04X",
           (USHORT)(VolSer >>16),
           (USHORT)(VolSer     ));
  #else                                          /* 32 bit API               */

  gRcApi = DosQueryFSInfo( gDriveNumber,
                           gFSInfoLevel,
                           &gFSVolBuf,
                           sizeof gFSVolBuf);

  if (gRcApi) {
      printf( "DosQueryFSInfo error: Rc%ld", gRcApi);
      strcpy( gDriveSerial, "unknown");
      strcpy( gDriveVolume, "unknown");
      return;
  }
  VolSer = *((UINT *)(&gFSVolBuf.fdateCreation));
  sprintf( gDriveSerial, "%04X:%04X",
           (USHORT)(VolSer >>16),
           (USHORT)(VolSer     ));
  #endif                             /* (defined(_MSC_VER) || defined(WC16_))*/

  /*-------------------------------------------------------------------------*/
  /* On successful return, the data buffer gFSVolBuf contains a set of       */
  /* information about the volume/serial number of the drive.                */
  /*-------------------------------------------------------------------------*/
  strcpy( gDriveVolume, gFSVolBuf.vol.szVolLabel);
  if ('\0' == gDriveVolume[0]) {
      strcpy( gDriveVolume, "unlabeled");
  }
  return;
}                                                /* CollectVolInfo()         */

/*---------------------------------------------------------------------------*/
/* FileSize()                                             Date: 04/15/1998   */
/*---------------------------------------------------------------------------*/
/* Description: Return the size of the specified file.                       */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
long FileSize (const char *FileSpec)
{
  FILE *Tmp;
  long FileLen;

  if (NULL == (Tmp = fopen( FileSpec, "rb"))) {
      return -1L;
  }
  #if (defined(_MSC_VER) || defined (__WATCOMC__))

  FileLen = filelength( fileno( Tmp));
  #else                                          /* VisualAge C++            */

  FileLen = _filelength( fileno( Tmp));          /* needs /Se                */

  #endif
  fclose( Tmp);
  return FileLen;
}                                                /* FileSize()               */

/*---------------------------------------------------------------------------*/
/* QueryFileSystem()                                      Date: 06/14/2000   */
/*---------------------------------------------------------------------------*/
/* Description: Store the name of the file system attached to the specified  */
/*              drive letter in gDriveFormat. If it is a LAN drive, its      */
/*              alias will be stored in gDriveVolumn.                        */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
void QueryFileSystem (char driveLetter)
{
  #if (defined(_MSC_VER) || defined(WC16_))

  UCHAR  szDeviceName[8];               /* Device name or drive letter string*/
  USHORT ulOrdinal       = 0;                /* Ordinal of entry in name list*/
  BYTE   *pszFSDName     = NULL;                 /* pointer to FS name       */
  BYTE   *prgFSAData     = NULL;                 /* pointer to FS data       */
  USHORT rcApi           = NO_ERROR;             /* Return code              */
  BYTE   *pByte;

  /*-------------------------------------------------------------------------*/
  /* Return-data buffer should be large enough to hold FSQBUFFER2 and the    */
  /* maximum data for szName, szFSDName, and rgFSAData. Typically, the data  */
  /* isn't that large.                                                       */
  /*-------------------------------------------------------------------------*/
  BYTE         fsqBuffer[sizeof(FSQBUFFER) + (3 * CCHMAXPATH)] = {0};
  USHORT       cbBuffer    = sizeof(fsqBuffer);  /* Buffer length)           */
  FSQBUFFER    *pfsqBuffer = (FSQBUFFER *)fsqBuffer;
  USHORT       cbFSDName;
  USHORT       cbFSAData;

  szDeviceName[0] = driveLetter;                 /* change drive letter      */
  szDeviceName[1] = ':';
  szDeviceName[2] = '\0';

  rcApi = DosQFSAttach(
              szDeviceName,                   /* Logical drive of attached FS*/
              ulOrdinal,                       /* ignored for FSAIL_QUERYNAME*/
              FSAIL_QUERYNAME,           /* Return data for a Drive or Device*/
              fsqBuffer,                         /* returned data            */
              &cbBuffer,                         /* returned data length     */
              0L
          );

  /*-------------------------------------------------------------------------*/
  /* On successful return, the fsqBuffer structure contains a set of         */
  /* information describing the specified attached file system and the       */
  /* DataBufferLen variable contains the size of information within the      */
  /* structure.                                                              */
  /*-------------------------------------------------------------------------*/
  if (NO_ERROR == rcApi) {
      /*---------------------------------------------------------------------*/
      /* The data for the variable fields in the FSQBUFFER structure are as  */
      /* follow:                                                             */
      /*                                                                     */
      /*   USHORT  iType;                                                    */
      /*   USHORT  cbName;                                                   */
      /*   UCHAR   szName[1];                                                */
      /*   USHORT  cbFSDName;                                                */
      /*   UCHAR   szFSDName[1];                                             */
      /*   USHORT  cbFSAData;                                                */
      /*   UCHAR   rgFSAData[1];                                             */
      /*                                                                     */
      /* They wised up an move the variable fields to the end of the         */
      /* structure in the 32 bit version of OS/2.                            */
      /*---------------------------------------------------------------------*/
      pByte      = pfsqBuffer->szName +
                   pfsqBuffer->cbName + 1;       /* ->cbFSDName              */
      cbFSDName  = *((USHORT *)pByte);

      pszFSDName = pByte + sizeof cbFSDName;     /* -> szFSDName             */

      pByte      = pszFSDName + cbFSDName + 1;   /* -> cbFSAData             */
      cbFSAData  = *((USHORT *)pByte);

      prgFSAData = pByte + sizeof cbFSAData;     /* ->rgFSAData              */

        #if 0

      printf("iType     = %hu\n", pfsqBuffer->iType);
      printf("szName    = %s\n",  pfsqBuffer->szName);
      printf("szFSDName = %s\n",  pszFSDName);
      printf("cbFSDName = %hu\n",  cbFSDName);
      printf("rgFSAData = %s\n",  prgFSAData);
      printf("cbFSAData = %hu\n",  cbFSAData);

      printf( "gDriveFormat            = <%s>\n", gDriveFormat);
        #endif

      strncpy( gDriveFormat, pszFSDName, sizeof gDriveFormat - 1);
      gDriveFormat[sizeof gDriveFormat - 1] = '\0';

      if (!strcmp( pszFSDName, "LAN")) {
          strncpy( gDriveVolume, prgFSAData, sizeof gDriveVolume - 1);
          gDriveVolume[sizeof gDriveVolume - 1] = '\0';
      }
  } else {
      printf("DosQFSAttach error: return code = %hu\n", rcApi);
  }
  return;
  #else

  UCHAR  szDeviceName[8];
  ULONG  ulOrdinal       = 0;
  PBYTE  pszFSDName      = NULL;
  PBYTE  prgFSAData      = NULL;
  APIRET rcApi           = NO_ERROR;

  BYTE         fsqBuffer[sizeof(FSQBUFFER2) + (3 * CCHMAXPATH)] = {0};
  ULONG        cbBuffer   = sizeof(fsqBuffer);        /* Buffer length) */
  PFSQBUFFER2  pfsqBuffer = (PFSQBUFFER2) fsqBuffer;

  szDeviceName[0] = driveLetter;                 /* change drive letter      */
  szDeviceName[1] = ':';
  szDeviceName[2] = '\0';

  rcApi = DosQueryFSAttach(
              szDeviceName,                   /* Logical drive of attached FS*/
              ulOrdinal,                       /* ignored for FSAIL_QUERYNAME*/
              FSAIL_QUERYNAME,           /* Return data for a Drive or Device*/
              pfsqBuffer,                        /* returned data            */
              &cbBuffer                          /* returned data length     */
          );

  /*-------------------------------------------------------------------------*/
  /* On successful return, the fsqBuffer structure contains a set of         */
  /* information describing the specified attached file system and the       */
  /* DataBufferLen variable contains the size of information within the      */
  /* structure.                                                              */
  /*-------------------------------------------------------------------------*/
  if (NO_ERROR == rcApi) {
      /*---------------------------------------------------------------------*/
      /* The data for the last three fields in the FSQBUFFER2 structure are  */
      /* stored at the offset of fsqBuffer.szName. Each data field following */
      /* fsqBuffer.szName begins immediately after the previous item.        */
      /*---------------------------------------------------------------------*/
      pszFSDName = pfsqBuffer->szName + pfsqBuffer->cbName + 1;
      prgFSAData = pszFSDName + pfsqBuffer->cbFSDName + 1;

      strncpy( gDriveFormat, pszFSDName, sizeof gDriveFormat - 1);
      gDriveFormat[sizeof gDriveFormat - 1] = '\0';

      if (!strcmp( pszFSDName, "LAN")) {
          strncpy( gDriveVolume, prgFSAData, sizeof gDriveVolume - 1);
          gDriveVolume[sizeof gDriveVolume - 1] = '\0';
      }
      #if 0

      printf("iType     = %d\n", pfsqBuffer->iType);
      printf("szName    = %s\n", pfsqBuffer->szName);
      printf("szFSDName = %s\n", pszFSDName);
      printf("rgFSAData = %s\n", prgFSAData);
      #endif

  } else {
      printf("DosQueryFSAttach error: return code = %u\n", rcApi);
  }
  return;
  #endif

}                                                /* QueryFileSystem()        */

/*---------------------------------------------------------------------------*/
/* Split000()                                             Date: 04/17/1998   */
/*---------------------------------------------------------------------------*/
/* Description: Add commas as separators between the thousands, millions &   */
/*              billions. This is accomplished by shifting trailing digits   */
/*              three at a time, then inserting a comma before repeating.    */
/* Returns    : Nothing                                                      */
/* Assumptions: Partition size < 1,000,000,000,000 bytes                     */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
char *Split000 (double Bytes, char Separator)
{
  int    CommaCnt, DigitCnt, i;
  static
  char   Buffer[16];                   /* HPFS 64GB limit needs only 14 bytes*/
  char   *Src, *Dst;

  sprintf( Buffer, "%.0f", Bytes);
  DigitCnt = strlen( Buffer);
  CommaCnt = (DigitCnt-1) / 3;
  Dst = Buffer + DigitCnt + CommaCnt;
  Src = Buffer + DigitCnt;
  *Dst = *Src;
  for (i = 0; i < CommaCnt; i++) {
      *--Dst = *--Src;
      *--Dst = *--Src;
      *--Dst = *--Src;
      *--Dst = Separator;
  }                                              /* End for                  */
  return Buffer;
}                                                /* Split000()               */

/*---------------------------------------------------------------------------*/
/* TotalHogSize()                                         Date: 04/17/1998   */
/*---------------------------------------------------------------------------*/
/* Description: Count the number of Hog Files and add up their total.        */
/* Returns    : Total size as a double, and updated gOldHogCount.            */
/* Assumptions:                                                              */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
double TotalHogSize (void)
{
  double Sum=0;
  long   HogSize;
  char   HogName[256];

  gOldHogCount = 0;
  do {
      sprintf( HogName, "%s\\%d", gHogFilePath, gOldHogCount);
      HogSize = FileSize( HogName);
      Sum += HogSize;
      ++gOldHogCount;
  } while (-1L != HogSize);                      /* End do                   */
  --gOldHogCount;
  return Sum + 1.0;                              /* undo final HogSize       */
}                                                /* TotalHogSize()           */

/*---------------------------------------------------------------------------*/
/* Welcome()                                              Date: 01/20/2000   */
/*---------------------------------------------------------------------------*/
/* Description: Show syntax and usage                                        */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
void Welcome (char *MyName)
{
  printf( "\nSyntax: %s [DriveLetter[:]]\n"                                   \
          "\nThe current drive will be used if no DriveLetter is given."      \
          "\nThe trailing colon is also optional.\n"                          \
          "\nThis PUBLIC DOMAIN program helps you maintain the amount"        \
          "\nof free space in the specified drive to under 2G-512 Bytes."     \
          "\n(Because HPFS allocates space in 512 byte units.)"               \
          "\nThus stopping programs using 32-bit signed integer math"         \
          "\nfrom seeing space above 2G-512 bytes as negative.\n"             \
          "\nIf it can't find the \\SPACEHOG directory in the specified"      \
          "\npartition, it behaves like a run-of-the-mill freespace"          \
          "\nreporting utility, albeit it can handle disk partitions up"      \
          "\nto 999,999,999,999 bytes in size. (see Split000())\n"            \
          "\nIf it sees the \\SPACEHOG directory, which you have to"          \
          "\ncreate explicitly, it will create dummy files there as"          \
          "\nneeded to maintain the 2G-512 byte freespace ceiling for"        \
          "\nthat disk partition (you may have to run this twice). The"       \
          "\ndummy files are named with simple integers, from 0 thru"         \
          "\n99999999...\n"                                                   \
          "\nIf the amount of reserved space needed is less than 2G-512"      \
          "\nbytes, it will shrink or expand the size of the \\SPACEHOG\\0"   \
          "\nfile instead.\n"                                                 \
          "\nWarning: You use this program and its source code at your"       \
          "\n         own risk. I provide no warranty of any kind. But"       \
          "\n         I *do* welcome bug FIXES/reports via email to:\n"       \
          "\n         wfyuen@bestweb.net\n"                                   \
          "\nPatches: If you don't have any of the four compilers I used,"    \
          "\n         you can change the \\SPACEHOG directory to some place"  \
          "\n         else by patching the 256 byte variable I reserved"      \
          "\n         with a binary editor. Search for this string:\n"        \
          "\n         \"?:\\spacehog\"\n"                                     \
          "\n         The '?' will be replaced by the program with the"       \
          "\n         drive letter passed from the command line. Therefore,"  \
          "\n         you need not patch that. Please make sure you end the"  \
          "\n         new directory name with a binary 0 byte. It must not"   \
          "\n         end with a backslash ('\\')!!!\n"                       \
          "\nWing Yuen                                      %s\n"
          , MyName, __DATE__);
  return;
}                                                /* Welcome()                */

/*---------------------------------------------------------------------------*/
/* WriteNewHogFiles()                                     Date: 06/25/1998   */
/*---------------------------------------------------------------------------*/
/* Description: Replace the Hog Files                                        */
/* Returns    : Nothing                                                      */
/* Assumptions: None                                                         */
/* Programmers: Wing F Yuen                                                  */
/*---------------------------------------------------------------------------*/
int WriteNewHogFiles (void)
{
  #if (defined(_MSC_VER) || defined(WC16_))

  USHORT     ActionTaken;
  #else                                          /* 32 bit API               */

  ULONG      ActionTaken;
  #endif
  HFILE      Hog;
  int        i, HogCount, NewHogCount=1;
  double     HogSize, Overhead;
  char       HogName[256];

  HogSize = gNewHogSize;
  #ifndef NDEBUG

  printf( "MaxFileSize = %.0f\n", MAX_FILE_SIZE);
  printf( "gNewHogSize = %.0f\n", gNewHogSize);
  #endif

  if (HogSize > MAX_FILE_SIZE) {
      do {
          HogSize = HogSize - MAX_FILE_SIZE;
          ++NewHogCount;
      } while (HogSize > MAX_FILE_SIZE);         /* End do                   */
  }

  /*-------------------------------------------------------------------------*/
  /* When gOldHogCount differs from NewHogCount, the first run of SpaceHog   */
  /* will not maximize the free disk space. I haven't figure this puzzle     */
  /* out yet. If you do, please email me.                                    */
  /*-------------------------------------------------------------------------*/

  #ifndef NDEBUG

  printf( "gOldHogCount = %d\n",   gOldHogCount);
  printf( "NewHogCount  = %d\n",   NewHogCount );
  #endif

  for (i = 0; i < NewHogCount; i++) {
      /*---------------------------------------------------------------------*/
      /* Rewriting the entire file with DosOpen() is much faster than        */
      /* adjusting its size with chsize().                                   */
      /*---------------------------------------------------------------------*/
      sprintf( HogName, "%s\\%d", gHogFilePath, i);
      #ifndef NDEBUG

      printf( "\nRewriting %s\n",  HogName );
      printf( "HogSize  = %.0f\n", HogSize );
      #endif
      gRcApi = DosOpen( HogName,
                        &Hog,
                        &ActionTaken,
                        (ULONG)HogSize,
                        FILE_NORMAL,
                        OPEN_ACTION_CREATE_IF_NEW |
                        OPEN_ACTION_REPLACE_IF_EXISTS,
                        OPEN_SHARE_DENYREADWRITE |
                        OPEN_ACCESS_WRITEONLY,
                        0L);
      if (gRcApi) {
          printf( __FILE__"/WriteNewHogFiles(): DosOpen() returns %u\n",
                  gRcApi);
          return 1;
      }

      gRcApi = DosClose( Hog);
      if (gRcApi) {
          printf( __FILE__"/WriteNewHogFiles(): DosClose() returns %u\n",
                  gRcApi);
          return 1;
      }
      HogSize = MAX_FILE_SIZE;
  }                                              /* End for                  */
  /*-------------------------------------------------------------------------*/
  /* Delete any un-needed old hog files                                      */
  /*-------------------------------------------------------------------------*/
  for (i = NewHogCount; i < gOldHogCount; i++) {
      sprintf( HogName, "%s\\%d", gHogFilePath, i);
      remove( HogName);
  }                                              /* End for                  */

  return 0;
}                                                /* WriteNewHogFiles()       */
