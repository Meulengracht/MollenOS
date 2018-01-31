/* The Macia Language (MACIA)
*
* Copyright 2016, Philip Meulengracht
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
* Macia - Scanner (Lexer)
*/

/* Includes */
#include "../shared/stringbuffer.h"
#include "scanner.h"

/* C-Library */
#include <cctype>
#include <cstdio>

/* Initialize variables 
 * and clear out the elements just to be sure */
Scanner::Scanner() {
	m_lElements.clear();
}

/* Go through list, delete elements 
 * and clear list */
Scanner::~Scanner() {
	
}

/* Parses a file and converts 
 * it into a stream of tokens for use by the
 * parser */
int Scanner::Scan(char *Data, size_t Length)
{
	/* Some state variables */
	long CharPos = 1;
	int LineNo = 1;
	size_t Count = 0;

	/* Iterate through data */
	while (Count < Length) 
	{
		/* Extract character */
		char Character = Data[Count];

		/* Handle the types of characters 
		 * that we don't care about */
		if (isspace(Character)) {
			if (Character == '\n') {
				LineNo++;
				CharPos = 0;
			}

			/* Increase, go */
			CharPos++;
			goto NextScan;
		}

		/* Check for comments first */
		if (Character == '/'
			&& ((Count + 1) < Length)
			&& Data[Count + 1] == '/') {
			/* This is a comment line */

			/* We need a string buffer for this
			* to append */
			StringBuffer_t *Sb = GetStringBuffer();

			/* Consume the comment line tokens */
			Count += 2;
			Character = Data[Count];

			/* Keep iterating! 
			 * Consume everything untill we reach a newline */
			while (Character != '\n') {
				Sb->Append(Sb, Character);

				/* Consume -> Next */
				Count++;
				CharPos++;
				Character = Data[Count];
			}

			/* Create the token */
			CreateElement(CommentLine, Sb->ToString(Sb), LineNo, CharPos);

			/* Cleanup */
			Sb->Dispose(&Sb);

			/* Go one back */
			Count--;
		}
		else if (Character == '/'
			&& ((Count + 1) < Length)
			&& Data[Count + 1] == '*') {

			/* This is a comment block */
			int TempLineIncrease = 0;

			/* We need a string buffer for this
			* to append */
			StringBuffer_t *Sb = GetStringBuffer();

			/* Consume the comment line tokens */
			Count += 2;
			Character = Data[Count];

			/* Keep iterating!
			* Consume everything untill we reach a newline */
			while (1) {

				/* Check for end of command block 
				 * If it's the end, then consume them */
				if (Character == '*'
					&& Data[Count + 1] == '/') {
					Count += 2;
					break;
				}

				/* Keep track of line-skips */
				if (Character == '\n') {
					TempLineIncrease++;
					CharPos = 0;
				}

				/* Append */
				Sb->Append(Sb, Character);

				/* Consume -> Next */
				Count++;
				CharPos++;
				Character = Data[Count];

				/* Sanity */
				if ((Count + 1) >= Length) 
				{
					/* Error message */
					printf("Comment Block without closer at line %i\n", LineNo);

					/* Bail out */
					return -1;
				}
			}

			/* Create the token */
			CreateElement(CommentBlock, Sb->ToString(Sb), LineNo, CharPos);

			/* Cleanup */
			Sb->Dispose(&Sb);

			/* Restore line-no */
			LineNo += TempLineIncrease;

			/* Go one back */
			Count--;
		}
		/* Identifier? */
		else if (isalpha(Character)
			|| Character == '_') {

			/* We need a string buffer for this
			 * to append */
			StringBuffer_t *Sb = GetStringBuffer();

			/* Keep iterating! */
			while (isalpha(Character)
				|| isdigit(Character)
				|| Character == '_') {
				Sb->Append(Sb, Character);
				
				/* Consume -> Next */
				Count++;
				CharPos++;
				Character = Data[Count];
			}

			/* Create the token */
			CreateElement(Identifier, Sb->ToString(Sb), LineNo, CharPos);

			/* Cleanup */
			Sb->Dispose(&Sb);

			/* Go one back */
			Count--;
		}
		/* String literal? */
		else if (Character == '"') {

			/* We need a string buffer for this
			* to append */
			StringBuffer_t *Sb = GetStringBuffer();

			/* Skip Character */
			Count++;
			Character = Data[Count];

			/* Keep iterating! */
			while (Character != '"') {
				Sb->Append(Sb, Character);

				/* Consume -> Next */
				Count++;
				CharPos++;
				Character = Data[Count];
			}

			/* Create the token */
			CreateElement(StringLiteral, Sb->ToString(Sb), LineNo, CharPos);

			/* Cleanup */
			Sb->Dispose(&Sb);

			/* Increase */
			CharPos++;
		}
		/* Digit literal? */
		else if (isdigit(Character)) {

			/* We need a string buffer for this
			* to append */
			StringBuffer_t *Sb = GetStringBuffer();

			/* Keep iterating! */
			while (isdigit(Character)
				|| Character == '.') {
				Sb->Append(Sb, Character);

				/* Consume -> Next */
				Count++;
				CharPos++;
				Character = Data[Count];
			}

			/* Create the token */
			CreateElement(DigitLiteral, Sb->ToString(Sb), LineNo, CharPos);

			/* Cleanup */
			Sb->Dispose(&Sb);

			/* Go one back */
			Count--;
		}
		else
		{
			/* Which type of character is this? */
			switch (Character)
			{
				/* Tackle operators */
				case '+': {
					CreateElement(OperatorAdd, NULL, LineNo, CharPos);
				} break;
				case '-': {
					CreateElement(OperatorSubtract, NULL, LineNo, CharPos);
				} break;
				case '*': {
					CreateElement(OperatorMultiply, NULL, LineNo, CharPos);
				} break;
				case '/': {
					CreateElement(OperatorDivide, NULL, LineNo, CharPos);
				} break;
				case '=': {
					CreateElement(OperatorAssign, NULL, LineNo, CharPos);
				} break;

				/* Tackle brackets */
				case '(': {
					CreateElement(LeftParenthesis, NULL, LineNo, CharPos);
				} break;
				case ')': {
					CreateElement(RightParenthesis, NULL, LineNo, CharPos);
				} break;
				case '[': {
					CreateElement(LeftBracket, NULL, LineNo, CharPos);
				} break;
				case ']': {
					CreateElement(RightBracket, NULL, LineNo, CharPos);
				} break;
				case '{': {
					CreateElement(LeftFuncBracket, NULL, LineNo, CharPos);
				} break;
				case '}': {
					CreateElement(RightFuncBracket, NULL, LineNo, CharPos);
				} break;

				case ';': {
					CreateElement(OperatorSemiColon, NULL, LineNo, CharPos);
				} break;

				default: {
					/* Error message */
					printf("Invalid token at line %i, position %li: %c\n", LineNo, CharPos, Character);

					/* Bail out */
					return -1;

				} break;
			}
		}

	NextScan:
		Count++;
	}

	/* Return 0 on success */
	return 0;
}

/* Private helper for creating elements
 * and adding to the element list */
void Scanner::CreateElement(ElementType_t Type, const char *Data, int Line, long Character)
{
	/* Allocate a new instance and assign */
	Element *elem = new Element(Type, Line, Character);

	/* Add data? */
	if (Data != NULL)
		elem->SetData(Data);

	/* Add to list */
	m_lElements.push_back(elem);

	/* Diagnose */
	printf("Found Element %s\n", elem->GetName());
}
