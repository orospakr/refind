/*
 * refind/screen.c
 * Screen handling functions
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modifications copyright (c) 2012-2014 Roderick W. Smith
 *
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), a copy of which must be distributed
 * with this source code or binaries made from it.
 *
 */

#include "global.h"
#include "screen.h"
#include "config.h"
#include "libegint.h"
#include "lib.h"
#include "menu.h"
#include "../include/refit_call_wrapper.h"

#include "../include/egemb_refind_banner.h"

// Console defines and variables

UINTN ConWidth;
UINTN ConHeight;
CHAR16 *BlankLine = NULL;

static VOID DrawScreenHeader(IN CHAR16 *Title);

// UGA defines and variables

UINTN UGAWidth;
UINTN UGAHeight;
BOOLEAN AllowGraphicsMode;

EG_PIXEL StdBackgroundPixel  = { 0xbf, 0xbf, 0xbf, 0 };
EG_PIXEL MenuBackgroundPixel = { 0xbf, 0xbf, 0xbf, 0 };
EG_PIXEL DarkBackgroundPixel = { 0x0, 0x0, 0x0, 0 };

static BOOLEAN GraphicsScreenDirty;

// general defines and variables

static BOOLEAN haveError = FALSE;

static VOID PrepareBlankLine(VOID) {
    UINTN i;

    MyFreePool(BlankLine);
    // make a buffer for a whole text line
    BlankLine = AllocatePool((ConWidth + 1) * sizeof(CHAR16));
    for (i = 0; i < ConWidth; i++)
        BlankLine[i] = ' ';
    BlankLine[i] = 0;
}

//
// Screen initialization and switching
//

VOID InitScreen(VOID)
{
    // initialize libeg
    egInitScreen();

    if (egHasGraphicsMode()) {
        egGetScreenSize(&UGAWidth, &UGAHeight);
        AllowGraphicsMode = TRUE;
    } else {
        AllowGraphicsMode = FALSE;
        egSetTextMode(GlobalConfig.RequestedTextMode);
        egSetGraphicsModeEnabled(FALSE);   // just to be sure we are in text mode
    }
    GraphicsScreenDirty = TRUE;

    // disable cursor
    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, FALSE);

    // get size of text console
    if (refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, ST->ConOut->Mode->Mode, &ConWidth, &ConHeight) != EFI_SUCCESS) {
        // use default values on error
        ConWidth = 80;
        ConHeight = 25;
    }

    PrepareBlankLine();

    // show the banner if in text mode
    if (GlobalConfig.TextOnly && (GlobalConfig.ScreensaverTime != -1))
       DrawScreenHeader(L"Initializing...");
}

// Set the screen resolution and mode (text vs. graphics).
VOID SetupScreen(VOID)
{
    UINTN NewWidth, NewHeight;

    // Convert mode number to horizontal & vertical resolution values
    if ((GlobalConfig.RequestedScreenWidth > 0) && (GlobalConfig.RequestedScreenHeight == 0))
       egGetResFromMode(&(GlobalConfig.RequestedScreenWidth), &(GlobalConfig.RequestedScreenHeight));

    // Set the believed-to-be current resolution to the LOWER of the current
    // believed-to-be resolution and the requested resolution. This is done to
    // enable setting a lower-than-default resolution.
    if ((GlobalConfig.RequestedScreenWidth > 0) && (GlobalConfig.RequestedScreenHeight > 0)) {
       UGAWidth = (UGAWidth < GlobalConfig.RequestedScreenWidth) ? UGAWidth : GlobalConfig.RequestedScreenWidth;
       UGAHeight = (UGAHeight < GlobalConfig.RequestedScreenHeight) ? UGAHeight : GlobalConfig.RequestedScreenHeight;
    }

    // Set text mode. If this requires increasing the size of the graphics mode, do so.
    if (egSetTextMode(GlobalConfig.RequestedTextMode)) {
       egGetScreenSize(&NewWidth, &NewHeight);
       if ((NewWidth > UGAWidth) || (NewHeight > UGAHeight)) {
          UGAWidth = NewWidth;
          UGAHeight = NewHeight;
       }
       if ((UGAWidth > GlobalConfig.RequestedScreenWidth) || (UGAHeight > GlobalConfig.RequestedScreenHeight)) {
           // Requested text mode forces us to use a bigger graphics mode
           GlobalConfig.RequestedScreenWidth = UGAWidth;
           GlobalConfig.RequestedScreenHeight = UGAHeight;
       } // if
    }

    if (GlobalConfig.RequestedScreenWidth > 0) {
       egSetScreenSize(&(GlobalConfig.RequestedScreenWidth), &(GlobalConfig.RequestedScreenHeight));
       egGetScreenSize(&UGAWidth, &UGAHeight);
    } // if user requested a particular screen resolution

    if (GlobalConfig.TextOnly) {
        // switch to text mode if requested
        AllowGraphicsMode = FALSE;
        SwitchToText(FALSE);

    } else if (AllowGraphicsMode) {
        // clear screen and show banner
        // (now we know we'll stay in graphics mode)
        SwitchToGraphics();
        if (GlobalConfig.ScreensaverTime != -1) {
           BltClearScreen(TRUE);
        } else { // start with screen blanked
           GraphicsScreenDirty = TRUE;
        }
    }
} // VOID SetupScreen()

VOID SwitchToText(IN BOOLEAN CursorEnabled)
{
    egSetGraphicsModeEnabled(FALSE);
    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, CursorEnabled);
    // get size of text console
    if (refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, ST->ConOut->Mode->Mode, &ConWidth, &ConHeight) != EFI_SUCCESS) {
        // use default values on error
        ConWidth = 80;
        ConHeight = 25;
    }
    PrepareBlankLine();
}

VOID SwitchToGraphics(VOID)
{
    if (AllowGraphicsMode && !egIsGraphicsModeEnabled()) {
        egSetGraphicsModeEnabled(TRUE);
        GraphicsScreenDirty = TRUE;
    }
}

//
// Screen control for running tools
//

VOID BeginTextScreen(IN CHAR16 *Title)
{
    DrawScreenHeader(Title);
    SwitchToText(FALSE);

    // reset error flag
    haveError = FALSE;
}

VOID FinishTextScreen(IN BOOLEAN WaitAlways)
{
    if (haveError || WaitAlways) {
       PauseForKey();
       SwitchToText(FALSE);
    }

    // reset error flag
    haveError = FALSE;
}

VOID BeginExternalScreen(IN BOOLEAN UseGraphicsMode, IN CHAR16 *Title)
{
    if (!AllowGraphicsMode)
        UseGraphicsMode = FALSE;

    if (UseGraphicsMode) {
        SwitchToGraphics();
        BltClearScreen(FALSE);
    } else {
        egClearScreen(&DarkBackgroundPixel);
        DrawScreenHeader(Title);
        SwitchToText(TRUE);
    }

    // reset error flag
    haveError = FALSE;
}

VOID FinishExternalScreen(VOID)
{
    // make sure we clean up later
    GraphicsScreenDirty = TRUE;

    if (haveError) {
        SwitchToText(FALSE);
        PauseForKey();
    }

    // Reset the screen resolution, in case external program changed it....
    SetupScreen();

    // reset error flag
    haveError = FALSE;
}

VOID TerminateScreen(VOID)
{
    // clear text screen
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call1_wrapper(ST->ConOut->ClearScreen, ST->ConOut);

    // enable cursor
    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, TRUE);
}

static VOID DrawScreenHeader(IN CHAR16 *Title)
{
    UINTN y;

    // clear to black background
    egClearScreen(&DarkBackgroundPixel); // first clear in graphics mode
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call1_wrapper(ST->ConOut->ClearScreen, ST->ConOut); // then clear in text mode

    // paint header background
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BANNER);
    for (y = 0; y < 3; y++) {
        refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, y);
        Print(BlankLine);
    }

    // print header text
    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, 1);
    Print(L"rEFInd - %s", Title);

    // reposition cursor
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, 4);
}

//
// Keyboard input
//

BOOLEAN ReadAllKeyStrokes(VOID)
{
    BOOLEAN       GotKeyStrokes;
    EFI_STATUS    Status;
    EFI_INPUT_KEY key;

    GotKeyStrokes = FALSE;
    for (;;) {
        Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
        if (Status == EFI_SUCCESS) {
            GotKeyStrokes = TRUE;
            continue;
        }
        break;
    }
    return GotKeyStrokes;
}

VOID PauseForKey(VOID)
{
    UINTN index;

    Print(L"\n* Hit any key to continue *");

    if (ReadAllKeyStrokes()) {  // remove buffered key strokes
        refit_call1_wrapper(BS->Stall, 5000000);     // 5 seconds delay
        ReadAllKeyStrokes();    // empty the buffer again
    }

    refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
    ReadAllKeyStrokes();        // empty the buffer to protect the menu

    Print(L"\n");
}

#if REFIT_DEBUG > 0
VOID DebugPause(VOID)
{
    // show console and wait for key
    SwitchToText(FALSE);
    PauseForKey();

    // reset error flag
    haveError = FALSE;
}
#endif

VOID EndlessIdleLoop(VOID)
{
    UINTN index;

    for (;;) {
        ReadAllKeyStrokes();
        refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
    }
}

//
// Error handling
//

#ifdef __MAKEWITH_GNUEFI
BOOLEAN CheckFatalError(IN EFI_STATUS Status, IN CHAR16 *where)
{
    CHAR16 ErrorName[64];

    if (!EFI_ERROR(Status))
        return FALSE;

    StatusToString(ErrorName, Status);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
    Print(L"Fatal Error: %s %s\n", ErrorName, where);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    haveError = TRUE;

    //BS->Exit(ImageHandle, ExitStatus, ExitDataSize, ExitData);

    return TRUE;
}

BOOLEAN CheckError(IN EFI_STATUS Status, IN CHAR16 *where)
{
    CHAR16 ErrorName[64];

    if (!EFI_ERROR(Status))
        return FALSE;

    StatusToString(ErrorName, Status);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
    Print(L"Error: %s %s\n", ErrorName, where);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    haveError = TRUE;

    return TRUE;
}
#else
BOOLEAN CheckFatalError(IN EFI_STATUS Status, IN CHAR16 *where)
{
//    CHAR16 ErrorName[64];

    if (!EFI_ERROR(Status))
        return FALSE;

    gST->ConOut->SetAttribute (gST->ConOut, ATTR_ERROR);
    Print(L"Fatal Error: %r %s\n", Status, where);
    gST->ConOut->SetAttribute (gST->ConOut, ATTR_BASIC);
    haveError = TRUE;

    //gBS->Exit(ImageHandle, ExitStatus, ExitDataSize, ExitData);

    return TRUE;
}

BOOLEAN CheckError(IN EFI_STATUS Status, IN CHAR16 *where)
{
    if (!EFI_ERROR(Status))
        return FALSE;

    gST->ConOut->SetAttribute (gST->ConOut, ATTR_ERROR);
    Print(L"Error: %r %s\n", Status, where);
    gST->ConOut->SetAttribute (gST->ConOut, ATTR_BASIC);
    haveError = TRUE;

    return TRUE;
}
#endif

//
// Graphics functions
//

VOID SwitchToGraphicsAndClear(VOID)
{
    SwitchToGraphics();
    if (GraphicsScreenDirty)
        BltClearScreen(TRUE);
}

VOID BltClearScreen(BOOLEAN ShowBanner)
{
    static EG_IMAGE *Banner = NULL;
    EG_IMAGE *NewBanner = NULL;
    INTN BannerPosX, BannerPosY;
    EG_PIXEL Black = { 0x0, 0x0, 0x0, 0 };

    if (ShowBanner && !(GlobalConfig.HideUIFlags & HIDEUI_FLAG_BANNER)) {
        // load banner on first call
        if (Banner == NULL) {
            if (GlobalConfig.BannerFileName)
                Banner = egLoadImage(SelfDir, GlobalConfig.BannerFileName, FALSE);
            if (Banner == NULL)
                Banner = egPrepareEmbeddedImage(&egemb_refind_banner, FALSE);
        }

        if (Banner) {
           if (GlobalConfig.BannerScale == BANNER_FILLSCREEN) {
              if ((Banner->Height != UGAHeight) || (Banner->Width != UGAWidth)) {
                 NewBanner = egScaleImage(Banner, UGAWidth, UGAHeight);
              } // if
           } else if ((Banner->Width > UGAWidth) || (Banner->Height > UGAHeight)) {
              NewBanner = egCropImage(Banner, 0, 0, (Banner->Width > UGAWidth) ? UGAWidth : Banner->Width,
                                      (Banner->Height > UGAHeight) ? UGAHeight : Banner->Height);
           } // if/elseif
           if (NewBanner) {
              MyFreePool(Banner);
              Banner = NewBanner;
           }
           MenuBackgroundPixel = Banner->PixelData[0];
        } // if Banner exists

        // clear and draw banner
        if (GlobalConfig.ScreensaverTime != -1)
           egClearScreen(&MenuBackgroundPixel);
        else
           egClearScreen(&Black);

        if (Banner != NULL) {
            BannerPosX = (Banner->Width < UGAWidth) ? ((UGAWidth - Banner->Width) / 2) : 0;
            BannerPosY = (INTN) (ComputeRow0PosY() / 2) - (INTN) Banner->Height;
            if (BannerPosY < 0)
               BannerPosY = 0;
            GlobalConfig.BannerBottomEdge = BannerPosY + Banner->Height;
            if (GlobalConfig.ScreensaverTime != -1)
               BltImage(Banner, (UINTN) BannerPosX, (UINTN) BannerPosY);
        }

    } else { // not showing banner
        // clear to menu background color
        egClearScreen(&MenuBackgroundPixel);
    }

    GraphicsScreenDirty = FALSE;
    egFreeImage(GlobalConfig.ScreenBackground);
    GlobalConfig.ScreenBackground = egCopyScreen();
} // VOID BltClearScreen()


VOID BltImage(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos)
{
    egDrawImage(Image, XPos, YPos);
    GraphicsScreenDirty = TRUE;
}

VOID BltImageAlpha(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos, IN EG_PIXEL *BackgroundPixel)
{
    EG_IMAGE *CompImage;

    // compose on background
    CompImage = egCreateFilledImage(Image->Width, Image->Height, FALSE, BackgroundPixel);
    egComposeImage(CompImage, Image, 0, 0);

    // blit to screen and clean up
    egDrawImage(CompImage, XPos, YPos);
    egFreeImage(CompImage);
    GraphicsScreenDirty = TRUE;
}

// VOID BltImageComposite(IN EG_IMAGE *BaseImage, IN EG_IMAGE *TopImage, IN UINTN XPos, IN UINTN YPos)
// {
//     UINTN TotalWidth, TotalHeight, CompWidth, CompHeight, OffsetX, OffsetY;
//     EG_IMAGE *CompImage;
//     
//     // initialize buffer with base image
//     CompImage = egCopyImage(BaseImage);
//     TotalWidth  = BaseImage->Width;
//     TotalHeight = BaseImage->Height;
//     
//     // place the top image
//     CompWidth = TopImage->Width;
//     if (CompWidth > TotalWidth)
//         CompWidth = TotalWidth;
//     OffsetX = (TotalWidth - CompWidth) >> 1;
//     CompHeight = TopImage->Height;
//     if (CompHeight > TotalHeight)
//         CompHeight = TotalHeight;
//     OffsetY = (TotalHeight - CompHeight) >> 1;
//     egComposeImage(CompImage, TopImage, OffsetX, OffsetY);
//
//     // blit to screen and clean up
//     egDrawImage(CompImage, XPos, YPos);
//     egFreeImage(CompImage);
//     GraphicsScreenDirty = TRUE;
// }

VOID BltImageCompositeBadge(IN EG_IMAGE *BaseImage, IN EG_IMAGE *TopImage, IN EG_IMAGE *BadgeImage, IN UINTN XPos, IN UINTN YPos)
{
     UINTN TotalWidth = 0, TotalHeight = 0, CompWidth = 0, CompHeight = 0, OffsetX = 0, OffsetY = 0;
     EG_IMAGE *CompImage = NULL;

     // initialize buffer with base image
     if (BaseImage != NULL) {
         CompImage = egCopyImage(BaseImage);
         TotalWidth  = BaseImage->Width;
         TotalHeight = BaseImage->Height;
     }

     // place the top image
     if ((TopImage != NULL) && (CompImage != NULL)) {
         CompWidth = TopImage->Width;
         if (CompWidth > TotalWidth)
               CompWidth = TotalWidth;
         OffsetX = (TotalWidth - CompWidth) >> 1;
         CompHeight = TopImage->Height;
         if (CompHeight > TotalHeight)
               CompHeight = TotalHeight;
         OffsetY = (TotalHeight - CompHeight) >> 1;
         egComposeImage(CompImage, TopImage, OffsetX, OffsetY);
     }

     // place the badge image
     if (BadgeImage != NULL && CompImage != NULL && (BadgeImage->Width + 8) < CompWidth && (BadgeImage->Height + 8) < CompHeight) {
         OffsetX += CompWidth  - 8 - BadgeImage->Width;
         OffsetY += CompHeight - 8 - BadgeImage->Height;
         egComposeImage(CompImage, BadgeImage, OffsetX, OffsetY);
     }

     // blit to screen and clean up
     if (CompImage->HasAlpha)
        egDrawImageWithTransparency(CompImage, NULL, XPos, YPos, CompImage->Width, CompImage->Height);
     else
        egDrawImage(CompImage, XPos, YPos);
     egFreeImage(CompImage);
     GraphicsScreenDirty = TRUE;
}

// Line-editing functions borrowed from gummiboot (cursor_left(), cursor_right(), & line_edit()).
// gummiboot is copyright (c) 2012 by Kay Sievers <kay.sievers@vrfy.org> and Harald Hoyer
// <harald@redhat.com> and is licensed under the LGPL 2.1.

static void cursor_left(UINTN *cursor, UINTN *first)
{
   if ((*cursor) > 0)
      (*cursor)--;
   else if ((*first) > 0)
      (*first)--;
}

static void cursor_right(UINTN *cursor, UINTN *first, UINTN x_max, UINTN len)
{
   if ((*cursor)+2 < x_max)
      (*cursor)++;
   else if ((*first) + (*cursor) < len)
      (*first)++;
}

BOOLEAN line_edit(CHAR16 *line_in, CHAR16 **line_out, UINTN x_max) {
   CHAR16 *line;
   UINTN size;
   UINTN len;
   UINTN first;
   UINTN y_pos = 3;
   CHAR16 *print;
   UINTN cursor;
   BOOLEAN exit;
   BOOLEAN enter;

   DrawScreenHeader(L"Line Editor");
   refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, (ConWidth - 71) / 2, ConHeight - 1);
   refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut,
                       L"Use cursor keys to edit, Esc to exit, Enter to boot with edited options");

   if (!line_in)
      line_in = L"";
   size = StrLen(line_in) + 1024;
   line = AllocatePool(size * sizeof(CHAR16));
   StrCpy(line, line_in);
   len = StrLen(line);
   print = AllocatePool(x_max * sizeof(CHAR16));

   refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, TRUE);

   first = 0;
   cursor = 0;
   enter = FALSE;
   exit = FALSE;
   while (!exit) {
      UINTN index;
      EFI_STATUS err;
      EFI_INPUT_KEY key;
      UINTN i;

      i = len - first;
      if (i >= x_max-2)
         i = x_max-2;
      CopyMem(print, line + first, i * sizeof(CHAR16));
      print[i++] = ' ';
      print[i] = '\0';

      refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, y_pos);
      refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, print);
      refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);

      refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
      err = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
      if (EFI_ERROR(err))
         continue;

      switch (key.ScanCode) {
      case SCAN_ESC:
         exit = TRUE;
         break;
      case SCAN_HOME:
         cursor = 0;
         first = 0;
         continue;
      case SCAN_END:
         cursor = len;
         if (cursor >= x_max) {
            cursor = x_max-2;
            first = len - (x_max-2);
         }
         continue;
      case SCAN_UP:
         while((first + cursor) && line[first + cursor] == ' ')
            cursor_left(&cursor, &first);
         while((first + cursor) && line[first + cursor] != ' ')
            cursor_left(&cursor, &first);
         while((first + cursor) && line[first + cursor] == ' ')
            cursor_left(&cursor, &first);
         if (first + cursor != len && first + cursor)
            cursor_right(&cursor, &first, x_max, len);
         refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
         continue;
      case SCAN_DOWN:
         while(line[first + cursor] && line[first + cursor] == ' ')
            cursor_right(&cursor, &first, x_max, len);
         while(line[first + cursor] && line[first + cursor] != ' ')
            cursor_right(&cursor, &first, x_max, len);
         while(line[first + cursor] && line[first + cursor] == ' ')
              cursor_right(&cursor, &first, x_max, len);
         refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
         continue;
      case SCAN_RIGHT:
         if (first + cursor == len)
            continue;
         cursor_right(&cursor, &first, x_max, len);
         refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
         continue;
      case SCAN_LEFT:
         cursor_left(&cursor, &first);
         refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
         continue;
      case SCAN_DELETE:
         if (len == 0)
            continue;
         if (first + cursor == len)
            continue;
         for (i = first + cursor; i < len; i++)
            line[i] = line[i+1];
         line[len-1] = ' ';
         len--;
         continue;
      }

      switch (key.UnicodeChar) {
      case CHAR_LINEFEED:
      case CHAR_CARRIAGE_RETURN:
         *line_out = line;
         line = NULL;
         enter = TRUE;
         exit = TRUE;
         break;
      case CHAR_BACKSPACE:
         if (len == 0)
            continue;
         if (first == 0 && cursor == 0)
            continue;
         for (i = first + cursor-1; i < len; i++)
            line[i] = line[i+1];
         len--;
         if (cursor > 0)
            cursor--;
         if (cursor > 0 || first == 0)
            continue;
         /* show full line if it fits */
         if (len < x_max-2) {
            cursor = first;
            first = 0;
            continue;
         }
         /* jump left to see what we delete */
         if (first > 10) {
            first -= 10;
            cursor = 10;
         } else {
            cursor = first;
            first = 0;
         }
         continue;
      case '\t':
      case ' ' ... '~':
      case 0x80 ... 0xffff:
         if (len+1 == size)
            continue;
         for (i = len; i > first + cursor; i--)
            line[i] = line[i-1];
         line[first + cursor] = key.UnicodeChar;
         len++;
         line[len] = '\0';
         if (cursor+2 < x_max)
            cursor++;
         else if (first + cursor < len)
            first++;
         continue;
      }
   }

   refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, FALSE);
   FreePool(print);
   FreePool(line);
   return enter;
} /* BOOLEAN line_edit() */
