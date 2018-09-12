//==========================================================================
// Mouse Injector Plugin
//==========================================================================
// Copyright (C) 2018 Carnivorous
// All rights reserved.
//
// Mouse Injector is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, visit http://www.gnu.org/licenses/gpl-2.0.html
//==========================================================================
#include <math.h>
#include "../global.h"
#include "../device.h"
#include "../maindll.h"
#include "game.h"
#include "memory.h"

#define GUNAIMLIMIT 6.879164696 // 0x40DC221E
#define CROSSHAIRLIMIT 5.159373283 // 0x40A51996
#define TANKXROTATIONLIMIT 6.283185005 // 0x40C90FDA
// GOLDENEYE ADDRESSES - OFFSET ADDRESSES BELOW (REQUIRES PLAYERBASE TO USE)
#define GE_crouchflag 0x800D2FFC - 0x800D2F60
#define GE_deathflag 0x800D3038 - 0x800D2F60
#define GE_camx 0x800D30A8 - 0x800D2F60
#define GE_camy 0x800D30B8 - 0x800D2F60
#define GE_fov 0x800D4124 - 0x800D2F60
#define GE_crosshairx 0x800D3F50 - 0x800D2F60
#define GE_crosshairy 0x800D3F54 - 0x800D2F60
#define GE_watch 0x800D3128 - 0x800D2F60
#define GE_gunx 0x800D3F64 - 0x800D2F60
#define GE_guny 0x800D3F68 - 0x800D2F60
#define GE_aimingflag 0x800D3084 - 0x800D2F60
#define GE_currentweapon 0x800D37D0 - 0x800D2F60
#define GE_multipausemenu 0x800A9D24 - 0x800A7360
// STATIC ADDRESSES BELOW
#define BONDDATA (unsigned int)EMU_ReadInt(0x8007A0B0) // playable character point (used for all players)
#define GE_camera 0x80036494 // camera flag (0 = multiplayer, 1 = map overview, 2 = start flyby, 3 = in flyby, 4 = player in control, 5 = trigger restart map)
#define GE_exit 0x800364B0 // exit flag (0 = disable controls, 1 = enable controls)
#define GE_pause 0x80048370 // pause flag (1 = GE is paused)
#define GE_menupage 0x8002A8C0 // menu page id
#define GE_menux 0x8002A908 // crosshair menu cursor x axis
#define GE_menuy 0x8002A90C // crosshair menu cursor y axis
#define GE_tankxrot 0x80036484 // tank x rotation
#define GE_tankflag 0x80036448 // tank flag (0 = walking, 1 = in-tank)
#define GE_matchended 0x8008C700 // multiplayer match flag
#define GE_defaultfov 0x000B78BC // field of view default (rom)
#define GE_defaultfovinit 0x000CF838 // field of view init value (rom)
#define GE_defaultfovzoom 0x000B78DC // field of view default for zoom (rom)
#define GE_defaultzoomspeed 0x8004F1A8 // default zoom speed
#define GE_showcrosshair 0x0009F128 // show crosshair code (rom)
#define GE_crosshairimage 0x0029DE8C // crosshair image (rom)
#define GE_introcounter 0x8002A8CC // counter for intro
#define GE_seenintroflag 0x8002A930 // seen intro flag

static unsigned int playerbase[4] = {0, 0, 0, 0}; // current player's bonddata address
static int safetoduck[4] = {1, 1, 1, 1}, safetostand[4] = {0, 0, 0, 0}, crouchstance[4] = {0, 0, 0, 0}; // used for crouch toggle changes (limits tick-tocking)
static float crosshairposx[4], crosshairposy[4], aimx[4], aimy[4];

static int GE_Status(void);
static void GE_DetectMap(void);
static void GE_Inject(void);
static void GE_Crouch(const int player);
#define GE_ResetCrouchToggle(X) safetoduck[X] = 1, safetostand[X] = 0, crouchstance[X] = 0 // reset crouch toggle bind
static void GE_AimMode(const int player, const int aimingflag, const float fov);
static void GE_Controller(void);
static void GE_InjectHacks(void);
static void GE_SortArray(int *a, const int n);
static void GE_Quit(void);

static const GAMEDRIVER GAMEDRIVER_INTERFACE =
{
	"GoldenEye 007",
	GE_Status,
	GE_Inject,
	GE_Quit
};

const GAMEDRIVER *GAME_GOLDENEYE007 = &GAMEDRIVER_INTERFACE;

//==========================================================================
// Purpose: returns a value, which is then used to check what game is running in game.c
// Q: What is happening here?
// A: We look up some static addresses and if the values are within the expected ranges the program can assume that the game is currently running
//==========================================================================
static int GE_Status(void)
{
	const int ge_camera = EMU_ReadInt(GE_camera), ge_page = EMU_ReadInt(GE_menupage), ge_pause = EMU_ReadInt(GE_pause), ge_exit = EMU_ReadInt(GE_exit);
	const float ge_crosshairx = EMU_ReadFloat(GE_menux), ge_crosshairy = EMU_ReadFloat(GE_menuy);
	if(ge_camera >= 0 && ge_camera <= 10 && ge_page >= -1 && ge_page <= 25 && ge_pause >= 0 && ge_pause <= 1 && ge_exit >= 0 && ge_exit <= 1 && ge_crosshairx >= 20 && ge_crosshairx <= 420 && ge_crosshairy >= 20 && ge_crosshairy <= 310) // if GoldenEye 007 is current game
		return 1;
	return 0;
}
//==========================================================================
// Purpose: searches for the current map memory locations
// Changes Globals: playerbase, safetoduck, safetostand, crouchstance
//==========================================================================
static void GE_DetectMap(void)
{
	if(playerbase[PLAYER1] != BONDDATA && (EMU_ReadInt(GE_camera) == 1 || EMU_ReadInt(GE_camera) == 4 || EMU_ReadInt(GE_camera) == 9))
	{
		unsigned int pointerdump[128];
		for(int player = PLAYER1; player < ALLPLAYERS; player++)
		{
			playerbase[player] = 0; // reset playerbase for all players
			GE_ResetCrouchToggle(player); // reset crouch toggle on new map
		}
		for(int index = 0; index < 128; index++)
		{
			pointerdump[index] = BONDDATA;
			DEV_Sleep(1);
		}
		GE_SortArray((int *)pointerdump, sizeof pointerdump / sizeof pointerdump[0]);
		playerbase[PLAYER1] = pointerdump[0];
		int playerfound = PLAYER1;
		for(int index = 0; index < 128; index++)
		{
			if(playerbase[PLAYER1] != pointerdump[index] && playerfound == PLAYER1)
			{
				playerbase[PLAYER2] = pointerdump[index];
				playerfound = PLAYER2;
			}
			else if(playerbase[PLAYER2] != pointerdump[index] && playerfound == PLAYER2)
			{
				playerbase[PLAYER3] = pointerdump[index];
				playerfound = PLAYER3;
			}
			else if(playerbase[PLAYER3] != pointerdump[index] && playerfound == PLAYER3)
			{
				playerbase[PLAYER4] = pointerdump[index];
				break;
			}
		}
	}
}
//==========================================================================
// Purpose: calculate mouse movement and inject into current game
// Changes Globals: safetoduck, safetostand, crouchstance
// Q: Could you explain !aimingflag ? 10.0f : 40.0f
// A: While the player is aiming weapon sway will be reduced by 75%. While this is not in the original design of GE/PD I feel it gives a legitimate advantage for aiming instead of only making the crosshair visible
// Q: Could you explain basefov = fov > 60.0f ? (float)overridefov : 60.0f
// A: For weapons that zoom, the fov is lower than 60 - when this happens we will compute our calculations using 60 as a base. This is to prevent weapons from becoming too sluggish to move while zoomed with a high override fov
// Q: Could you explain if(aimingflag) gunx /= emuoverclock ? 1.03f : 1.07f, crosshairx /= emuoverclock ? 1.03f : 1.07f;
// A: GE_InjectHacks() disables the engine from overwriting the crosshair pos and gun rot - this is so aiming is jitter free. But it removed the auto centering code while aiming. If the player turns off cursor aiming for GE, the crosshair and gun will no longer move back to the center (it'll float to the corners of the screen). This if statement will emulate the centering code. The emuoverclock condition will make the scale the same - regardless of overclock or stock (inject exec at higher tickrate if overclocked).
//==========================================================================
static void GE_Inject(void)
{
	GE_DetectMap();
	if(EMU_ReadInt(GE_menupage) < 1) // hacks can only be injected at boot sequence before code blocks are cached, so inject until the main menu
		GE_InjectHacks();
	for(int player = PLAYER1; player < ALLPLAYERS; player++)
	{
		if(PROFILE[player].SETTINGS[CONFIG] == DISABLED) // bypass disabled players
			continue;
		const int camera = EMU_ReadInt(GE_camera);
		const int dead = EMU_ReadInt(playerbase[player] + GE_deathflag);
		const int watch = EMU_ReadInt(playerbase[player] + GE_watch);
		const int exit = EMU_ReadInt(GE_exit);
		const int pause = EMU_ReadInt(GE_pause);
		const int aimingflag = EMU_ReadInt(playerbase[player] + GE_aimingflag);
		const int menupage = EMU_ReadInt(GE_menupage);
		const int tankflag = EMU_ReadInt(GE_tankflag);
		const int mproundend = EMU_ReadInt(GE_matchended);
		const int mppausemenu = EMU_ReadInt(playerbase[player] + GE_multipausemenu);
		const int cursoraimingflag = PROFILE[player].SETTINGS[GEAIMMODE] && aimingflag ? 1 : 0;
		const float fov = EMU_ReadFloat(playerbase[player] + GE_fov);
		const float basefov = fov > 60.0f ? (float)overridefov : 60.0f;
		const float mouseaccel = PROFILE[player].SETTINGS[ACCELERATION] ? sqrt(DEVICE[player].XPOS * DEVICE[player].XPOS + DEVICE[player].YPOS * DEVICE[player].YPOS) / TICKRATE / 12.0f * PROFILE[player].SETTINGS[ACCELERATION] : 0;
		const float sensitivity = PROFILE[player].SETTINGS[SENSITIVITY] / 40.0f * fmax(mouseaccel, 1);
		const float gunsensitivity = sensitivity * (PROFILE[player].SETTINGS[CROSSHAIR] / 2.5f);
		float camx = EMU_ReadFloat(playerbase[player] + GE_camx), camy = EMU_ReadFloat(playerbase[player] + GE_camy);
		if(camx >= 0 && camx <= 360 && camy >= -90 && camy <= 90 && fov >= 1 && fov <= 120 && dead == 0 && watch == 0 && pause == 0 && (camera == 4 || camera == 0) && exit == 1 && menupage == 11 && !mproundend && !mppausemenu) // if safe to inject
		{
			GE_AimMode(player, cursoraimingflag, fov / basefov);
			if(!tankflag) // player is on foot
			{
				GE_Crouch(player); // only allow crouching if player is not in tank
				if(!cursoraimingflag) // if not aiming (or geaimmode is off)
					camx += DEVICE[player].XPOS / 10.0f * sensitivity * (fov / basefov); // regular mouselook calculation
				else
					camx += aimx[player] * (fov / basefov); // scroll screen with aimx/aimy
				if(camx < 0)
					camx += 360;
				else if(camx >= 360)
					camx -= 360;
				EMU_WriteFloat(playerbase[player] + GE_camx, camx);
			}
			else // player is in tank
			{
				GE_ResetCrouchToggle(player); // reset crouch toggle if in tank
				float tankx = EMU_ReadFloat(GE_tankxrot);
				if(!cursoraimingflag)
					tankx += DEVICE[player].XPOS / 10.0f * sensitivity / (360 / TANKXROTATIONLIMIT * 2.5) * (fov / basefov);
				else
					tankx += aimx[player] / (360 / TANKXROTATIONLIMIT * 2.5) * (fov / basefov);
				if(tankx < 0)
					tankx += TANKXROTATIONLIMIT;
				else if(tankx >= TANKXROTATIONLIMIT)
					tankx -= TANKXROTATIONLIMIT;
				EMU_WriteFloat(GE_tankxrot, tankx);
			}
			if(!cursoraimingflag)
				camy += (!PROFILE[player].SETTINGS[INVERTPITCH] ? -DEVICE[player].YPOS : DEVICE[player].YPOS) / 10.0f * sensitivity * (fov / basefov);
			else
				camy += -aimy[player] * (fov / basefov);
			camy = ClampFloat(camy, tankflag ? -20 : -90, 90); // tank limits player from looking down -20
			EMU_WriteFloat(playerbase[player] + GE_camy, camy);
			if(PROFILE[player].SETTINGS[CROSSHAIR] && !cursoraimingflag) // if crosshair movement is enabled and player isn't aiming (don't calculate weapon movement while the player is in aim mode)
			{
				if(!tankflag)
				{
					float gunx = EMU_ReadFloat(playerbase[player] + GE_gunx), crosshairx = EMU_ReadFloat(playerbase[player] + GE_crosshairx); // after camera x and y have been calculated and injected, calculate the gun/crosshair movement
					gunx += DEVICE[player].XPOS / (!aimingflag ? 10.0f : 40.0f) * gunsensitivity * (fov / basefov) * 0.019f;
					crosshairx += DEVICE[player].XPOS / (!aimingflag ? 10.0f : 40.0f) * gunsensitivity * (fov / 4 / (basefov / 4)) * 0.01912f;
					if(aimingflag) // emulate cursor moving back to the center
						gunx /= emuoverclock ? 1.03f : 1.07f, crosshairx /= emuoverclock ? 1.03f : 1.07f;
					gunx = ClampFloat(gunx, -GUNAIMLIMIT, GUNAIMLIMIT);
					crosshairx = ClampFloat(crosshairx, -CROSSHAIRLIMIT, CROSSHAIRLIMIT);
					EMU_WriteFloat(playerbase[player] + GE_gunx, gunx);
					EMU_WriteFloat(playerbase[player] + GE_crosshairx, crosshairx);
				}
				if((!tankflag && camy > -90 || tankflag && camy > -20) && camy < 90) // only allow player's gun to pitch within a valid range
				{
					float guny = EMU_ReadFloat(playerbase[player] + GE_guny), crosshairy = EMU_ReadFloat(playerbase[player] + GE_crosshairy);
					guny += (!PROFILE[player].SETTINGS[INVERTPITCH] ? DEVICE[player].YPOS : -DEVICE[player].YPOS) / (!aimingflag ? 40.0f : 20.0f) * gunsensitivity * (fov / basefov) * 0.025f;
					crosshairy += (!PROFILE[player].SETTINGS[INVERTPITCH] ? DEVICE[player].YPOS : -DEVICE[player].YPOS) / (!aimingflag ? 40.0f : 20.0f) * gunsensitivity * (fov / 4 / (basefov / 4)) * 0.0225f;
					if(aimingflag)
						guny /= emuoverclock ? 1.15f : 1.35f, crosshairy /= emuoverclock ? 1.15f : 1.35f;
					guny = ClampFloat(guny, -GUNAIMLIMIT, GUNAIMLIMIT);
					crosshairy = ClampFloat(crosshairy, -CROSSHAIRLIMIT, CROSSHAIRLIMIT);
					EMU_WriteFloat(playerbase[player] + GE_guny, guny);
					EMU_WriteFloat(playerbase[player] + GE_crosshairy, crosshairy);
				}
			}
		}
		else if(player == PLAYER1 && menupage != 11 && menupage != 23) // if user is in menu (only player 1 can control menu)
		{
			float menucrosshairx = EMU_ReadFloat(GE_menux), menucrosshairy = EMU_ReadFloat(GE_menuy);
			menucrosshairx += DEVICE[player].XPOS / 10.0f * sensitivity * 6;
			menucrosshairy += DEVICE[player].YPOS / 10.0f * sensitivity * (400.0f / 290.0f * 6); // y is a little weaker then x in the menu so add more power to make it feel even with x axis
			menucrosshairx = ClampFloat(menucrosshairx, 20, 420);
			menucrosshairy = ClampFloat(menucrosshairy, 20, 310);
			EMU_WriteFloat(GE_menux, menucrosshairx);
			EMU_WriteFloat(GE_menuy, menucrosshairy);
		}
		if(dead || menupage != 11) // if player is dead or in menu, reset crouch toggle
			GE_ResetCrouchToggle(player);
	}
	GE_Controller(); // set controller data
}
//==========================================================================
// Purpose: crouching function for GoldenEye (2 = stand, 0 = duck)
// Changes Globals: safetoduck, crouchstance, safetostand
//==========================================================================
static void GE_Crouch(const int player)
{
	int crouchheld = DEVICE[player].BUTTONPRIM[CROUCH] || DEVICE[player].BUTTONSEC[CROUCH];
	if(PROFILE[player].SETTINGS[CROUCHTOGGLE]) // check and change player stance
	{
		if(safetoduck[player] && crouchheld) // standing to ducking
			safetoduck[player] = 0, crouchstance[player] = 1;
		if(!safetoduck[player] && !crouchheld) // crouch is no longer being held, ready to stand
			safetostand[player] = 1;
		if(safetostand[player] && crouchheld) // standing up
			safetoduck[player] = 1, crouchstance[player] = 0;
		if(safetostand[player] && safetoduck[player] && !crouchheld) // crouch key not active, ready to change toggle
			safetostand[player] = 0;
		crouchheld = crouchstance[player];
	}
	if(crouchheld) // user is holding down the crouch button
		EMU_WriteInt(playerbase[player] + GE_crouchflag, 0); // set in-game crouch flag to lowest point
	else // player isn't holding down crouch
		EMU_WriteInt(playerbase[player] + GE_crouchflag, 2); // force standing flag value
}
//==========================================================================
// Purpose: replicate the original aiming system, uses aimx/y to move screen when crosshair is on border of screen
// Changes Globals: crosshairposx, crosshairposy, aimx, aimy
//==========================================================================
static void GE_AimMode(const int player, const int aimingflag, const float fov)
{
	const float crosshairx = EMU_ReadFloat(playerbase[player] + GE_crosshairx), crosshairy = EMU_ReadFloat(playerbase[player] + GE_crosshairy), offsetpos[2][33] = {{0, 0, 0, 0, 0.1625, 0.1625, 0.15, 0.5, 0.8, 0.4, 0.5, 0.5, 0.48, 0.9, 0.25, 0.6, 0.6, 0.7, 0.25, 0.15, 0.1625, 0.1625, 0.5, 0.5, 0.9, 0.9, 0, 0, 0, 0, 0, 0.4}, {0, 0, 0, 0, 0.1, 0.1, 0.2, 0.325, 1, 0.3, 0.425, 0.425, 0.45, 0.95, 0.1, 0.55, 0.5, 0.7, 0.25, 0.1, 0.1, 0.1, 0.275, 1, 0.9, 0.8, 0, 0, 0, 0, 0, 0.25}}; // table of X/Y offset for weapons
	const int currentweapon = EMU_ReadInt(playerbase[player] + GE_currentweapon);
	const float threshold = 0.72f, speed = 475.f, sensitivity = 292.f;
	if(aimingflag) // if player is aiming
	{
		const float mouseaccel = PROFILE[player].SETTINGS[ACCELERATION] ? sqrt(DEVICE[player].XPOS * DEVICE[player].XPOS + DEVICE[player].YPOS * DEVICE[player].YPOS) / TICKRATE / 12.0f * PROFILE[player].SETTINGS[ACCELERATION] : 0;
		crosshairposx[player] += DEVICE[player].XPOS / 10.0f * (PROFILE[player].SETTINGS[SENSITIVITY] / sensitivity) * fmax(mouseaccel, 1); // calculate the crosshair position
		crosshairposy[player] += (!PROFILE[player].SETTINGS[INVERTPITCH] ? DEVICE[player].YPOS : -DEVICE[player].YPOS) / 10.0f * (PROFILE[player].SETTINGS[SENSITIVITY] / sensitivity) * fmax(mouseaccel, 1);
		crosshairposx[player] = ClampFloat(crosshairposx[player], -CROSSHAIRLIMIT, CROSSHAIRLIMIT); // apply clamp then inject
		crosshairposy[player] = ClampFloat(crosshairposy[player], -CROSSHAIRLIMIT, CROSSHAIRLIMIT);
		EMU_WriteFloat(playerbase[player] + GE_crosshairx, crosshairposx[player]);
		EMU_WriteFloat(playerbase[player] + GE_crosshairy, crosshairposy[player]);
		EMU_WriteFloat(playerbase[player] + GE_gunx, crosshairposx[player] * (1.11f + (currentweapon >= 0 && currentweapon <= 32 ? offsetpos[0][currentweapon] : 0.15f) * 1.5f) + fov - 1); // calculate and inject the gun angles (uses pre-made pos table or if unknown weapon use fail-safe value)
		EMU_WriteFloat(playerbase[player] + GE_guny, crosshairposy[player] * (1.11f + (currentweapon >= 0 && currentweapon <= 32 ? offsetpos[1][currentweapon] : 0) * 1.5f) + fov - 1);
		if(crosshairx > 0 && crosshairx / CROSSHAIRLIMIT > threshold) // if crosshair is within threshold of the border then calculate a linear scrolling speed and enable mouselook
			aimx[player] = (crosshairx / CROSSHAIRLIMIT - threshold) * speed * TIMESTEP;
		else if(crosshairx < 0 && crosshairx / CROSSHAIRLIMIT < -threshold)
			aimx[player] = (crosshairx / CROSSHAIRLIMIT + threshold) * speed * TIMESTEP;
		else
			aimx[player] = 0;
		if(crosshairy > 0 && crosshairy / CROSSHAIRLIMIT > threshold)
			aimy[player] = (crosshairy / CROSSHAIRLIMIT - threshold) * speed * TIMESTEP;
		else if(crosshairy < 0 && crosshairy / CROSSHAIRLIMIT < -threshold)
			aimy[player] = (crosshairy / CROSSHAIRLIMIT + threshold) * speed * TIMESTEP;
		else
			aimy[player] = 0;
	}
	else // player is not aiming so reset crosshairposxy values
		crosshairposx[player] = crosshairx, crosshairposy[player] = crosshairy;
}
//==========================================================================
// Purpose: calculate and send emulator key combo
//==========================================================================
static void GE_Controller(void)
{
	for(int player = PLAYER1; player < ALLPLAYERS; player++)
	{
		CONTROLLER[player].U_CBUTTON = DEVICE[player].BUTTONPRIM[FORWARDS] || DEVICE[player].BUTTONSEC[FORWARDS];
		CONTROLLER[player].D_CBUTTON = DEVICE[player].BUTTONPRIM[BACKWARDS] || DEVICE[player].BUTTONSEC[BACKWARDS];
		CONTROLLER[player].L_CBUTTON = DEVICE[player].BUTTONPRIM[STRAFELEFT] || DEVICE[player].BUTTONSEC[STRAFELEFT];
		CONTROLLER[player].R_CBUTTON = DEVICE[player].BUTTONPRIM[STRAFERIGHT] || DEVICE[player].BUTTONSEC[STRAFERIGHT];
		CONTROLLER[player].Z_TRIG = DEVICE[player].BUTTONPRIM[FIRE] || DEVICE[player].BUTTONSEC[FIRE] || DEVICE[player].BUTTONPRIM[PREVIOUSWEAPON] || DEVICE[player].BUTTONSEC[PREVIOUSWEAPON];
		CONTROLLER[player].R_TRIG = DEVICE[player].BUTTONPRIM[AIM] || DEVICE[player].BUTTONSEC[AIM];
		CONTROLLER[player].A_BUTTON = DEVICE[player].BUTTONPRIM[ACCEPT] || DEVICE[player].BUTTONSEC[ACCEPT] || DEVICE[player].BUTTONPRIM[PREVIOUSWEAPON] || DEVICE[player].BUTTONSEC[PREVIOUSWEAPON] || DEVICE[player].BUTTONPRIM[NEXTWEAPON] || DEVICE[player].BUTTONSEC[NEXTWEAPON];
		CONTROLLER[player].B_BUTTON = DEVICE[player].BUTTONPRIM[CANCEL] || DEVICE[player].BUTTONSEC[CANCEL];
		CONTROLLER[player].START_BUTTON = DEVICE[player].BUTTONPRIM[START] || DEVICE[player].BUTTONSEC[START];
		DEVICE[player].ARROW[0] = (DEVICE[player].BUTTONPRIM[UP] || DEVICE[player].BUTTONSEC[UP]) ? 127 : 0;
		DEVICE[player].ARROW[1] = (DEVICE[player].BUTTONPRIM[DOWN] || DEVICE[player].BUTTONSEC[DOWN]) ? -127 : 0;
		DEVICE[player].ARROW[2] = (DEVICE[player].BUTTONPRIM[LEFT] || DEVICE[player].BUTTONSEC[LEFT]) ? -127 : 0;
		DEVICE[player].ARROW[3] = (DEVICE[player].BUTTONPRIM[RIGHT] || DEVICE[player].BUTTONSEC[RIGHT]) ? 127 : 0;
		CONTROLLER[player].X_AXIS = DEVICE[player].ARROW[0] + DEVICE[player].ARROW[1];
		CONTROLLER[player].Y_AXIS = DEVICE[player].ARROW[2] + DEVICE[player].ARROW[3];
	}
	if(EMU_ReadInt(GE_menupage) != 11 && !CONTROLLER[PLAYER1].B_BUTTON) // pressing aim will act like the back button (only in menus and for player 1)
		CONTROLLER[PLAYER1].B_BUTTON = DEVICE[PLAYER1].BUTTONPRIM[AIM] || DEVICE[PLAYER1].BUTTONSEC[AIM];
}
//==========================================================================
// Purpose: inject hacks into rom before code has been cached
//==========================================================================
static void GE_InjectHacks(void)
{
	const int addressarray[27] = {0x000B7EA0, 0x000B7EB8, 0x0009C7F8, 0x0009C7FC, 0x0009C80C, 0x0009C810, 0x0009C998, 0x0009C99C, 0x0009C9AC, 0x0009C9B0, 0x000AE4DC, 0x000AE4E0, 0x000AE4E4, 0x000AE4E8, 0x000AE4EC, 0x000AE4F0, 0x000AE4F4, 0x000AE4F8, 0x000AE4FC, 0x000AE500, 0x000AE504, 0x000AE508, 0x000AE50C, 0x000AE510, 0x000AE514, 0x000AE518, 0x000AE51C}, codearray[27] = {0x00000000, 0x00000000, 0x0BC1E66B, 0x460C5100, 0x0BC1E66F, 0x460E3280, 0x0BC1E673, 0x460C4100, 0x0BC1E677, 0x460E5200, 0x8C590124, 0x53200001, 0xE4440FF0, 0x0BC19F34, 0x8C590124, 0x53200001, 0xE44A0FF4, 0x0BC19F39, 0x8C590124, 0x53200001, 0xE4441004, 0x0BC19F9C, 0x8C590124, 0x53200001, 0xE4481008, 0x0BC19FA1, 0x00000000}; // disable autostand code, add branch to crosshair code so cursor aiming mode is absolute (without jitter)
	for(int index = 0; index < 27; index++) // inject code array
		EMU_WriteROM(addressarray[index], codearray[index]);
	if(overridefov != 60) // override default fov
	{
		float newfov = overridefov;
		unsigned int unsignedinteger = *(unsigned int *)(float *)(&newfov);
		EMU_WriteROM(GE_defaultfov, 0x3C010000 + (short)(unsignedinteger / 0x10000));
		EMU_WriteROM(GE_defaultfovinit, 0x3C010000 + (short)(unsignedinteger / 0x10000));
		EMU_WriteROM(GE_defaultfovzoom, 0x3C010000 + (short)(unsignedinteger / 0x10000));
		if(overridefov > 60)
			EMU_WriteFloat(GE_defaultzoomspeed, (overridefov - 60) * ((1.7f - 0.909091f) / 60.0f) + 0.909091f); // adjust zoom speed default (0.909091 default, 1.7 max)
	}
	if(geshowcrosshair) // inject show crosshair hack
	{
		EMU_WriteROM(GE_showcrosshair, 0x00007025); // replace lw $t6, 0x1128 ($v0) (8C4E1128) with or $t6, $r0, $r0 (00007025)
		if(EMU_ReadROM(0x28) != 0x45522020) // if rom isn't Goldfinger 64 (why? because GF64 replaced beta crosshair image with ammo icon)
			EMU_WriteROM(GE_crosshairimage, 0x000008BD); // replace crosshair image with beta crosshair
	}
	if(CONTROLLER[PLAYER1].Z_TRIG && CONTROLLER[PLAYER1].R_TRIG) // skip intros if holding down fire + aim
	{
		EMU_WriteInt(GE_introcounter, 0x00001000);
		EMU_WriteInt(GE_seenintroflag, 0);
	}
}
//==========================================================================
// Purpose: sorting function for pointer scanning
//==========================================================================
static void GE_SortArray(int *a, const int n)
{
	if(n < 2)
		return;
	int p = a[n / 2];
	int *l = a;
	int *r = a + n - 1;
	while(l <= r)
	{
		while(*l < p)
			l++;
		while(*r > p)
			r--;
		if(l <= r)
		{
			int t = *l;
			*l++ = *r;
			*r-- = t;
		}
	}
	GE_SortArray(a, r - a + 1);
	GE_SortArray(l, a + n - l);
}
//==========================================================================
// Purpose: run when emulator closes rom
// Changes Globals: playerbase, safetoduck, safetostand, crouchstance
//==========================================================================
static void GE_Quit(void)
{
	for(int player = PLAYER1; player < ALLPLAYERS; player++)
	{
		playerbase[player] = 0;
		GE_ResetCrouchToggle(player);
	}
}