# Writing Lister Plugins (version 2.1) for Total Commander

*Copyright (C) 2002-2018 Christian Ghisler, Ghisler Software GmbH. All Rights Reserved*

## Table of Contents

1. [Overview](#overview)
2. [Functions](#functions)
   - [Mandatory Functions](#mandatory-functions)
   - [Optional Functions](#optional-functions)
3. [Structures](#structures)
4. [Messages](#messages)
5. [Unicode Support](#unicode-support)
6. [64-bit Support](#64-bit-support)
7. [Header Files](#header-files)
8. [Plugin Interface Changes](#plugin-interface-changes)

## Overview

This help file is about writing lister plugins for Total Commander. Lister system plugins will be loaded in lister (F3 or Ctrl+Q in Total Commander) to show special file contents.

### The minimum function needed for a Lister plugin is:

**ListLoad** - Tells the plugin to load a file and create a child window for it

### The following are optional functions:

- **ListLoadNext** - Load next file with same plugin
- **ListCloseWindow** - Tells the plugin that the user switches to a different file or plugin
- **ListGetDetectString** - Allows Total Commander to detect the file types supported by the plugin without actually loading the plugin
- **ListSearchText** - The user tries to find text
- **ListSendCommand** - Passes various commands to the plugin
- **ListPrint** - Tells the plugin to print the displayed file
- **ListNotificationReceived** - The parent window of the plugin receives a notification message
- **ListSetDefaultParams** - Tells the plugin the interface version and default settings file
- **ListGetPreviewBitmap** - Can be used for thumbnail view to display a preview
- **ListSearchDialog** - Allows the plugin to show its own search dialog instead of the default dialog

### How it works:

When a user opens lister with F3 or the Quick View Panel with Ctrl+Q, Total Commander checks the section [ListerPlugins] in wincmd.ini. For all plugins found, it checks for the nr_detect key (with nr the plugin number). If present, the string is used as a parse function for the file. This allows to load only the plugin which is responsible for a specific file type. If nr_detect isn't found, the plugin is loaded and ListGetDetectString is called (if present). If ListGetDetectString exists, its return value is stored in nr_detect.

If nr_detect isn't present, or parsing of nr_detect returns true, the ListLoad function is called. If ListLoad returns a window handle, the load process is complete. Otherwise the next plugin is checked (as above).

- ListPrint is called if the user chooses the 'print' command from the menu.
- ListSendCommand is called when the user chooses some other menu command, like select all, or copy to clipboard.
- ListSearchText is called when the user uses the find or find next function.
- ListCloseWindow will be called when a different file is loaded, or lister is closed. If ListCloseWindow isn't implemented, the window will simply be closed with DestroyWindow.
- ListGetPreviewBitmap will be called to show a preview bitmap in thumbnail view. Please only implement this function if it makes sense for your type of images!

**Note:** It's extremely important to create a good detection string, especially if loading of your plugin is slow! With a good detection string, your plugin will only be loaded when needed. If you cannot make a good detection string, then make sure that your plugin doesn't have any static objects defined as global variables! These would be loaded with the DLL! Only create such objects in the ListLoad function!

---

## Functions

### Mandatory Functions

#### ListLoad

ListLoad is called when a user opens lister with F3 or the Quick View Panel with Ctrl+Q, and when the definition string either doesn't exist, or its evaluation returns true.

**Declaration:**
```c
HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags);
```

**Parameters:**
- **ParentWin** - This is lister's window. Create your plugin window as a child of this window.
- **FileToLoad** - The name of the file which has to be loaded.
- **ShowFlags** - A combination of the following flags:
  - `lcp_wraptext` - Text: Word wrap mode is checked
  - `lcp_fittowindow` - Images: Fit image to window is checked
  - `lcp_fitlargeronly` - Fit image to window only if larger than the window. Always set together with lcp_fittowindow.
  - `lcp_center` - Center image in viewer window
  - `lcp_ansi` - Ansi charset is checked
  - `lcp_ascii` - Ascii(DOS) charset is checked
  - `lcp_variable` - Variable width charset is checked
  - `lcp_forceshow` - User chose 'Image/Multimedia' from the menu. See remarks.

You may ignore these parameters if they don't apply to your document type.

**Return value:**
Return a handle to your window if load succeeds, NULL otherwise. If NULL is returned, Lister will try the next plugin.

**Remarks:**
Please note that multiple Lister windows can be open at the same time! Therefore you cannot save settings in global variables. You can call RegisterClass with the parameter cbWndExtra to reserve extra space for your data, which you can then access via GetWindowLong(). Or use an internal list, and store the list parameter via SetWindowLong(hwnd,GWL_ID,...).

Lister will subclass your window to catch some hotkeys like 'n' or 'p'.

When lister is activated, it will set the focus to your window. If your window contains child windows, then make sure that you set the focus to the correct child when your main window receives the focus!

If lcp_forceshow is defined, you may try to load the file even if the plugin wasn't made for it. Example: A plugin with line numbers may only show the file as such when the user explicitly chooses 'Image/Multimedia' from the menu.

Lister plugins which only create thumbnail images do not need to implement this function.

---

### Optional Functions

#### ListLoadNext

New in Total Commander 7: ListLoadNext is called when a user switches to the next or previous file in lister with 'n' or 'p' keys, or goes to the next/previous file in the Quick View Panel, and when the definition string either doesn't exist, or its evaluation returns true.

**Declaration:**
```c
int __stdcall ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags);
```

**Parameters:**
- **ParentWin** - This is lister's window. Your plugin window needs to be a child of this window
- **ListWin** - The plugin window returned by ListLoad
- **FileToLoad** - The name of the file which has to be loaded.
- **ShowFlags** - A combination of the same flags as ListLoad

**Return value:**
Return LISTPLUGIN_OK if load succeeds, LISTPLUGIN_ERROR otherwise. If LISTPLUGIN_ERROR is returned, Lister will try to load the file with the normal ListLoad function (also with other plugins).

**Remarks:**
Please note that multiple Lister windows can be open at the same time! Therefore you cannot save settings in global variables. You can call RegisterClass with the parameter cbWndExtra to reserve extra space for your data, which you can then access via GetWindowLong(). Or use an internal list, and store the list parameter via SetWindowLong(hwnd,GWL_ID,...).

Lister will subclass your window to catch some hotkeys like 'n' or 'p'.

When lister is activated, it will set the focus to your window. If your window contains child windows, then make sure that you set the focus to the correct child when your main window receives the focus!

If lcp_forceshow is defined, you may try to load the file even if the plugin wasn't made for it. Example: A plugin with line numbers may only show the file as such when the user explicitly chooses 'Image/Multimedia' from the menu.

Lister plugins which only create thumbnail images do not need to implement this function. If you do not implement ListLoadNext but only ListLoad, then the plugin will be unloaded and loaded again when switching through files, which results in flickering.

---

#### ListCloseWindow

ListCloseWindow is called when a user closes lister, or loads a different file. If ListCloseWindow isn't present, DestroyWindow() is called.

**Declaration:**
```c
void __stdcall ListCloseWindow(HWND ListWin);
```

**Parameters:**
- **ListWin** - This is the window handle which needs to be destroyed.

**Return value:**
This function doesn't return any value.

**Remarks:**
You can use this function to close open files, free buffers etc.

---

#### ListGetDetectString

ListGetDetectString is called when the plugin is loaded for the first time. It should return a parse function which allows Lister to find out whether your plugin can probably handle the file or not. You can use this as a first test - more thorough tests may be performed in ListLoad(). It's very important to define a good test string, especially when there are dozens of plugins loaded! The test string allows lister to load only those plugins relevant for that specific file type.

**Declaration:**
```c
void __stdcall ListGetDetectString(char* DetectString, int maxlen);
```

**Parameters:**
- **DetectString** - Return the detection string here. See remarks for the syntax.
- **maxlen** - Maximum length, in bytes, of the detection string (currently 2k).

**Return value:**
This function doesn't return any value.

**Remarks:**
The syntax of the detection string is as follows. There are operands, operators and functions.

**Operands:**
- `EXT` - The extension of the file to be loaded (always uppercase).
- `SIZE` - The size of the file to be loaded.
- `FORCE` - 1 if the user chose 'Image/Multimedia' from the menu, 0 otherwise.
- `MULTIMEDIA` - This detect string is special: It is always TRUE (also in older TC versions). If it is present in the string, this plugin overrides internal multimedia viewers in TC. If not, the internal viewers are used. Check the example below!
- `[5]` - The fifth byte in the file to be loaded. The first 8192 bytes can be checked for a match.
- `12345` - The number 12345
- `"TEST"` - The string "TEST"

**Operators:**
- `&` - AND. The left AND the right expression must be true (!=0).
- `|` - OR: Either the left OR the right expression needs to be true (!=0).
- `=` - EQUAL: The left and right expression need to be equal.
- `!=` - UNEQUAL: The left and right expression must not be equal.
- `<` - SMALLER: The left expression is smaller than the right expression. Comparing a number and a string returns false (0). Booleans are stored as 0 (false) and 1 (true).
- `>` - LARGER: The left expression is larger than the right expression.

**Functions:**
- `()` - Braces: The expression inside the braces is evaluated as a whole.
- `!()` - NOT: The expression inside the braces will be inverted. Note that the braces are necessary!
- `FIND()` - The text inside the braces is searched in the first 8192 bytes of the file. Returns 1 for success and 0 for failure.
- `FINDI()` - The text inside the braces is searched in the first 8192 bytes of the file. Upper/lowercase is ignored.

**Internal handling of variables:**
Variables can store numbers and strings. Operators can compare numbers with numbers and strings with strings, but not numbers with strings. Exception: A single char can also be compared with a number. Its value is its ANSI character code (e.g. "A"=65). Boolean values of comparisons are stored as 1 (true) and 0 (false).

**Examples:**

```
EXT="WAV" | EXT="AVI"
```
The file may be a Wave or AVI file.

```
EXT="WAV" & [0]="R" & [1]="I" & [2]="F" & [3]="F" & FIND("WAVEfmt")
```
Also checks for Wave header "RIFF" and string "WAVEfmt"

```
EXT="WAV" & (SIZE<1000000 | FORCE)
```
Load wave files smaller than 1000000 bytes at startup/file change, and all wave files if the user explicitly chooses 'Image/Multimedia' from the menu.

```
([0]="P" & [1]="K" & [2]=3 & [3]=4) | ([0]="P" & [1]="K" & [2]=7 & [3]=8)
```
Checks for the ZIP header PK#3#4 or PK#7#8 (the latter is used for multi-volume zip files).

```
EXT="TXT" & !(FINDI("<HEAD>") | FINDI("<BODY>"))
```
This plugin handles text files which aren't HTML files. A first detection is done with the <HEAD> and <BODY> tags. If these are not found, a more thorough check may be done in the plugin itself.

```
MULTIMEDIA & (EXT="WAV" | EXT="MP3")
```
Replace the internal player for WAV and MP3 files (which normally uses Windows Media Player as a plugin). Requires TC 6.0 or later!

**Operator precedence:**
The strongest operators are =, != < and >, then comes &, and finally |. What does this mean? Example:
```
expr1="a" & expr2 | expr3<5 & expr4!=b
```
will be evaluated as 
```
((expr1="a") & expr2) | ((expr3<5) & (expr4!="b"))
```
If in doubt, simply use braces to make the evaluation order clear.

---

#### ListGetPreviewBitmap

ListGetPreviewBitmap is called to retrieve a bitmap for the thumbnails view. Please only implement and export this function if it makes sense to show preview pictures for the supported file types! This function is new in version 1.4. It requires Total Commander >=6.5, but is ignored by older versions.

**Declaration:**
```c
HBITMAP __stdcall ListGetPreviewBitmap(char* FileToLoad, int width, int height,
    char* contentbuf, int contentbuflen);
```

**Parameters:**
- **FileToLoad** - The name of the file for which to load the preview bitmap.
- **width** - Requested maximum width of the bitmap.
- **height** - Requested maximum height of the bitmap
- **contentbuf** - The first 8 kBytes (8k) of the file. Often this is enough data to show a reasonable preview, e.g. the first few lines of a text file.
- **contentbuflen** - The length of the data passed in contentbuf. Please note that contentbuf is not a 0 terminated string, it may contains 0 bytes in the middle! It's just the 1:1 contents of the first 8k of the file.

**Return value:**
Return a device-dependent bitmap created with e.g. CreateCompatibleBitmap.

**Notes:**
1. This function is only called in Total Commander 6.5 and later. The plugin version will be >= 1.4.
2. The bitmap handle goes into possession of Total Commander, which will delete it after using it. The plugin must not delete the bitmap handle!
3. Make sure you scale your image correctly to the desired maximum width+height! Do not fill the rest of the bitmap - instead, create a bitmap which is SMALLER than requested! This way, Total Commander can center your image and fill the rest with the default background color.

The following sample code will stretch a bitmap with dimensions bigwidth*bigheight down to max. width*height keeping the correct aspect ratio (proportions):

```c
HBITMAP __stdcall ListGetPreviewBitmap(char* FileToLoad, int width, int height,
    char* contentbuf, int contentbuflen)
{
    int w, h;
    int stretchx, stretchy;
    OSVERSIONINFO vx;
    BOOL is_nt;
    BITMAP bmpobj;
    HBITMAP bmp_image, bmp_thumbnail, oldbmp_image, oldbmp_thumbnail;
    HDC maindc, dc_thumbnail, dc_image;
    POINT pt;

    // check for operating system: Windows 9x does NOT support the HALFTONE stretchblt mode!
    vx.dwOSVersionInfoSize = sizeof(vx);
    GetVersionEx(&vx);
    is_nt = vx.dwPlatformId == VER_PLATFORM_WIN32_NT;

    // here you load your image
    bmp_image = SomeHowLoadImageFromFile(FileToLoad);
    if (bmp_image && GetObject(bmp_image, sizeof(bmpobj), &bmpobj)) {
        bigx = bmpobj.bmWidth;
        bigy = bmpobj.bmHeight;
        // do we need to stretch?
        if ((bigx >= width || bigy >= height) && (bigx > 0 && bigy > 0)) {
            stretchy = MulDiv(width, bigy, bigx);
            if (stretchy <= height) {
                w = width;
                h = stretchy;
                if (h < 1) h = 1;
            } else {
                stretchx = MulDiv(height, bigx, bigy);
                w = stretchx;
                if (w < 1) w = 1;
                h = height;
            }
            maindc = GetDC(GetDesktopWindow());
            dc_thumbnail = CreateCompatibleDC(maindc);
            dc_image = CreateCompatibleDC(maindc);
            bmp_thumbnail = CreateCompatibleBitmap(maindc, w, h);
            ReleaseDC(GetDesktopWindow(), maindc);
            oldbmp_image = (HBITMAP)SelectObject(dc_image, bmp_image);
            oldbmp_thumbnail = (HBITMAP)SelectObject(dc_thumbnail, bmp_thumbnail);
            if (is_nt) {
                SetStretchBltMode(dc_thumbnail, HALFTONE);
                SetBrushOrgEx(dc_thumbnail, 0, 0, &pt);
            } else {
                SetStretchBltMode(dc_thumbnail, COLORONCOLOR);
            }
            StretchBlt(dc_thumbnail, 0, 0, w, h, dc_image, 0, 0, bigx, bigy, SRCCOPY);
            SelectObject(dc_image, oldbmp_image);
            SelectObject(dc_thumbnail, oldbmp_thumbnail);
            DeleteDC(dc_image);
            DeleteDC(dc_thumbnail);
            DeleteObject(bmp_image);
            bmp_image = bmp_thumbnail;
        }
    }
    return bmp_image;
}
```

---

## Structures

### ListDefaultParamStruct

ListDefaultParamStruct is passed to ListSetDefaultParams to inform the plugin about the current plugin interface version and ini file location.

**Declaration:**
```c
typedef struct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;
```

**Description of struct members:**
- **size** - The size of the structure, in bytes. Later revisions of the plugin interface may add more structure members, and will adjust this size field accordingly.
- **PluginInterfaceVersionLow** - Low value of plugin interface version. This is the value after the comma, multiplied by 100! Example. For plugin interface version 1.3, the low DWORD is 30 and the high DWORD is 1.
- **PluginInterfaceVersionHi** - High value of plugin interface version.
- **DefaultIniName** - Suggested location+name of the ini file where the plugin could store its data. This is a fully qualified path+file name, and will be in the same directory as the wincmd.ini. It's recommended to store the plugin data in this file or at least in this directory, because the plugin directory or the Windows directory may not be writable!

---

## Header Files

### listplug.h (C/C++)

```c
/* Contents of file listplug.h */

#define lc_copy         1
#define lc_newparams    2
#define lc_selectall    3
#define lc_setpercent   4

#define lcp_wraptext    1
#define lcp_fittowindow 2
#define lcp_ansi        4
#define lcp_ascii       8
#define lcp_variable    12
#define lcp_forceshow   16
#define lcp_fitlargeronly 32
#define lcp_center      64

#define lcs_findfirst   1
#define lcs_matchcase   2
#define lcs_wholewords  4
#define lcs_backwards   8

#define itm_percent     0xFFFE
#define itm_fontstyle   0xFFFD
#define itm_wrap        0xFFFC
#define itm_fit         0xFFFB
#define itm_next        0xFFFA
#define itm_center      0xFFF9

#define LISTPLUGIN_OK   0
#define LISTPLUGIN_ERROR 1

typedef struct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

HWND __stdcall ListLoad(HWND ParentWin,char* FileToLoad,int ShowFlags);
HWND __stdcall ListLoadW(HWND ParentWin,WCHAR* FileToLoad,int ShowFlags);
int __stdcall ListLoadNext(HWND ParentWin,HWND PluginWin,char* FileToLoad,int ShowFlags);
int __stdcall ListLoadNextW(HWND ParentWin,HWND PluginWin,WCHAR* FileToLoad,int ShowFlags);
void __stdcall ListCloseWindow(HWND ListWin);
void __stdcall ListGetDetectString(char* DetectString,int maxlen);
int __stdcall ListSearchText(HWND ListWin,char* SearchString,int SearchParameter);
int __stdcall ListSearchTextW(HWND ListWin,WCHAR* SearchString,int SearchParameter);
int __stdcall ListSearchDialog(HWND ListWin,int FindNext);
int __stdcall ListSendCommand(HWND ListWin,int Command,int Parameter);
int __stdcall ListPrint(HWND ListWin,char* FileToPrint,char* DefPrinter,
                        int PrintFlags,RECT* Margins);
int __stdcall ListPrintW(HWND ListWin,WCHAR* FileToPrint,WCHAR* DefPrinter,
                        int PrintFlags,RECT* Margins);
int __stdcall ListNotificationReceived(HWND ListWin,int Message,WPARAM wParam,LPARAM lParam);
void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps);
HBITMAP __stdcall ListGetPreviewBitmap(char* FileToLoad,int width,int height,
    char* contentbuf,int contentbuflen);
HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR* FileToLoad,int width,int height,
    char* contentbuf,int contentbuflen);
```