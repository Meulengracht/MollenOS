/**
 * Copyright 2017, Philip Meulengracht
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
 * Human Input Device Driver (Generic)
 */

#ifndef __HID_KEYCODES_H__
#define __HID_KEYCODES_H__

// KEYBOARD / KEYPAD PAGE
#define HID_KEYCODE_NONE         0
#define HID_KEYCODE_ERROR_RO     1
#define HID_KEYCODE_POSTFAIL     2
#define HID_KEYCODE_ERROR_UN     3
#define HID_KEYCODE_A            4
#define HID_KEYCODE_B            5
#define HID_KEYCODE_C            6
#define HID_KEYCODE_D            7
#define HID_KEYCODE_E            8
#define HID_KEYCODE_F            9
#define HID_KEYCODE_G            10
#define HID_KEYCODE_H            11
#define HID_KEYCODE_I            12
#define HID_KEYCODE_J            13
#define HID_KEYCODE_K            14
#define HID_KEYCODE_L            15
#define HID_KEYCODE_M            16
#define HID_KEYCODE_N            17
#define HID_KEYCODE_O            18
#define HID_KEYCODE_P            19
#define HID_KEYCODE_Q            20
#define HID_KEYCODE_R            21
#define HID_KEYCODE_S            22
#define HID_KEYCODE_T            23
#define HID_KEYCODE_U            24
#define HID_KEYCODE_V            25
#define HID_KEYCODE_W            26
#define HID_KEYCODE_X            27
#define HID_KEYCODE_Y            28
#define HID_KEYCODE_Z            29
#define HID_KEYCODE_1            30
#define HID_KEYCODE_2            31
#define HID_KEYCODE_3            32
#define HID_KEYCODE_4            33
#define HID_KEYCODE_5            34
#define HID_KEYCODE_6            35
#define HID_KEYCODE_7            36
#define HID_KEYCODE_8            37
#define HID_KEYCODE_9            38
#define HID_KEYCODE_0            39
#define HID_KEYCODE_ENTER        40
#define HID_KEYCODE_ESCAPE       41
#define HID_KEYCODE_BACKSPACE    42
#define HID_KEYCODE_TAB          43
#define HID_KEYCODE_SPACE        44
#define HID_KEYCODE_HYPHEN       45
#define HID_KEYCODE_EQUAL        46
#define HID_KEYCODE_LBRACKET     47
#define HID_KEYCODE_RBRACKET     48
#define HID_KEYCODE_BACKSLASH    49
#define HID_KEYCODE_HASH         50
#define HID_KEYCODE_SEMICOLON    51
#define HID_KEYCODE_TICK         52
#define HID_KEYCODE_GRAVE_ACCENT 53
#define HID_KEYCODE_COMMA        54
#define HID_KEYCODE_DOT          55
#define HID_KEYCODE_SLASH        56
#define HID_KEYCODE_CAPSLOCK     57
#define HID_KEYCODE_F1           58
#define HID_KEYCODE_F2           59
#define HID_KEYCODE_F3           60
#define HID_KEYCODE_F4           61
#define HID_KEYCODE_F5           62
#define HID_KEYCODE_F6           63
#define HID_KEYCODE_F7           64
#define HID_KEYCODE_F8           65
#define HID_KEYCODE_F9           66
#define HID_KEYCODE_F10          67
#define HID_KEYCODE_F11          68
#define HID_KEYCODE_F12          69
#define HID_KEYCODE_PRINTSCR     70
#define HID_KEYCODE_SCROLLLOCK   71
#define HID_KEYCODE_PAUSE        72
#define HID_KEYCODE_INSERT       73
#define HID_KEYCODE_HOME         74
#define HID_KEYCODE_PAGEUP       75
#define HID_KEYCODE_DELETE       76
#define HID_KEYCODE_END          77
#define HID_KEYCODE_PAGEDOWN     78
#define HID_KEYCODE_RIGHTARROW   79
#define HID_KEYCODE_LEFTARROW    80
#define HID_KEYCODE_DOWNARROW    81
#define HID_KEYCODE_UPARROW      82
#define HID_KEYCODE_NUMLOCK      83
#define HID_KEYCODE_KP_SLASH     84
#define HID_KEYCODE_KP_MULTIPLY  85
#define HID_KEYCODE_KP_MINUS     86
#define HID_KEYCODE_KP_PLUS      87
#define HID_KEYCODE_KP_ENTER     88
#define HID_KEYCODE_KP_1         89
#define HID_KEYCODE_KP_2         90
#define HID_KEYCODE_KP_3         91
#define HID_KEYCODE_KP_4         92
#define HID_KEYCODE_KP_5         93
#define HID_KEYCODE_KP_6         94
#define HID_KEYCODE_KP_7         95
#define HID_KEYCODE_KP_8         96
#define HID_KEYCODE_KP_9         97
#define HID_KEYCODE_KP_0         98
#define HID_KEYCODE_KP_DOT       99
#define HID_KEYCODE_KP_BACKSLASH 100
#define HID_KEYCODE_APPLICATION  101
#define HID_KEYCODE_POWER        102
#define HID_KEYCODE_KP_EQUAL     103
#define HID_KEYCODE_F13          104
#define HID_KEYCODE_F14          105
#define HID_KEYCODE_F15          106
#define HID_KEYCODE_F16          107
#define HID_KEYCODE_F17          108
#define HID_KEYCODE_F18          109
#define HID_KEYCODE_F19          110
#define HID_KEYCODE_F20          111
#define HID_KEYCODE_F21          112
#define HID_KEYCODE_F22          113
#define HID_KEYCODE_F23          114
#define HID_KEYCODE_F24          115
#define HID_KEYCODE_EXECUTE      116
#define HID_KEYCODE_HELP         117
#define HID_KEYCODE_MENU         118
#define HID_KEYCODE_SELECT       119
#define HID_KEYCODE_STOP         120
#define HID_KEYCODE_AGAIN        121
#define HID_KEYCODE_UNDO         122
#define HID_KEYCODE_CUT          123
#define HID_KEYCODE_COPY         124
#define HID_KEYCODE_PASTE        125
#define HID_KEYCODE_FIND         126
#define HID_KEYCODE_MUTE         127
#define HID_KEYCODE_VOLUME_UP    128
#define HID_KEYCODE_VOLUME_DOWN  129
#define HID_KEYCODE_LOCK_CAPSLOCK 130
#define HID_KEYCODE_LOCK_NUMLOCK  131
#define HID_KEYCODE_LOCK_SCROLLLOCK 132
#define HID_KEYCODE_KP_COMMA      133
#define HID_KEYCODE_KP_EQUAL_SIGN 134
#define HID_KEYCODE_INT_1         135
#define HID_KEYCODE_INT_2         136
#define HID_KEYCODE_INT_3         137
#define HID_KEYCODE_INT_4         138
#define HID_KEYCODE_INT_5         139
#define HID_KEYCODE_INT_6         140
#define HID_KEYCODE_INT_7         141
#define HID_KEYCODE_INT_8         142
#define HID_KEYCODE_INT_9         143
#define HID_KEYCODE_LANG_1        144
#define HID_KEYCODE_LANG_2        145
#define HID_KEYCODE_LANG_3        146
#define HID_KEYCODE_LANG_4        147
#define HID_KEYCODE_LANG_5        148
#define HID_KEYCODE_LANG_6        149
#define HID_KEYCODE_LANG_7        150
#define HID_KEYCODE_LANG_8        151
#define HID_KEYCODE_LANG_9        152
#define HID_KEYCODE_ALT_ERASE     153
#define HID_KEYCODE_SYSREQ        154
#define HID_KEYCODE_CANCEL        155
#define HID_KEYCODE_CLEAR         156
#define HID_KEYCODE_PRIOR         157
#define HID_KEYCODE_RETURN        158
#define HID_KEYCODE_SEPERATOR     159
#define HID_KEYCODE_OUT           160
#define HID_KEYCODE_OPER          161
#define HID_KEYCODE_CLEAR_AGAIN   162
#define HID_KEYCODE_CRSEL         163
#define HID_KEYCODE_EXSEL         164
#define HID_KEYCODE_KP_00         176
#define HID_KEYCODE_KP_000        177
#define HID_KEYCODE_THOUSANDS_SEP 178
#define HID_KEYCODE_DECIMAL_SEP   179
#define HID_KEYCODE_CURRENCY_UNIT 180
#define HID_KEYCODE_CURRENCY_SUBUNIT 181
#define HID_KEYCODE_KP_LPARENTHESIS 182
#define HID_KEYCODE_KP_RPARENTHESIS 183
#define HID_KEYCODE_KP_LBRACKET   184
#define HID_KEYCODE_KP_RBRACKET   185
#define HID_KEYCODE_KP_TAB        186
#define HID_KEYCODE_KP_BACKSPACE  187
#define HID_KEYCODE_KP_A          188
#define HID_KEYCODE_KP_B          189
#define HID_KEYCODE_KP_C          190
#define HID_KEYCODE_KP_D          191
#define HID_KEYCODE_KP_E          192
#define HID_KEYCODE_KP_F          193
#define HID_KEYCODE_KP_XOR        194
#define HID_KEYCODE_KP_CARET      195
#define HID_KEYCODE_KP_PERCENTAGE 196
#define HID_KEYCODE_KP_LESS_THAN  197 // <
#define HID_KEYCODE_KP_MORE_THAN  198 // >
#define HID_KEYCODE_KP_BL_AND     199 // &
#define HID_KEYCODE_KP_AND        200 // &&
#define HID_KEYCODE_KP_BL_OR      201 // |
#define HID_KEYCODE_KP_OR         202 // ||
#define HID_KEYCODE_KP_COLON      203
#define HID_KEYCODE_KP_HASH       204
#define HID_KEYCODE_KP_SPACE      205
#define HID_KEYCODE_KP_AT         206
#define HID_KEYCODE_KP_EXCLAMATION 207
#define HID_KEYCODE_KP_MEM_STORE  208
#define HID_KEYCODE_KP_MEM_RECALL 209
#define HID_KEYCODE_KP_MEM_CLEAR  210
#define HID_KEYCODE_KP_MEM_ADD    211
#define HID_KEYCODE_KP_MEM_SUB    212
#define HID_KEYCODE_KP_MEM_MULT   213
#define HID_KEYCODE_KP_MEM_DIV    214
#define HID_KEYCODE_KP_MEM_NEGATE 215
#define HID_KEYCODE_KP_CLEAR      216
#define HID_KEYCODE_KP_CLEAR_ENTRY 217
#define HID_KEYCODE_KP_BINARY     218
#define HID_KEYCODE_KP_OCTAL      219
#define HID_KEYCODE_KP_DECIMAL    220
#define HID_KEYCODE_KP_HEX        221
#define HID_KEYCODE_LCONTROL      224
#define HID_KEYCODE_LSHIFT        225
#define HID_KEYCODE_LALT          226
#define HID_KEYCODE_LGUI          227
#define HID_KEYCODE_RCONTROL      228
#define HID_KEYCODE_RSHIFT        229
#define HID_KEYCODE_RALT          230
#define HID_KEYCODE_RGUI          231

#endif //!__HID_KEYCODES_H__
