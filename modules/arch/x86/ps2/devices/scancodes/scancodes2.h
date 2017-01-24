/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86 PS2 Controller (Keyboard) - ScancodeSet 2 Driver
 * http://wiki.osdev.org/PS2
 */

#ifndef _KEYBOARD_SCANCODESET2_H_
#define _KEYBOARD_SCANCODESET2_H_

/* Table */
typedef enum _ScancodeSet2
{
	/* Special Codes */
	EXTENDED	= 0xE0,
	RELEASED	= 0xF0,

	/* Standard Codes */
	F9			= 0x1,
	F5			= 0x3,
	F3			= 0x4,
	F1			= 0x5,
	F2			= 0x6,
	F12			= 0x7,
	F10			= 0x9,
	F8			= 0xA,
	F6			= 0xB,
	F4			= 0xC,
	TAB			= 0xD,
	BTICK		= 0xE,
	LALT		= 0x11,
	LSHIFT		= 0x12,
	LCONTROL	= 0x14,
	Q			= 0x15,
	D1			= 0x16,
	Z			= 0x1A,
	S			= 0x1B,
	A			= 0x1C,
	W			= 0x1D,
	D2			= 0x1E,
	C			= 0x21,
	X			= 0x22,
	D			= 0x23,
	E			= 0x24,
	D4			= 0x25,
	D3			= 0x26,
	SPACE		= 0x29,
	V			= 0x2A,
	F			= 0x2B,
	T			= 0x2C,
	R			= 0x2D,
	D5			= 0x2E,
	N			= 0x31,
	B			= 0x32,
	H			= 0x33,
	G			= 0x34,
	Y			= 0x35,
	D6			= 0x36,
	M			= 0x3A,
	J			= 0x3B,
	U			= 0x3C,
	D7			= 0x3D,
	D8			= 0x3E,
	COMMA		= 0x41,
	K			= 0x42,
	I			= 0x43,
	O			= 0x44,
	D0			= 0x45,
	D9			= 0x46,
	DOT			= 0x49,
	SLASH		= 0x4A,
	L			= 0x4B,
	SEMICOL		= 0x4C,
	P			= 0x4D,
	HYPHEN		= 0x4E,
	TICK		= 0x52, /* ' */
	LBRACKET	= 0x54,
	EQUAL		= 0x55,
	CAPSLOCK	= 0x58,
	RSHIFT		= 0x59,
	ENTER		= 0x5A,
	RBRACKET	= 0x5B,
	BACKSLASH	= 0x5D,
	BACKSPACE	= 0x66,
	KEYPAD1		= 0x69,
	KEYPAD4		= 0x6B,
	KEYPAD7		= 0x6C,
	KEYPAD0		= 0x70,
	KEYPADDOT	= 0x71,
	KEYPAD2		= 0x72,
	KEYPAD5		= 0x73,
	KEYPAD6		= 0x74,
	KEYPAD8		= 0x75,
	ESCAPE		= 0x76,
	NUMLOCK		= 0x77,
	F11			= 0x78,
	KEYPADPLUS	= 0x79,
	KEYPAD3		= 0x7A,
	KEYPADMINUS	= 0x7B,
	KEYPADMUL	= 0x7C,
	KEYPAD9		= 0x7D,
	SCROLLLOCK	= 0x7E,
	F7			= 0x83,

	/* Extended Codes */
	/* 0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77 -> Pause */
	WwwSearch	= 0xE010,
	RALT		= 0xE011,
	RCONTROL	= 0xE014,
	PrevTrack	= 0xE015,
	WwwFav		= 0xE018,
	LGUI		= 0xE01F,
	WwwRefresh	= 0xE020,
	VolDown		= 0xE021,
	Mute		= 0xE023,
	RGUI		= 0xE027,
	WwwStop		= 0xE028,
	Calculator	= 0xE02B,
	Apps		= 0xE02F,
	WwwForward	= 0xE030,
	VolUp		= 0xE032,
	PlayPause	= 0xE034,
	Power		= 0xE037,
	WwwBack		= 0xE038,
	WwwHome		= 0xE03A,
	Stop		= 0xE03B,
	Sleep		= 0xE03F,
	MyPc		= 0xE040,
	Email		= 0xE048,
	KEYPADBACKSLASH	= 0xE04A,
	NextTrack	= 0xE04D,
	MediaSelect	= 0xE050,
	KEYPADENTER	= 0xE05A,
	Wake		= 0xE05E,
	END			= 0xE069,
	LEFTARROW	= 0xE06B,
	HOME		= 0xE06C,
	INSERT		= 0xE070,
	DELETE		= 0xE071,
	DOWNARROW	= 0xE072,
	RIGHTARROW	= 0xE074,
	UPARROW		= 0xE075,
	PAGEDOWN	= 0xE07A,
	PAGEUP		= 0xE07D
} ScancodeSet2;

#endif //!_KEYBOARD_SCANCODESET2_H_
