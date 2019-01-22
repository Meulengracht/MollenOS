/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS Terminal Implementation (Alumnious)
 * - The terminal emulator implementation for Vali. Built on manual rendering and
 *   using freetype as the font renderer.
 */

#include <os/mollenos.h>
#include <os/process.h>
#include <os/input.h>
#include <io.h>
#include "../terminal_interpreter.hpp"
#include "../terminal.hpp"
#include "alumni_vali.hpp"

namespace {
    static bool EndsWith(const std::string& String, const std::string& Suffix)
    {
        return String.size() >= Suffix.size() && 0 == String.compare(String.size() - Suffix.size(), Suffix.size(), Suffix);
    }
}

CValiAlumni::CValiAlumni(std::unique_ptr<CTerminal> Terminal, std::unique_ptr<CTerminalInterpreter> Interpreter)
    : CAlumni(std::move(Terminal), std::move(Interpreter)), m_Profile("Philip"), m_CurrentDirectory("n/a"),
      m_Stdout(-1), m_Stderr(-1), m_Application(UUID_INVALID), m_StdoutThread(std::bind(&CValiAlumni::StdoutListener, this)), 
      m_StderrThread(std::bind(&CValiAlumni::StderrListener, this))
{
    UpdateWorkingDirectory();
}

CValiAlumni::~CValiAlumni()
{
}

void CValiAlumni::UpdateWorkingDirectory()
{
    char* CurrentPath = (char*)std::malloc(_MAXPATH);
    std::memset(CurrentPath, 0, _MAXPATH);
    if (GetWorkingDirectory(CurrentPath, _MAXPATH) == OsSuccess) {
        m_CurrentDirectory = std::string(CurrentPath);
    }
    else {
        m_CurrentDirectory = "n/a";
    }
    std::free(CurrentPath);
}

bool CValiAlumni::HandleKeyCode(unsigned int KeyCode, unsigned int Flags)
{
    SystemKey_t Key;
    Key.KeyCode = KeyCode;
    Key.Flags   = Flags;

    // Don't respond to released events
    if (Flags & KEY_MODIFIER_RELEASED) {
        return false;
    }

    if (KeyCode == VK_BACK) {
        m_Terminal->RemoveInput();
        m_Terminal->Invalidate();
    }
    else if (KeyCode == VK_ENTER) {
        std::string Input = m_Terminal->ClearInput(true);
        if (!m_Interpreter->Interpret(Input)) {
            if (m_Interpreter->GetClosestMatch().length() != 0) {
                m_Terminal->Print("Command did not exist, did you mean %s?\n", m_Interpreter->GetClosestMatch().c_str());
            }
        }
        PrintCommandHeader();
    }
    else if (KeyCode == VK_UP) {
        m_Terminal->HistoryPrevious();
        m_Terminal->Invalidate();
    }
    else if (KeyCode == VK_DOWN) {
        m_Terminal->HistoryNext();
        m_Terminal->Invalidate();
    }
    else if (KeyCode == VK_LEFT) {
        m_Terminal->MoveCursorLeft();
        m_Terminal->Invalidate();
    }
    else if (KeyCode == VK_RIGHT) {
        m_Terminal->MoveCursorRight();
        m_Terminal->Invalidate();
    }
    else if (TranslateSystemKey(&Key) == OsSuccess) {
        m_Terminal->AddInput(Key.KeyAscii);
        m_Terminal->Invalidate();
    }
    return true;
}

void CValiAlumni::PrintCommandHeader()
{
    // Dont print the command header if an application is running
    if (m_Application == UUID_INVALID) {
        m_Terminal->Print("[ %s | %s ]\n", m_Profile.c_str(), m_CurrentDirectory.c_str());
        m_Terminal->Print("$ ");
    }
    m_Terminal->Invalidate();
}

void CValiAlumni::WaitForProcess()
{
    int ExitCode = 0;
    ProcessJoin(m_Application, 0, &ExitCode);
    m_Terminal->Print("process exitted with code %i\n", ExitCode);
    m_Application = UUID_INVALID;
    PrintCommandHeader();
}

bool CValiAlumni::ExecuteProgram(const std::string& Program, const std::vector<std::string>& Arguments)
{
    ProcessStartupInformation_t StartupInformation;
    InitializeStartupInformation(&StartupInformation);
    std::string Line = "";

    // Set arguments
    if (Arguments.size() != 0) {
        for (int i = 0; i < Arguments.size(); i++) {
            if (i != 0) {
                Line += " ";
            }
            Line += Arguments[i];
        }
    }

    // Set inheritation
    StartupInformation.InheritFlags = PROCESS_INHERIT_STDOUT | PROCESS_INHERIT_STDIN | PROCESS_INHERIT_STDERR;
    StartupInformation.StdOutHandle = m_Stdout;
    StartupInformation.StdInHandle  = STDIN_FILENO;
    StartupInformation.StdErrHandle = m_Stderr;
    m_Application = ProcessSpawnEx(Program.c_str(), Line.c_str(), &StartupInformation);
    if (m_Application != UUID_INVALID) {
        WaitForProcess();
        return true;
    }
    return false;
}

std::vector<std::string> CValiAlumni::GetDirectoryContents(const std::string& Path)
{
    struct DIR*                 dir;
    struct DIRENT               dp;
    std::vector<std::string>    Entries;

    if (opendir(Path.c_str(), 0, &dir) != -1) {
        while (readdir(dir, &dp) != -1) {
            Entries.push_back(std::string(&dp.d_name[0]));
        }
        closedir(dir);
    }
    return Entries;
}

bool CValiAlumni::IsProgramPathValid(const std::string& Path)
{
    OsFileDescriptor_t Stats = { 0 };

    if (GetFileInformationFromPath(Path.c_str(), &Stats) == FsOk) {
        if (!(Stats.Flags & FILE_FLAG_DIRECTORY) && (Stats.Permissions & FILE_PERMISSION_EXECUTE)) {
            return true;
        }
        m_Terminal->Print("%s: not an executable file 0x%x\n", Path.c_str(), Stats.Permissions);
    }
    return false;
}

bool CValiAlumni::CommandResolver(const std::string& Command, const std::vector<std::string>& Arguments)
{
    std::string ProgramName = Command;
    std::string ProgramPath = "";

    if (!EndsWith(ProgramName, ".app")) {
        ProgramName += ".app";
    }

    // Guess the path of requested application, right now only working
    // directory and $bin is supported. Should we support apps that don't have .app? ...
    ProgramPath = "$bin/" + ProgramName;
    if (!IsProgramPathValid(ProgramPath)) {
        char TempBuffer[_MAXPATH] = { 0 };
        if (GetWorkingDirectory(&TempBuffer[0], _MAXPATH) == OsSuccess) {
            std::string CwdPath(&TempBuffer[0]);
            ProgramPath = CwdPath + "/" + ProgramName;
            if (!IsProgramPathValid(ProgramPath)) {
                m_Terminal->Print("%s: not a valid command\n", Command.c_str());
                return false;
            }
        }
        else {
            return false;
        }
    }
    return ExecuteProgram(ProgramPath, Arguments);
}

bool CValiAlumni::ListDirectory(const std::vector<std::string>& Arguments)
{
    std::string Path = m_CurrentDirectory;
    std::string Line = "";
    if (Arguments.size() != 0) {
        Path = Arguments[0];
    }

    auto DirectoryEntries = GetDirectoryContents(m_CurrentDirectory);
    for (auto Entry : DirectoryEntries) {
        Line += Entry + " ";
    }

    m_Terminal->Print("%s\n", Line.c_str());
    return true;
}

bool CValiAlumni::ChangeDirectory(const std::vector<std::string>& Arguments)
{
    if (Arguments.size() != 0) {
        std::string Path = Arguments[0];
        if (SetWorkingDirectory(Path.c_str()) == OsSuccess) {
            UpdateWorkingDirectory();
            return true;
        }
        m_Terminal->Print("cd: invalid argument %s\n", Path.c_str());
        return false;
    }
    m_Terminal->Print("cd: no argument given\n");
    return false;
}

void CValiAlumni::StdoutListener()
{
    char ReadBuffer[256];

    m_Stdout = pipe();
    if (m_Stdout == -1) {
        m_Terminal->Print("FAILED TO CREATE STDOUT\n");
        m_Terminal->Invalidate();
        return;
    }

    while (IsAlive()) {
        std::memset(&ReadBuffer[0], 0, sizeof(ReadBuffer));
        read(m_Stdout, &ReadBuffer[0], sizeof(ReadBuffer));
        m_Terminal->Print(&ReadBuffer[0]);
        m_Terminal->Invalidate();
    }
}

void CValiAlumni::StderrListener()
{
    char ReadBuffer[256];

    m_Stderr = pipe();
    if (m_Stderr == -1) {
        m_Terminal->Print("FAILED TO CREATE STDERR\n");
        m_Terminal->Invalidate();
        return;
    }

    while (IsAlive()) {
        std::memset(&ReadBuffer[0], 0, sizeof(ReadBuffer));
        read(m_Stderr, &ReadBuffer[0], sizeof(ReadBuffer));
        m_Terminal->Print(&ReadBuffer[0]);
        m_Terminal->Invalidate();
    }
}
