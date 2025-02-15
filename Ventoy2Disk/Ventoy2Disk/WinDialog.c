/******************************************************************************
 * WinDialog.c
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <Windows.h>
#include <Shlobj.h>
#include <commctrl.h>
#include "resource.h"
#include "Language.h"
#include "Ventoy2Disk.h"
#include "DiskService.h"
#include "VentoyJson.h"

HINSTANCE g_hInst;

BOOL g_SecureBoot = FALSE;
HWND g_DialogHwnd;
HWND g_ComboxHwnd;
HWND g_StaticLocalVerHwnd;
HWND g_StaticDiskVerHwnd;
HWND g_StaticLocalStyleHwnd;
HWND g_StaticDiskStyleHwnd;
HWND g_BtnInstallHwnd;
HWND g_StaticDevHwnd;
HWND g_StaticLocalHwnd;
HWND g_StaticLocalSecureHwnd;
HWND g_StaticDiskHwnd;
HWND g_StaticDiskSecureHwnd;
HWND g_BtnUpdateHwnd;
HWND g_ProgressBarHwnd;
HWND g_StaticStatusHwnd;
CHAR g_CurVersion[64];
HANDLE g_ThreadHandle = NULL;

HFONT g_language_normal_font = NULL;
HFONT g_language_bold_font = NULL;

int g_cur_part_style = 0; // 0:MBR  1:GPT
int g_language_count = 0;
int g_cur_lang_id = 0;
VENTOY_LANGUAGE *g_language_data = NULL;
VENTOY_LANGUAGE *g_cur_lang_data = NULL;

static const char* current_arch_string(void)
{
#if (defined VTARCH_X86)
    return "X86";
#elif (defined VTARCH_X64)
    return "X64"; 
#elif (defined VTARCH_ARM)
    return "ARM";
#elif (defined VTARCH_ARM64)
    return "ARM64"; 
#else
    return "XXX";
#endif
}

static int LoadCfgIni(void)
{
	int value;

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("PartStyle"), 0, VENTOY_CFG_INI);
    if (value == 1)
    {
        g_cur_part_style = 1;
    }

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("ShowAllDevice"), 0, VENTOY_CFG_INI);
    if (value == 1)
    {
        g_FilterUSB = 0;
    }

	return 0;
}

static int WriteCfgIni(void)
{
    WCHAR *CfgBuf = NULL;
    WORD UTFHdr = 0xFEFF;
    int charcount = 0;
    FILE *fp = NULL;


    fopen_s(&fp, VENTOY_CFG_INI_A, "wb+");
    if (fp == NULL)
    {
        return 1;
    }

    CfgBuf = (WCHAR *)malloc(1024 * 64);
    if (CfgBuf == NULL)
    {
        fclose(fp);
        return 1;
    }

    charcount = swprintf_s(CfgBuf, 1024 * 64 / sizeof(WCHAR),
        L"[Ventoy]\r\n"
        L"Language=%s\r\n"
        L"PartStyle=%d\r\n"
        L"ShowAllDevice=%d\r\n"
        ,
        g_language_data[g_cur_lang_id].Name,
        g_cur_part_style,
        1 - g_FilterUSB);

    fwrite(&UTFHdr, 1, sizeof(UTFHdr), fp);
    fwrite(CfgBuf, 1, charcount * sizeof(WCHAR), fp);
    fclose(fp);

    free(CfgBuf);

//	WritePrivateProfileString(TEXT("Ventoy"), TEXT("Language"), g_language_data[g_cur_lang_id].Name, VENTOY_CFG_INI);

//  swprintf_s(TmpBuf, 128, TEXT("%d"), g_SecureBoot);
//  WritePrivateProfileString(TEXT("Ventoy"), TEXT("SecureBoot"), TmpBuf, VENTOY_CFG_INI);

	return 0;
}


void GetExeVersionInfo(const char *FilePath)
{
    UINT length;
    DWORD verBufferSize;
    CHAR  verBuffer[2048];
    VS_FIXEDFILEINFO *verInfo = NULL;

    verBufferSize = GetFileVersionInfoSizeA(FilePath, NULL);

    if (verBufferSize > 0 && verBufferSize <= sizeof(verBuffer))
    {
        if (GetFileVersionInfoA(FilePath, 0, verBufferSize, (LPVOID)verBuffer))
        {
            VerQueryValueA(verBuffer, "\\", &verInfo, &length);

            safe_sprintf(g_CurVersion, "%u.%u.%u.%u",
                HIWORD(verInfo->dwProductVersionMS),
                LOWORD(verInfo->dwProductVersionMS),
                HIWORD(verInfo->dwProductVersionLS),
                LOWORD(verInfo->dwProductVersionLS));
        }
    }
}

void SetProgressBarPos(int Pos)
{
    CHAR Ratio[64];

    if (Pos >= PT_FINISH)
    {
        Pos = PT_FINISH;
    }

    SendMessage(g_ProgressBarHwnd, PBM_SETPOS, Pos, 0);

    safe_sprintf(Ratio, "Status - %.0lf%%", Pos * 100.0 / PT_FINISH);
    SetWindowTextA(g_StaticStatusHwnd, Ratio);
}

static void UpdateLocalVentoyVersion()
{
    CHAR Ver[128];

	safe_sprintf(Ver, "%s", GetLocalVentoyVersion());
	SetWindowTextA(g_StaticLocalVerHwnd, Ver);

	SetWindowTextA(g_StaticLocalStyleHwnd, g_cur_part_style ? "GPT" : "MBR");
}

static void OnComboxSelChange(HWND hCombox)
{
    int nCurSelected;
    PHY_DRIVE_INFO *CurDrive = NULL;
    HMENU SubMenu;    
    HMENU hMenu = GetMenu(g_DialogHwnd);

    UpdateLocalVentoyVersion();
    SetWindowTextA(g_StaticDiskVerHwnd, "");
	SetWindowTextA(g_StaticDiskStyleHwnd, "");
    SetWindowTextA(g_StaticDiskSecureHwnd, "");
    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    SubMenu = GetSubMenu(hMenu, 0);
    ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_BYPOSITION | MF_STRING | MF_DISABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));    

    if (g_PhyDriveCount > 0)
    {
        nCurSelected = (int)SendMessage(hCombox, CB_GETCURSEL, 0, 0);
        if (CB_ERR != nCurSelected)
        {
            CurDrive = GetPhyDriveInfoById(nCurSelected);
        }
    }
    
    if (CurDrive)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_BYPOSITION | MF_STRING | MF_ENABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
        SetWindowTextA(g_StaticDiskVerHwnd, CurDrive->VentoyVersion);

		if (CurDrive->VentoyVersion[0])
		{
			SetWindowTextA(g_StaticDiskStyleHwnd, CurDrive->PartStyle ? "GPT" : "MBR");

            Log("Combox select change, update secure boot option: %u %u", g_SecureBoot, CurDrive->SecureBootSupport);
            g_SecureBoot = CurDrive->SecureBootSupport;

            if (g_SecureBoot)
            {
                SetWindowText(g_StaticDiskSecureHwnd, SECURE_ICON_STRING);
                SetWindowText(g_StaticLocalSecureHwnd, SECURE_ICON_STRING);
                CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
            }
            else
            {
                SetWindowTextA(g_StaticDiskSecureHwnd, "");
                SetWindowTextA(g_StaticLocalSecureHwnd, "");
                CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
            }
		}
		else
		{
			SetWindowTextA(g_StaticDiskStyleHwnd, "");

            Log("Not ventoy disk, clear secure boot option");
            g_SecureBoot = FALSE;
            SetWindowTextA(g_StaticDiskSecureHwnd, "");
            SetWindowTextA(g_StaticLocalSecureHwnd, "");
            CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
		}
		
		
        if (g_ForceOperation == 0)
        {
            if (CurDrive->VentoyVersion[0])
            {
                //only can update
                EnableWindow(g_BtnInstallHwnd, FALSE);
                EnableWindow(g_BtnUpdateHwnd, TRUE);
            }
            else
            {
                //only can install
                EnableWindow(g_BtnInstallHwnd, TRUE);
                EnableWindow(g_BtnUpdateHwnd, FALSE);
            }
        }
        else
        {
            EnableWindow(g_BtnInstallHwnd, TRUE);
			if (CurDrive->VentoyVersion[0])
			{
				EnableWindow(g_BtnUpdateHwnd, TRUE);
			}
        }
    }
    
    InvalidateRect(g_DialogHwnd, NULL, TRUE);
    UpdateWindow(g_DialogHwnd);
}

static void UpdateReservedPostfix(void)
{
	int Space = 0;
	WCHAR Buf[128] = { 0 };
	
	Space = GetReservedSpaceInMB();

	if (Space <= 0)
	{
		SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DEV), _G(STR_DEVICE));
	}
	else
	{
		if (Space % 1024 == 0)
		{
			wsprintf(Buf, L"%s  [ -%dGB ]", _G(STR_DEVICE), Space / 1024);
		}
		else
		{
			wsprintf(Buf, L"%s  [ -%dMB ]", _G(STR_DEVICE), Space);
		}
		SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DEV), Buf);
	}
}

static void UpdateItemString(int defaultLangId)
{
	int i;
    UINT State;
	HMENU SubMenu;
	HFONT hLangFont, hBoldFont;
	HMENU hMenu = GetMenu(g_DialogHwnd);

	g_cur_lang_id = defaultLangId;
	g_cur_lang_data = g_language_data + defaultLangId;

	hBoldFont = hLangFont = CreateFont(g_language_data[defaultLangId].FontSize, 0, 0, 0, 700, FALSE, FALSE, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH, g_language_data[defaultLangId].FontFamily);

	hLangFont = CreateFont(g_language_data[defaultLangId].FontSize, 0, 0, 0, 400, FALSE, FALSE, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH, g_language_data[defaultLangId].FontFamily);

	SendMessage(g_BtnInstallHwnd, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
	SendMessage(g_BtnUpdateHwnd, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

	SendMessage(g_StaticStatusHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticLocalHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticDiskHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticDevHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_DialogHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);

    g_language_normal_font = hLangFont;
    g_language_bold_font = hBoldFont;

	ModifyMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, 0, _G(STR_MENU_OPTION));

	UpdateReservedPostfix();

	SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_LOCAL), _G(STR_LOCAL_VER));
	SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DISK), _G(STR_DISK_VER));
	SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));

	SetWindowText(g_BtnInstallHwnd, _G(STR_INSTALL));
	SetWindowText(g_BtnUpdateHwnd, _G(STR_UPDATE));

	SubMenu = GetSubMenu(hMenu, 0);
	if (g_SecureBoot)
	{
        SetWindowText(g_StaticLocalSecureHwnd, SECURE_ICON_STRING);
		ModifyMenu(SubMenu, OPT_SUBMENU_SECURE_BOOT, MF_BYPOSITION | MF_STRING | MF_CHECKED, 0, _G(STR_MENU_SECURE_BOOT));
	}
	else
	{
        SetWindowTextA(g_StaticLocalSecureHwnd, "");
        ModifyMenu(SubMenu, OPT_SUBMENU_SECURE_BOOT, MF_BYPOSITION | MF_STRING | MF_UNCHECKED, 0, _G(STR_MENU_SECURE_BOOT));
	}
    
    ModifyMenu(SubMenu, OPT_SUBMENU_PART_STYLE, MF_STRING | MF_BYPOSITION, VTOY_MENU_PART_STYLE, _G(STR_MENU_PART_STYLE));
    ModifyMenu(SubMenu, OPT_SUBMENU_PART_CFG, MF_STRING | MF_BYPOSITION, VTOY_MENU_PART_CFG, _G(STR_MENU_PART_CFG));

    State = GetMenuState(SubMenu, VTOY_MENU_CLEAN, MF_BYCOMMAND);
    if (State & MF_DISABLED)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_STRING | MF_BYPOSITION | MF_DISABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_STRING | MF_BYPOSITION, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
    }

    if (g_FilterUSB == 0)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
    }

#if VTSI_SUPPORT
    if (g_WriteImage == 1)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
    }
#endif

	ShowWindow(g_DialogHwnd, SW_HIDE);
	ShowWindow(g_DialogHwnd, SW_NORMAL);

	//Update check
	for (i = 0; i < g_language_count; i++)
	{
		CheckMenuItem(hMenu, VTOY_MENU_LANGUAGE_BEGIN | i, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
	}
	CheckMenuItem(hMenu, VTOY_MENU_LANGUAGE_BEGIN | defaultLangId, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
}

static int ventoy_compare_language(VENTOY_LANGUAGE *lang1, VENTOY_LANGUAGE *lang2)
{
	if (lstrcmp(lang1->Name, TEXT("Chinese Simplified (简体中文)")) == 0)
	{
		return -1;
	}
	else if (lstrcmp(lang2->Name, TEXT("Chinese Simplified (简体中文)")) == 0)
	{
		return 1;
	}

	return lstrcmp(lang1->Name, lang2->Name);
}

static void ventoy_sort_language(VENTOY_LANGUAGE *LangData, int LangCount)
{
	int i, j;
	VENTOY_LANGUAGE *tmpdata = NULL;

	tmpdata = (VENTOY_LANGUAGE *)malloc(sizeof(VENTOY_LANGUAGE));
    if (tmpdata == NULL)
    {
        return;
    }

	for (i = 0; i < LangCount; i++)
	{
		for (j = i + 1; j < LangCount; j++)
		{
			if (ventoy_compare_language(LangData + j, LangData + i) < 0)
			{
				memcpy(tmpdata, LangData + i, sizeof(VENTOY_LANGUAGE));
				memcpy(LangData + i, LangData + j, sizeof(VENTOY_LANGUAGE));
				memcpy(LangData + j, tmpdata, sizeof(VENTOY_LANGUAGE));
			}
		}
	}

	free(tmpdata);
}

static void LoadLanguageFromIni(void)
{
    int i, j, k;
    WCHAR *SectionName = NULL;
    WCHAR *SectionNameBuf = NULL;
    VENTOY_LANGUAGE *cur_lang = NULL;
    WCHAR Language[64];
    WCHAR TmpBuf[64];

    swprintf_s(Language, 64, L"StringDefine");
    for (i = 0; i < STR_ID_MAX; i++)
    {
        swprintf_s(TmpBuf, 64, L"%d", i);
        GET_INI_STRING(Language, TmpBuf, g_language_data[0].StrId[i]);
    }

    SectionNameBuf = (WCHAR *)malloc(SIZE_1MB);
    if (SectionNameBuf == NULL)
    {
        return;
    }

    GetPrivateProfileSectionNames(SectionNameBuf, SIZE_1MB / sizeof(WCHAR), VENTOY_LANGUAGE_INI);

    cur_lang = g_language_data;
    for (SectionName = SectionNameBuf; *SectionName && g_language_count < VENTOY_MAX_LANGUAGE; SectionName += (lstrlen(SectionName) + 1))
    {
        if (lstrlen(SectionName) < 9 || memcmp(L"Language-", SectionName, 9 * sizeof(WCHAR)))
        {
            continue;
        }

        // "Language-"
        lstrcpy(cur_lang->Name, SectionName + 9);

        GET_INI_STRING(SectionName, TEXT("FontFamily"), cur_lang->FontFamily);
        cur_lang->FontSize = GetPrivateProfileInt(SectionName, TEXT("FontSize"), 10, VENTOY_LANGUAGE_INI);

        for (j = 0; j < STR_ID_MAX; j++)
        {
            GET_INI_STRING(SectionName, g_language_data[0].StrId[j], cur_lang->MsgString[j]);

            for (k = 0; cur_lang->MsgString[j][k] && cur_lang->MsgString[j][k + 1]; k++)
            {
                if (cur_lang->MsgString[j][k] == '#' && cur_lang->MsgString[j][k + 1] == '@')
                {
                    cur_lang->MsgString[j][k] = '\r';
                    cur_lang->MsgString[j][k + 1] = '\n';
                }
            }
        }

        g_language_count++;
        cur_lang++;
    }
    free(SectionNameBuf);

    Log("Total %d languages ...", g_language_count);
}

static void UTF8ToWString(const char *str, WCHAR *buf)
{
    int wcsLen;
    int len = (int)strlen(str);

    wcsLen = MultiByteToWideChar(CP_UTF8, 0, str, len, NULL, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, len, buf, wcsLen);
}

static void LoadLanguageFromJson(void)
{
    int k;
    int ret;
    int index = 0;
    int len = 0;
    char *buf = NULL;
    VTOY_JSON *json = NULL;
    VTOY_JSON *node = NULL;
    VTOY_JSON *cur = NULL;
    VENTOY_LANGUAGE *cur_lang = NULL;

    ReadWholeFileToBuf(VENTOY_LANGUAGE_JSON_A, 4, &buf, &len);
    buf[len] = 0;

    json = vtoy_json_create();

    ret = vtoy_json_parse(json, buf);

    Log("language json file len:%d json parse:%d", len, ret);

    cur_lang = g_language_data;
    for (node = json->pstChild; node; node = node->pstNext)
    {
        cur = node->pstChild;
        index = 0;
        while (cur)
        {
            if (strncmp(cur->pcName, "name", 4) == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->Name);
            }
            else if (strcmp(cur->pcName, "FontFamily") == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->FontFamily);
            }
            else if (strcmp(cur->pcName, "FontSize") == 0)
            {
                cur_lang->FontSize = (int)cur->unData.lValue;
            }
            else if (strncmp(cur->pcName, "STR_", 4) == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->MsgString[index]);

                for (k = 0; cur_lang->MsgString[index][k] && cur_lang->MsgString[index][k + 1]; k++)
                {
                    if (cur_lang->MsgString[index][k] == '#' && cur_lang->MsgString[index][k + 1] == '@')
                    {
                        cur_lang->MsgString[index][k] = '\r';
                        cur_lang->MsgString[index][k + 1] = '\n';
                    }
                }

                index++;
            }
            cur = cur->pstNext;
        }

        cur_lang++;
        g_language_count++;
    }

    vtoy_json_destroy(json);
    free(buf);

    Log("Total %d languages ...", g_language_count);
}


static void LanguageInit(void)
{
    int i;
	int id = -1, DefaultId = -1;
	WCHAR TmpBuf[256];
	LANGID LangId = GetSystemDefaultUILanguage();
	HMENU SubMenu;
	HMENU hMenu = GetMenu(g_DialogHwnd);

    SubMenu = GetSubMenu(hMenu, 0);
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_PART_CFG, TEXT("yyy"));
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_ALL_DEV, TEXT("USB Device Only")); 

#if VTSI_SUPPORT    
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_VTSI, TEXT("Generate VTSI File"));    
#endif

    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_CLEAN, TEXT("yyy"));       

    if (g_cur_part_style)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_CHECKED);
    }
    else
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_UNCHECKED);
    }

	SubMenu = GetSubMenu(hMenu, 1);
	DeleteMenu(SubMenu, 0, MF_BYPOSITION);

	g_language_data = (VENTOY_LANGUAGE *)malloc(sizeof(VENTOY_LANGUAGE)* VENTOY_MAX_LANGUAGE);
    if (g_language_data == NULL)
    {
        return;
    }

	memset(g_language_data, 0, sizeof(VENTOY_LANGUAGE)* VENTOY_MAX_LANGUAGE);

    if (IsFileExist(VENTOY_LANGUAGE_JSON_A))
    {
        Log("Load languages from json file ...");
        LoadLanguageFromJson(); 
    }
    else
    {
        Log("Load languages from ini file ...");
        LoadLanguageFromIni();
    }

	ventoy_sort_language(g_language_data, g_language_count);

	if (MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED) == LangId)
	{
		DefaultId = 0;
	}

	memset(TmpBuf, 0, sizeof(TmpBuf));
	GetPrivateProfileString(TEXT("Ventoy"), TEXT("Language"), TEXT("#"), TmpBuf, 256, VENTOY_CFG_INI);

	for (i = 0; i < g_language_count; i++)
	{
		AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_LANGUAGE_BEGIN | i, g_language_data[i].Name);
		
		if (id < 0 && lstrcmp(g_language_data[i].Name, TmpBuf) == 0)
		{
			id = i;
		}

		if (DefaultId < 0 && lstrcmp(g_language_data[i].Name, TEXT("English (English)")) == 0)
		{
			DefaultId = i;
		}
	}

	if (id < 0)
	{
		id = DefaultId;
	}



	UpdateItemString(id);
}

static void InitComboxCtrl(HWND hWnd)
{
    DWORD i, j;
    HANDLE hCombox;
    CHAR Drive[16];
    CHAR Letter[128];
    CHAR DeviceName[256];

    hCombox = GetDlgItem(hWnd, IDC_COMBO1);

    // delete all items
    SendMessage(hCombox, CB_RESETCONTENT, 0, 0);
    
    //Fill device combox
    for (i = 0; i < g_PhyDriveCount; i++)
    {
        if (g_PhyDriveList[i].Id < 0)
        {
            continue;
        }

        if (g_PhyDriveList[i].DriveLetters[0])
        {
            safe_sprintf(Letter, "%C: ", g_PhyDriveList[i].DriveLetters[0]);
            for (j = 1; j < sizeof(g_PhyDriveList[i].DriveLetters) / sizeof(CHAR); j++)
            {
                if (g_PhyDriveList[i].DriveLetters[j] == 0)
                {
                    break;
                }
                safe_sprintf(Drive, "%C: ", g_PhyDriveList[i].DriveLetters[j]);
                strcat_s(Letter, sizeof(Letter), Drive);
            }
        }
        else
        {
            Letter[0] = 0;
        }

        safe_sprintf(DeviceName, "%s[%dGB] %s %s",
            Letter,
            GetHumanReadableGBSize(g_PhyDriveList[i].SizeInBytes),
            g_PhyDriveList[i].VendorId,
            g_PhyDriveList[i].ProductId
            );
        SendMessageA(hCombox, CB_ADDSTRING, 0, (LPARAM)DeviceName);
    }

    SendMessage(hCombox, CB_SETCURSEL, 0, 0);
    OnComboxSelChange(g_ComboxHwnd);
}

static BOOL InitDialog(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	//HFONT hStyleFont;
    HFONT hStaticFont;	
    HICON hIcon;
    CHAR WinText[128];

    g_DialogHwnd = hWnd;
    g_ComboxHwnd = GetDlgItem(hWnd, IDC_COMBO1);
    g_StaticLocalVerHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_VER);
    g_StaticDiskVerHwnd = GetDlgItem(hWnd, IDC_STATIC_DISK_VER);
	g_StaticLocalStyleHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_STYLE);
	g_StaticDiskStyleHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV_STYLE);

    g_BtnInstallHwnd = GetDlgItem(hWnd, IDC_BUTTON4);

	g_StaticDevHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV);
	g_StaticLocalHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL);
	g_StaticDiskHwnd = GetDlgItem(hWnd, IDC_STATIC_DISK);

    g_StaticDiskSecureHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV_SECURE);
    g_StaticLocalSecureHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_SECURE);
    SetWindowTextA(g_StaticDiskSecureHwnd, "");
    SetWindowTextA(g_StaticLocalSecureHwnd, "");

    g_BtnUpdateHwnd = GetDlgItem(hWnd, IDC_BUTTON3);
    g_ProgressBarHwnd = GetDlgItem(hWnd, IDC_PROGRESS1);
    g_StaticStatusHwnd = GetDlgItem(hWnd, IDC_STATIC_STATUS);

    hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON1));
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    SendDlgItemMessage(hWnd, IDC_COMMAND1, BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON2)));

    SendMessage(g_ProgressBarHwnd, PBM_SETRANGE, (WPARAM)0, (LPARAM)(MAKELPARAM(0, PT_FINISH)));
    PROGRESS_BAR_SET_POS(PT_START);

	SetMenu(hWnd, LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU1)));

    LanguageInit();

    sprintf_s(WinText, sizeof(WinText), "Ventoy2Disk  %s", current_arch_string());
    SetWindowTextA(hWnd, WinText);

    // Set static text & font 
    hStaticFont = CreateFont(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, 0,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH&FF_SWISS, TEXT("Courier New"));
	SendMessage(g_StaticLocalVerHwnd, WM_SETFONT, (WPARAM)hStaticFont, TRUE);
	SendMessage(g_StaticDiskVerHwnd, WM_SETFONT, (WPARAM)hStaticFont, TRUE);

#if 0
	hStyleFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
		ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH&FF_SWISS, TEXT("Courier New"));
	SendMessage(g_StaticLocalStyleHwnd, WM_SETFONT, (WPARAM)hStyleFont, TRUE);
	SendMessage(g_StaticDiskStyleHwnd, WM_SETFONT, (WPARAM)hStyleFont, TRUE);
#endif


    InitComboxCtrl(hWnd);

    SetFocus(g_ProgressBarHwnd);

    return TRUE;
}

static DWORD WINAPI InstallVentoyThread(void* Param)
{
    int rc;
    int TryId = 1;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    if (g_WriteImage)
    {
        rc = InstallVentoy2FileImage(pPhyDrive, g_cur_part_style);
    }
    else
    {
        rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);
        if (rc)
        {
            Log("This time install failed, clean disk, wait 5s and retry...");
            VDS_CleanDisk(pPhyDrive->PhyDrive);

            Sleep(5000);

            Log("Now retry to install...");
            rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);

            if (rc)
            {
                Log("This time install failed, clean disk, wait 10s and retry...");
                DSPT_CleanDisk(pPhyDrive->PhyDrive);

                Sleep(10000);

                Log("Now retry to install...");
                rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);
            }
        }
    }

    if (rc == 0)
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, g_WriteImage ? _G(STR_VTSI_CREATE_SUCCESS) : _G(STR_INSTALL_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);

        if (g_WriteImage == 0)
        {
            safe_strcpy(pPhyDrive->VentoyVersion, GetLocalVentoyVersion());
            pPhyDrive->PartStyle = g_cur_part_style;
            pPhyDrive->SecureBootSupport = g_SecureBoot;
        }
    }
    else
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, g_WriteImage ? _G(STR_VTSI_CREATE_FAILED) : _G(STR_INSTALL_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }
    
	PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    return 0;
}

static DWORD WINAPI ClearVentoyThread(void* Param)
{
    int rc;
    UINT Drive = 0;
    CHAR DrvLetter = 0;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    rc = ClearVentoyFromPhyDrive(g_DialogHwnd, pPhyDrive, &DrvLetter);
    if (rc)
    {
        Log("This time clear failed, now wait and retry...");
        Sleep(10000);

        Log("Now retry to clear...");

        rc = ClearVentoyFromPhyDrive(g_DialogHwnd, pPhyDrive, &DrvLetter);
    }

    if (rc == 0)
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_CLEAR_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
        safe_strcpy(pPhyDrive->VentoyVersion, "");
    }
    else
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_CLEAR_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }

    PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    if (rc == 0 && DrvLetter > 0)
    {
        if (DrvLetter >= 'A' && DrvLetter <= 'Z')
        {
            Drive = DrvLetter - 'A';
        }
        else if (DrvLetter >= 'a' && DrvLetter <= 'z')
        {
            Drive = DrvLetter - 'a';
        }

        if (Drive > 0)
        {
            //SHFormatDrive(g_DialogHwnd, Drive, SHFMT_ID_DEFAULT, SHFMT_OPT_FULL);
        }
    }

    return 0;
}


static DWORD WINAPI UpdateVentoyThread(void* Param)
{
    int rc;
    int TryId = 1;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
	if (rc)
	{
		Log("This time update failed, now wait and retry...");
		Sleep(5000);

		Log("Now retry to update...");
        rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);

		//Try3 is dangerous ...
		Sleep(1000);
		Log("Now retry to update...");
		rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
	}

    if (rc == 0)
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_UPDATE_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
        safe_strcpy(pPhyDrive->VentoyVersion, GetLocalVentoyVersion());
        pPhyDrive->SecureBootSupport = g_SecureBoot;
    }
    else
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_UPDATE_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }
    
	PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    return 0;
}



static void OnInstallBtnClick(void)
{
    int nCurSel;
	int SpaceMB = 0;
	int SizeInMB = 0;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (g_WriteImage)
    {
        if (MessageBox(g_DialogHwnd, _G(STR_VTSI_CREATE_TIP), _G(STR_INFO), MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
    }
    else
    {
        if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }

        if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP2), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

	if (g_cur_part_style == 0 && pPhyDrive->SizeInBytes > 2199023255552ULL)
	{
		MessageBox(g_DialogHwnd, _G(STR_DISK_2TB_MBR_ERROR), _G(STR_ERROR), MB_OK | MB_ICONERROR);
		return;
	}

	SpaceMB = GetReservedSpaceInMB();
	SizeInMB = (int)(pPhyDrive->SizeInBytes / 1024 / 1024);
	Log("SpaceMB:%d SizeInMB:%d", SpaceMB, SizeInMB);

	if (SizeInMB <= SpaceMB || (SizeInMB - SpaceMB) <= (VENTOY_EFI_PART_SIZE / SIZE_1MB))
	{
		MessageBox(g_DialogHwnd, _G(STR_SPACE_VAL_INVALID), _G(STR_ERROR), MB_OK | MB_ICONERROR);
		Log("Invalid space value ...");
		return;
	}

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, InstallVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void OnRefreshBtnClick(HWND hWnd)
{
    Log("#### Now Refresh PhyDrive ####");
    Ventoy2DiskDestroy();
    Ventoy2DiskInit();
    InitComboxCtrl(hWnd);
}

static void OnUpdateBtnClick(void)
{
    int nCurSel;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (MessageBox(g_DialogHwnd, _G(STR_UPDATE_TIP), _G(STR_INFO), MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return;
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, UpdateVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void OnClearVentoy(hWnd)
{
    int nCurSel;
    int SpaceMB = 0;
    int SizeInMB = 0;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP2), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, ClearVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void MenuProc(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WORD CtrlID;
    HMENU SubMenu;
	HMENU hMenu = GetMenu(hWnd);

	CtrlID = LOWORD(wParam);

	if (CtrlID == 0)
	{
		g_SecureBoot = !g_SecureBoot;

		if (g_SecureBoot)
		{
            SetWindowText(g_StaticLocalSecureHwnd, SECURE_ICON_STRING);
			CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
		}
		else
		{
            SetWindowTextA(g_StaticLocalSecureHwnd, "");
			CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
		}
	}
    else if (CtrlID == VTOY_MENU_PART_CFG)
    {
        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG2), hWnd, PartDialogProc);
		UpdateReservedPostfix();
    }
    else if (CtrlID == VTOY_MENU_CLEAN)
    {
        OnClearVentoy(hWnd);
    }
#if VTSI_SUPPORT  
    else if (CtrlID == VTOY_MENU_VTSI)
    {
        SubMenu = GetSubMenu(hMenu, 0);

        g_WriteImage = 1 - g_WriteImage;
        if (g_WriteImage == 1)
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
        }
        else
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
        }
    }
#endif
    else if (CtrlID == VTOY_MENU_ALL_DEV)
    {
        SubMenu = GetSubMenu(hMenu, 0);

        g_FilterUSB = 1 - g_FilterUSB;
        if (g_FilterUSB == 0)
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
        }
        else
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
        }

        OnRefreshBtnClick(hWnd);
    }
    else if (CtrlID == ID_PARTSTYLE_MBR)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_UNCHECKED);
        g_cur_part_style = 0;
        UpdateLocalVentoyVersion();
        ShowWindow(g_DialogHwnd, SW_HIDE);
        ShowWindow(g_DialogHwnd, SW_NORMAL);
    }
    else if (CtrlID == ID_PARTSTYLE_GPT)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_CHECKED);
        g_cur_part_style = 1;
        UpdateLocalVentoyVersion();
        ShowWindow(g_DialogHwnd, SW_HIDE);
        ShowWindow(g_DialogHwnd, SW_NORMAL);
    }
	else if (CtrlID >= VTOY_MENU_LANGUAGE_BEGIN && CtrlID < VTOY_MENU_LANGUAGE_BEGIN + g_language_count)
	{
		UpdateItemString(CtrlID - VTOY_MENU_LANGUAGE_BEGIN);
	}
}

INT_PTR CALLBACK DialogProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    WORD NotifyCode;
    WORD CtrlID;

    switch (Message)
    {
		case WM_NOTIFY:
		{
			UINT code = 0;
			UINT_PTR idFrom = 0;

			if (lParam)
			{
				code = ((LPNMHDR)lParam)->code;
				idFrom = ((LPNMHDR)lParam)->idFrom;
			}
			
			if (idFrom == IDC_SYSLINK1 && (NM_CLICK == code || NM_RETURN == code))
			{
				ShellExecute(NULL, L"open", L"https://www.ventoy.net", NULL, NULL, SW_SHOW);
			}
			break;
		}
        case WM_COMMAND:
        {
            NotifyCode = HIWORD(wParam);
            CtrlID = LOWORD(wParam);

            if (CtrlID == IDC_COMBO1 && NotifyCode == CBN_SELCHANGE)
            {
                OnComboxSelChange((HWND)lParam);
            }

            if (CtrlID == IDC_BUTTON4 && NotifyCode == BN_CLICKED)
            {
                OnInstallBtnClick();
            }
            else if (CtrlID == IDC_BUTTON3 && NotifyCode == BN_CLICKED)
            {
                OnUpdateBtnClick();
            }
            else if (CtrlID == IDC_COMMAND1 && NotifyCode == BN_CLICKED)
            {
                OnRefreshBtnClick(hWnd);
            }

			if (lParam == 0 && NotifyCode == 0)
			{
				MenuProc(hWnd, wParam, lParam);
			}

            break;
        }
        case WM_INITDIALOG:
        {
            InitDialog(hWnd, wParam, lParam);
            break;
        }
        case WM_CTLCOLORSTATIC:
        {
            if (GetDlgItem(hWnd, IDC_STATIC_LOCAL_VER) == (HANDLE)lParam || 
                GetDlgItem(hWnd, IDC_STATIC_DISK_VER) == (HANDLE)lParam)
            {
                SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, RGB(255, 0, 0));
                return (LRESULT)(HBRUSH)(GetStockObject(HOLLOW_BRUSH));
            }
#if 0
            else if (GetDlgItem(hWnd, IDC_STATIC_LOCAL_SECURE) == (HANDLE)lParam ||
                GetDlgItem(hWnd, IDC_STATIC_DEV_SECURE) == (HANDLE)lParam)
			{
				SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, RGB(0xea, 0x99, 0x1f));
				return (LRESULT)(HBRUSH)(GetStockObject(HOLLOW_BRUSH));
			}
#endif
            else
            {
                break;
            }
        }
        case WM_CLOSE:
        {
            if (g_ThreadHandle)
            {
                MessageBox(g_DialogHwnd, _G(STR_WAIT_PROCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                EndDialog(hWnd, 0);
            }
			WriteCfgIni();
            break;
        }
    }

    return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow)
{
    int i, j;
    WCHAR *Pos = NULL;
    WCHAR CurDir[MAX_PATH];
    const char *checkfile[] =
    {
        "boot\\boot.img",
        "boot\\core.img.xz",
        "ventoy\\ventoy.disk.img.xz",
        "ventoy\\version",
        NULL
    };

    UNREFERENCED_PARAMETER(hPrevInstance);
    
    GetCurrentDirectory(MAX_PATH, CurDir);
    Pos = wcsstr(CurDir, L"\\altexe");
    if (Pos)
    {
        *Pos = 0;
        SetCurrentDirectory(CurDir);
    }

    for (i = 0; checkfile[i]; i++)
    {
        if (!IsFileExist("%s", checkfile[i]))
        {
            for (j = 0; j < 50; j++)
            {
                Log("####### File <%s> not found, did you download it from official website ? ######", checkfile[i]);
            }

            if (IsFileExist("grub\\grub.cfg"))
            {
                MessageBox(NULL, TEXT("Don't run me here, please use the released install package."), TEXT("Error"), MB_OK | MB_ICONERROR);
            }
            else
            {
                MessageBox(NULL, TEXT("Please run under the correct directory!"), TEXT("Error"), MB_OK | MB_ICONERROR);
            }
            return ERROR_NOT_FOUND;
        }
    }

    GetExeVersionInfo(__argv[0]);

	Log("\n##################################################################################\n"		
		"######################### Ventoy2Disk%s %s (%s) #########################\n"
		"##################################################################################",
        current_arch_string(), g_CurVersion, GetLocalVentoyVersion());

    ParseCmdLineOption(lpCmdLine);
    LoadCfgIni();
	

    DumpWindowsVersion();

    Ventoy2DiskInit();

    g_hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DialogProc);

    Ventoy2DiskDestroy();

    return 0;
}
