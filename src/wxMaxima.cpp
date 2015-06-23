// -*- mode: c++; c-file-style: "linux"; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
//  Copyright (C) 2004-2015 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//            (C) 2008-2009 Ziga Lenarcic <zigalenarcic@users.sourceforge.net>
//            (C) 2011-2011 cw.ahbong <cw.ahbong@gmail.com>
//            (C) 2012-2013 Doug Ilijev <doug.ilijev@gmail.com>
//            (C) 2014-2015 Gunter Königsmann <wxMaxima@physikbuch.de>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include "wxMaxima.h"
#include "SubstituteWiz.h"
#include "IntegrateWiz.h"
#include "LimitWiz.h"
#include "Plot2dWiz.h"
#include "SeriesWiz.h"
#include "SumWiz.h"
#include "Plot3dWiz.h"
#include "Config.h"
#include "Gen1Wiz.h"
#include "Gen2Wiz.h"
#include "Gen3Wiz.h"
#include "Gen4Wiz.h"
#include "BC2Wiz.h"
#include "MatWiz.h"
#include "SystemWiz.h"
#include "MathPrintout.h"
#include "MyTipProvider.h"
#include "EditorCell.h"
#include "SlideShowCell.h"
#include "PlotFormatWiz.h"
#include "Dirstructure.h"

#include <wx/clipbrd.h>
#include <wx/filedlg.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/mimetype.h>
#include <wx/dynlib.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/artprov.h>
#include <wx/aboutdlg.h>
#include <wx/utils.h>

#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/fs_mem.h>

#include <wx/url.h>
#include <wx/sstream.h>
#include <list>

#if defined __WXMAC__
#define MACPREFIX "wxMaxima.app/Contents/Resources/"
#endif

enum {
  maxima_process_id
};

wxMaxima::wxMaxima(wxWindow *parent, int id, const wxString title,
                   const wxPoint pos, const wxSize size) :
  wxMaximaFrame(parent, id, title, pos, size)
{
  wxConfig *config = (wxConfig *)wxConfig::Get();

  m_saving = false;
  m_autoSaveInterval = 0;
  config->Read(wxT("autoSaveInterval"), &m_autoSaveInterval);
  m_autoSaveInterval *= 60000;

  m_CWD = wxEmptyString;
  m_port = 4010;
  m_pid = -1;
  m_ready = false;
  m_readingPrompt=false;
  m_inLispMode = false;
  m_first = true;
  m_isRunning = false;
  m_dispReadOut = false;
  m_promptSuffix = "<PROMPT-S/>";
  m_promptPrefix = "<PROMPT-P/>";
  m_findDialog = NULL;
  m_firstPrompt = wxT("(%i1) ");

  m_client = NULL;
  m_server = NULL;

  config->Read(wxT("lastPath"), &m_lastPath);
  m_lastPrompt = wxEmptyString;

  m_closing = false;
  m_openFile = wxEmptyString;
  m_currentFile = wxEmptyString;
  m_fileSaved = true;
  m_printData = NULL;

  m_variablesOK = false;

  m_htmlHelpInitialized = false;
  m_chmhelpFile = wxEmptyString;

  m_isConnected = false;
  m_isRunning = false;

  wxFileSystem::AddHandler(new wxMemoryFSHandler); // for saving wxmx

  LoadRecentDocuments();
  UpdateRecentDocuments();

  m_findDialog = NULL;
  m_findData.SetFlags(wxFR_DOWN);

  m_console->SetFocus();
  m_console->m_keyboardInactiveTimer.SetOwner(this,KEYBOARD_INACTIVITY_TIMER_ID);

  m_autoSaveIntervalExpired = false;
  m_autoSaveTimer.SetOwner(this,AUTO_SAVE_TIMER_ID);
  
#if wxUSE_DRAG_AND_DROP
  m_console->SetDropTarget(new MyDropTarget(this));
#endif

  StatusMaximaBusy(waiting);
  
  /// RegEx for function definitions
  m_funRegEx.Compile(wxT("^ *([[:alnum:]%_]+) *\\(([[:alnum:]%_,[[.].] ]*)\\) *:="));
  // RegEx for variable definitions
  m_varRegEx.Compile(wxT("^ *([[:alnum:]%_]+) *:"));
  // RegEx for blank statement removal
  m_blankStatementRegEx.Compile(wxT("(^;)|((^|;)(((\\/\\*.*\\*\\/)?([[:space:]]*))+;)+)"));
}

wxMaxima::~wxMaxima()
{
  if (m_client != NULL)
    m_client->Destroy();

  if (m_printData != NULL)
    delete m_printData;
}


#if wxUSE_DRAG_AND_DROP

bool MyDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& files) {

  if (files.GetCount() != 1)
    return true;

  if (wxGetKeyState(WXK_SHIFT)) {
    m_wxmax->m_console->InsertText(files[0]);
    return true;
  }

  if (files[0].Right(4) == wxT(".wxm") ||
      files[0].Right(5) == wxT(".wxmx"))
  {
    if (m_wxmax->m_console->GetTree() != NULL &&
        !m_wxmax->DocumentSaved())
    {
      int close = m_wxmax->SaveDocumentP();
      
      if (close == wxID_CANCEL)
        return false;
      
      if (close == wxID_YES) {
        if (!m_wxmax->SaveFile())
          return false;
      }
    }

    m_wxmax->OpenFile(files[0]);
    return true;
  }

  if (files[0].Right(4) == wxT(".png")  ||
      files[0].Right(5) == wxT(".jpeg") ||
      files[0].Right(4) == wxT(".jpg"))
  {
    m_wxmax->LoadImage(files[0]);
    return true;
  }

  m_wxmax->m_console->InsertText(files[0]);
  return true;
}

#endif

//!--------------------------------------------------------------------------------
//  Startup
//--------------------------------------------------------------------------------
void wxMaxima::InitSession()
{
  bool server = false;
  int defaultPort = 4010;

  wxConfig::Get()->Read(wxT("defaultPort"), &defaultPort);
  m_port = defaultPort;

  while (!(server = StartServer()))
  {
    m_port++;
    if (m_port > defaultPort + 50)
    {
      wxMessageBox(_("wxMaxima could not start the server.\n\n"
                     "Please check you have network support\n"
                     "enabled and try again!"),
                   _("Fatal error"),
                   wxOK | wxICON_ERROR);
      break;
    }
  }

  if (!server)
    SetStatusText(_("Starting server failed"));
  else if (!StartMaxima())
    SetStatusText(_("Starting Maxima process failed"), 1);
}

void wxMaxima::FirstOutput(wxString s)
{
  Dirstructure dirstructure;
  
  int startMaxima = s.find(wxT("Maxima"), 5); // The first in s is wxMaxima version - skip it
  int startHTTP = s.find(wxT("http"), startMaxima);
  m_maximaVersion = s.SubString(startMaxima+7, startHTTP - 1);

  wxRegEx lisp(wxT("[u|U]sing Lisp ([^\n]*)\n"));
  if (lisp.Matches(s))
    m_lispVersion = lisp.GetMatch(s, 1);

  m_lastPrompt = wxT("(%i1) ");
  
  /// READ FUNCTIONS FOR AUTOCOMPLETION
  m_console->LoadSymbols(dirstructure.AutocompleteFile());

  m_console->SetFocus();
}

///--------------------------------------------------------------------------------
///  Appending stuff to output
///--------------------------------------------------------------------------------

/*! ConsoleAppend adds a new line s of type to the console window.
 *
 * It will call
 * DoConsoleAppend if s is in xml and DoRawCosoleAppend if s is not in xml.
 */
void wxMaxima::ConsoleAppend(wxString s, int type)
{
  m_dispReadOut = false;
  s.Replace(m_promptSuffix, wxEmptyString);

  // If the string we have to append is empty we return immediately.
  wxString t(s);
  t.Trim();
  t.Trim(false);
  if (!t.Length())
  {
    return ;
  }

  if (type != MC_TYPE_ERROR)
    StatusMaximaBusy(parsing);

  if (type == MC_TYPE_DEFAULT)
  {    
    while (s.Length() > 0)
    {
      int start = s.Find(wxT("<mth"));

      if (start == wxNOT_FOUND) {
        t = s;
        t.Trim();
        t.Trim(false);
        if (t.Length())
          DoRawConsoleAppend(s, MC_TYPE_DEFAULT);
        s = wxEmptyString;
      }
      else {

        // If the string doesn't begin with a <mth> we add the
        // part of the string that precedes the <mth> to the console
        // first.
        wxString pre = s.SubString(0, start - 1);
        wxString pre1(pre);
        pre1.Trim();
        pre1.Trim(false);
        if (pre1.Length())
          DoRawConsoleAppend(pre, MC_TYPE_DEFAULT);

        // If the math tag ends inside this string we add the whole tag.
        int end = s.Find(wxT("</mth>"));
        if (end == wxNOT_FOUND)
          end = s.Length();
        else
          end += 5;
        wxString rest = s.SubString(start, end);

        DoConsoleAppend(wxT("<span>") + rest +
                        wxT("</span>"), type, false);
        s = s.SubString(end + 1, s.Length());
      }
    }
  }
  
  else if (type == MC_TYPE_PROMPT) {
    m_lastPrompt = s;

    if (s.StartsWith(wxT("MAXIMA> "))) {
      s = s.Right(8);
    }
    else
      s = s + wxT(" ");

    DoConsoleAppend(wxT("<span>") + s + wxT("</span>"), type, true);
  }

  else if (type == MC_TYPE_ERROR)
    DoRawConsoleAppend(s, MC_TYPE_ERROR);

  else
    DoConsoleAppend(wxT("<span>") + s + wxT("</span>"), type, false);
}

void wxMaxima::DoConsoleAppend(wxString s, int type, bool newLine,
                               bool bigSkip)
{
  MathCell* cell;

  s.Replace(wxT("\n"), wxT(""), true);

  cell = m_MParser.ParseLine(s, type);

  if (cell == NULL)
  {
    wxMessageBox(_("There was an error in generated XML!\n\n"
                   "Please report this as a bug."), _("Error"),
                 wxOK | wxICON_EXCLAMATION);
    return ;
  }

  cell->SetSkip(bigSkip);
  m_console->InsertLine(cell, newLine || cell->BreakLineHere());
}

void wxMaxima::DoRawConsoleAppend(wxString s, int type)
{
  if (type == MC_TYPE_MAIN_PROMPT)
  {
    TextCell* cell = new TextCell(s);
    cell->SetType(type);
    m_console->InsertLine(cell, true);
  }

  else
  {
    wxStringTokenizer tokens(s, wxT("\n"));
    int count = 0;
    MathCell *tmp = NULL, *lst = NULL;
    while (tokens.HasMoreTokens())
    {
      TextCell* cell = new TextCell(tokens.GetNextToken());

      cell->SetType(type);

      if (tokens.HasMoreTokens())
        cell->SetSkip(false);

      if (lst == NULL)
        tmp = lst = cell;
      else {
        lst->AppendCell(cell);
        cell->ForceBreakLine(true);
        lst = cell;
      }

      count++;
    }
    m_console->InsertLine(tmp, true);
  }
}

/*! Remove empty statements
 *
 * We need to remove any statement which would be considered empty
 * and thus cause an error. Comments within non-empty expressions seem to
 * be fine.
 *
 * What we need to remove is any statements which are any amount of whitespace
 * and any amount of comments, in any order, ended by a semicolon,
 * and nothing else.
 *
 * The most that should be left over is a single empty statement, ";".
 *
 * @param s The command string from which to remove comment expressions.
 */
void wxMaxima::StripComments(wxString& s)
{
  m_blankStatementRegEx.Replace(&s, wxT(";"));
}

void wxMaxima::SendMaxima(wxString s, bool addToHistory)
{
  if (!m_variablesOK) {
    m_variablesOK = true;
    SetupVariables();
  }

#if wxUSE_UNICODE
  s.Replace(wxT("\x00B2"), wxT("^2"));
  s.Replace(wxT("\x00B3"), wxT("^3"));
  s.Replace(wxT("\x00BD"), wxT("(1/2)"));
  s.Replace(wxT("\x221A"), wxT("sqrt"));
  s.Replace(wxT("\x03C0"), wxT("%pi"));
  s.Replace(wxT("\x2148"), wxT("%i"));
  s.Replace(wxT("\x2147"), wxT("%e"));
  s.Replace(wxT("\x221E"), wxT("inf"));
  s.Replace(wxT("\x22C0"), wxT(" and "));
  s.Replace(wxT("\x22C1"), wxT(" or "));
  s.Replace(wxT("\x22BB"), wxT(" xor "));
  s.Replace(wxT("\x22BC"), wxT(" nand "));
  s.Replace(wxT("\x22BD"), wxT(" nor "));
  s.Replace(wxT("\x21D2"), wxT(" implies "));
  s.Replace(wxT("\x21D4"), wxT(" equiv "));
  s.Replace(wxT("\x00AC"), wxT(" not "));
  s.Replace(wxT("\x2212"), wxT("-")); // An unicode minus sign
  s.Replace(wxT("\xDCB6"), wxT(" ")); // Some weird unicode space character

#endif

  StatusMaximaBusy(calculating);
  m_dispReadOut = false;

  /// Add this command to History
  if (addToHistory)
    AddToHistory(s);

  s.Replace(wxT("\n"), wxT(" "));
  s.Append(wxT("\n"));
  StripComments(s);

  /// Check for function/variable definitions
  wxStringTokenizer commands(s, wxT(";$"));
  while (commands.HasMoreTokens())
  {
    wxString line = commands.GetNextToken();
    if (m_varRegEx.Matches(line))
      m_console->AddSymbol(m_varRegEx.GetMatch(line, 1));

    if (m_funRegEx.Matches(line))
    {
      wxString funName = m_funRegEx.GetMatch(line, 1);
      m_console->AddSymbol(funName);

      /// Create a template from the input
      wxString args = m_funRegEx.GetMatch(line, 2);
      wxStringTokenizer argTokens(args, wxT(","));
      funName << wxT("(");
      int count = 0;
      while (argTokens.HasMoreTokens()) {
        if (count > 0)
          funName << wxT(",");
        wxString a = argTokens.GetNextToken().Trim().Trim(false);
        if (a != wxEmptyString)
        {
          if (a[0]=='[')
            funName << wxT("[<") << a.SubString(1, a.Length()-2) << wxT(">]");
          else
            funName << wxT("<") << a << wxT(">");
          count++;
        }
      }
      funName << wxT(")");
      m_console->AddSymbol(funName, AutoComplete::tmplte);
    }
  }

  if(m_console) m_console->EnableEdit(false);

  if(m_client)
  {
#if wxUSE_UNICODE
    m_client->Write(s.utf8_str(), strlen(s.utf8_str()));
#else
    m_client->Write(s.c_str(), s.Length());
#endif
  }
}

///--------------------------------------------------------------------------------
///  Socket stuff
///--------------------------------------------------------------------------------

/*! Convert problematic characters into something same
 * 
 * This makes sure that special character codes are not encountered unexpectedly
 * (i.e. early).
 */
void wxMaxima::SanitizeSocketBuffer(char *buffer, int length)
{
  for (int i = 0; i < length; i++)
  {
    if (buffer[i] == 0)
      buffer[i] = ' ';  // convert input null (0) to space (0x20)
  }
}

void wxMaxima::ClientEvent(wxSocketEvent& event)
{
  char buffer[SOCKET_SIZE + 1];
  int read;
  switch (event.GetSocketEvent())
  {

  case wxSOCKET_INPUT:
    m_client->Read(buffer, SOCKET_SIZE);

    if (!m_client->Error())
    {
      read = m_client->LastCount();
      buffer[read] = 0;

      SanitizeSocketBuffer(buffer, read);

#if wxUSE_UNICODE
      m_currentOutput += wxString(buffer, wxConvUTF8);
#else
      m_currentOutput += wxString(buffer, *wxConvCurrent);
#endif

      if (!m_dispReadOut &&
	  (m_currentOutput != wxT("\n")) &&
	  (m_currentOutput != wxT("<wxxml-symbols></wxxml-symbols>"))) {
	StatusMaximaBusy(transferring);
        m_dispReadOut = true;
      }

      if (m_first && m_currentOutput.Find(m_firstPrompt) > -1)
        ReadFirstPrompt(m_currentOutput);

      ReadLoadSymbols(m_currentOutput);

      ReadMath(m_currentOutput);

      ReadPrompt(m_currentOutput);

      ReadLispError(m_currentOutput);
    }

    break;

  case wxSOCKET_LOST:
    if (!m_closing)
      ConsoleAppend(wxT("\nCLIENT: Lost socket connection ...\n"
                        "Restart Maxima with 'Maxima->Restart Maxima'.\n"),
                    MC_TYPE_ERROR);
    m_console->SetWorkingGroup(NULL);
    m_console->SetSelection(NULL);
    m_console->SetActiveCell(NULL);
    m_pid = -1;
    m_client->Destroy();
    m_client = NULL;
    m_isConnected = false;
    break;

  default:
    break;
  }
}

/*!
 * ServerEvent is triggered when maxima connects to the socket server.
 */
void wxMaxima::ServerEvent(wxSocketEvent& event)
{
  switch (event.GetSocketEvent())
  {

  case wxSOCKET_CONNECTION :
  {
    if (m_isConnected) {
      wxSocketBase *tmp = m_server->Accept(false);
      tmp->Close();
      return;
    }
    m_isConnected = true;
    m_client = m_server->Accept(false);
    m_client->SetEventHandler(*this, socket_client_id);
    m_client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
    m_client->Notify(true);
#ifndef __WXMSW__
    ReadProcessOutput();
#endif
  }
  break;

  case wxSOCKET_LOST:
    if (!m_closing)
      ConsoleAppend(wxT("\nSERVER: Lost socket connection ...\n"
                        "Restart Maxima with 'Maxima->Restart Maxima'.\n"),
                    MC_TYPE_ERROR);
    m_pid = -1;
    m_isConnected = false;

  default:
    break;
  }
}

bool wxMaxima::StartServer()
{
  SetStatusText(wxString::Format(_("Starting server on port %d"), m_port), 1);

  wxIPV4address addr;

#ifndef __WXMAC__
  addr.LocalHost();
#else
  addr.AnyAddress();
#endif

  addr.Service(m_port);

  m_server = new wxSocketServer(addr);
  if (!m_server->Ok())
  {
    delete m_server;
    m_server = NULL;
    m_isRunning = false;
    m_isConnected = false;
    SetStatusText(_("Starting server failed"), 1);
    return false;
  }
  SetStatusText(_("Server started"), 1);
  m_server->SetEventHandler(*this, socket_server_id);
  m_server->SetNotify(wxSOCKET_CONNECTION_FLAG);
  m_server->Notify(true);

  m_isConnected = false;
  m_isRunning = true;
  return m_isRunning;
}

///--------------------------------------------------------------------------------
///  Maxima process stuff
///--------------------------------------------------------------------------------

bool wxMaxima::StartMaxima()
{
  if (m_isConnected)
  {
    KillMaxima();
    //    m_client->Close();
    m_isConnected = false;
  }

  m_console->QuestionAnswered();
  m_console->SetWorkingGroup(NULL);

  m_variablesOK = false;
  wxString command = GetCommand();;

  if (command.Length() > 0)
  {
#if defined(__WXMSW__)
    wxString clisp = command.SubString(1, command.Length() - 3);
    clisp.Replace("\\bin\\maxima.bat", "\\clisp-*.*");
    if (wxFindFirstFile(clisp, wxDIR).empty())
      command.Append(wxString::Format(wxT(" -s %d"), m_port));
    else
      command.Append(wxString::Format(wxT(" -r \":lisp (setup-client %d)\""), m_port));
    wxSetEnv(wxT("home"), wxGetHomeDir());
    wxSetEnv(wxT("maxima_signals_thread"), wxT("1"));
#else
    command.Append(wxString::Format(wxT(" -r \":lisp (setup-client %d)\""),
                                    m_port));
#endif

#if defined __WXMAC__
    wxSetEnv(wxT("DISPLAY"), wxT(":0.0"));
#endif

    m_process = new wxProcess(this, maxima_process_id);
    m_process->Redirect();
    m_first = true;
    m_pid = -1;
    SetStatusText(_("Starting Maxima..."), 1);
    wxExecute(command, wxEXEC_ASYNC, m_process);
    m_input = m_process->GetInputStream();
    SetStatusText(_("Maxima started. Waiting for connection..."), 1);
  }
  else
    return false;
  return true;
}


void wxMaxima::Interrupt(wxCommandEvent& event)
{
  if (m_pid < 0)
  {
    GetMenuBar()->Enable(menu_interrupt_id, false);
    return ;
  }
#if defined (__WXMSW__)
  wxString path, maxima = GetCommand(false);
  wxArrayString out;
  maxima = maxima.SubString(2, maxima.Length() - 3);
  wxFileName::SplitPath(maxima, &path, NULL, NULL);
  wxString command = wxT("\"") + path + wxT("\\winkill.exe\"");
  command += wxString::Format(wxT(" -INT %ld"), m_pid);
  wxExecute(command, out);
#else
  wxProcess::Kill(m_pid, wxSIGINT);
#endif
}

void wxMaxima::KillMaxima()
{
  m_process->Detach();
  if (m_pid < 0)
  {
    if (m_inLispMode)
      SendMaxima(wxT("($quit)"));
    else
      SendMaxima(wxT("quit();"));
    return ;
  }
  wxProcess::Kill(m_pid, wxSIGKILL);
}

void wxMaxima::OnProcessEvent(wxProcessEvent& event)
{
  if (!m_closing)
    SetStatusText(_("Maxima process terminated."), 1);

  m_maximaVersion = wxEmptyString;
  m_lispVersion = wxEmptyString;

//  delete m_process;
//  m_process = NULL;
}

void wxMaxima::CleanUp()
{
  if (m_client)
    m_client->Notify(false);
  if (m_isConnected)
    KillMaxima();
  if (m_isRunning)
    m_server->Destroy();
}

///--------------------------------------------------------------------------------
///  Dealing with stuff read from the socket
///--------------------------------------------------------------------------------

void wxMaxima::ReadFirstPrompt(wxString &data)
{
#if defined(__WXMSW__)
  int start = data.Find(wxT("Maxima"));
  if (start == wxNOT_FOUND)
    start = 0;
  
  FirstOutput(wxT("wxMaxima ")
              wxT(VERSION)
              wxT(" http://andrejv.github.io/wxmaxima/\n") +
              data.SubString(start, data.Length() - 1));
#endif // __WXMSW__

  // Wait for a line maxima informs us about it's process id in.
  int s = data.Find(wxT("pid=")) + 4;
  int t = s + data.SubString(s, data.Length()).Find(wxT("\n")) - 1;

  // Read this pid
  if (s < t)
    data.SubString(s, t).ToLong(&m_pid);

  if (m_pid > 0)
    GetMenuBar()->Enable(menu_interrupt_id, true);

  m_first = false;
  m_inLispMode = false;
  StatusMaximaBusy(waiting);
  m_closing = false; // when restarting maxima this is temporarily true
  data = wxEmptyString;
  m_console->EnableEdit(true);

  if (m_openFile.Length())
  {
    OpenFile(m_openFile);
    m_openFile = wxEmptyString;
  }
  else if (m_console->m_evaluationQueue->Empty())
  {
    bool open = false;
    wxConfig::Get()->Read(wxT("openHCaret"), &open);
    if (open)
      m_console->OpenNextOrCreateCell();
  }
}

/***
 * Checks if maxima displayed a new chunk of math
 */
void wxMaxima::ReadMath(wxString &data)
{

  // Skip all data before the last prompt in the data string.
  int end;
  while ((end = data.Find(m_promptPrefix)) != wxNOT_FOUND)
  {
    m_readingPrompt = true;
    wxString o = data.Left(end);
    ConsoleAppend(o, MC_TYPE_DEFAULT);
    data = data.SubString(end + m_promptPrefix.Length(),
                                                data.Length());
  }
  
  // If we did find a prompt in the last step we leave this function again.
  if (m_readingPrompt)
    return ;

  // Append everything untill the "end of math" marker to the console.
  wxString mth = wxT("</mth>");
  end = data.Find(mth);
  while (end > -1)
  {
    wxString o = data.Left(end);
    ConsoleAppend(o + mth, MC_TYPE_DEFAULT);
    data = data.SubString(end + mth.Length(),
                          data.Length());
    end = data.Find(mth);
  }
}

void wxMaxima::ReadLoadSymbols(wxString &data)
{
  int start;
  while ((start = data.Find(wxT("<wxxml-symbols>"))) != wxNOT_FOUND)
  {
    int end = data.Find(wxT("</wxxml-symbols>"));
    if (end != wxNOT_FOUND)
    {
      // Put the symbols into a separate string
      wxString symbols = data.SubString(start + 15, end - 1);

      // Remove the symbols from the data string
      data = data.SubString(0, start-1) +
        data.SubString(end + 16, data.Length());

      // Send each symbol to the console
      wxStringTokenizer templates(symbols, wxT("$"));
      while (templates.HasMoreTokens())
        m_console->AddSymbol(templates.GetNextToken());
    }
    else
      break;
  }
}

/***
 * Checks if maxima displayed a new prompt.
 */
void wxMaxima::ReadPrompt(wxString &data)
{
  m_console->m_questionPrompt = false;
  m_ready=true;
  int end = data.Find(m_promptSuffix);
  if (end > -1)
  {
    m_readingPrompt = false;
    wxString o = data.Left(end);
    if (o != wxT("\n") && o.Length())
    {
      // Maxima displayed a new main prompt
      if (o.StartsWith(wxT("(%i")))
      {
        m_console->QuestionAnswered();
        //m_lastPrompt = o.Mid(1,o.Length()-1);
        //m_lastPrompt.Replace(wxT(")"), wxT(":"), false);
        m_lastPrompt = o;
        m_console->m_evaluationQueue->RemoveFirst(); // remove it from queue

        if (m_console->m_evaluationQueue->Empty()) { // queue empty?
          m_console->ShowHCaret();
          m_console->SetWorkingGroup(NULL);
          m_console->Refresh();

          // If we have selected a cell in order to show we are evaluating it
          // we should now remove this marker.
          if(m_console->FollowEvaluation())
          {
            if(m_console->GetActiveCell())
              m_console->GetActiveCell() -> SelectNone();            
            m_console->SetSelection(NULL,NULL); 
          }
	  m_console->FollowEvaluation(false);
        }
        else { // we don't have an empty queue
          m_ready = false;
          m_console->Refresh();
          m_console->EnableEdit();
          if(!m_console->QuestionPending())
            TryEvaluateNextInQueue();
          else
            m_console->QuestionAnswered();
        }

        m_console->EnableEdit();

        if (m_console->m_evaluationQueue->Empty())
        {
          bool open = false;
          wxConfig::Get()->Read(wxT("openHCaret"), &open);
          if (open)
            m_console->OpenNextOrCreateCell();
        }
      }

      // We have a question
      else {
        m_console->QuestionAnswered();
        m_console->QuestionPending(true);
        if (o.Find(wxT("<mth>")) > -1)
          DoConsoleAppend(o, MC_TYPE_PROMPT);
        else
          DoRawConsoleAppend(o, MC_TYPE_PROMPT);
	if(m_console->ScrolledAwayFromEvaluation())
        {
          if(m_console->m_mainToolBar)
            m_console->m_mainToolBar->EnableTool(ToolBar::tb_follow,true);
        }
      }

      if (o.StartsWith(wxT("\nMAXIMA>")))
        m_inLispMode = true;
      else
        m_inLispMode = false;
    }

    if (m_ready)
    {
      if(m_console->m_questionPrompt)
        StatusMaximaBusy(userinput);
      else
      {
        if(m_console->m_evaluationQueue->Empty())
          StatusMaximaBusy(waiting);
      }
    }
    
    data = data.SubString(end + m_promptSuffix.Length(),
                                                data.Length());
  }
}

void wxMaxima::SetCWD(wxString file)
{
#if defined __WXMSW__
  file.Replace(wxT("\\"), wxT("/"));
#endif
  
  wxFileName filename(file);

  if (filename.GetPath() == wxEmptyString)
    filename.AssignDir(wxGetCwd());

  // Escape all backslashes in the filename if needed by the OS.
  wxString filenamestring= filename.GetFullPath();
#if defined __WXMSW__
  filenamestring.Replace(wxT("\\"),wxT("/"));
#endif

  wxString workingDirectory = filename.GetPath();

  if(workingDirectory != GetCWD())
  {
    SendMaxima(wxT(":lisp-quiet (setf $wxfilename \"") +
               filenamestring +
               wxT("\")"));
    SendMaxima(wxT(":lisp-quiet (setf $wxdirname \"") +
               filename.GetPath() +
               wxT("\")"));
    
    SendMaxima(wxT(":lisp-quiet (wx-cd \"") + filenamestring + wxT("\")"));
    if (m_ready)
    {
      if(m_console->m_evaluationQueue->Empty())
        StatusMaximaBusy(waiting);
    }
    m_CWD = workingDirectory;
  }
}

// OpenWXMFile
// Clear document (if clearDocument == true), then insert file
bool wxMaxima::OpenWXMFile(wxString file, MathCtrl *document, bool clearDocument)
{
  SetStatusText(_("Opening file"), 1);
  wxBeginBusyCursor();
  document->Freeze();

  // open wxm file
  wxTextFile inputFile(file);
  wxArrayString *wxmLines = NULL;

  if (!inputFile.Open()) {
    wxEndBusyCursor();
    document->Thaw();
    wxMessageBox(_("wxMaxima encountered an error loading ") + file, _("Error"), wxOK | wxICON_EXCLAMATION);
    return false;
  }

  if (inputFile.GetFirstLine() !=
      wxT("/* [wxMaxima batch file version 1] [ DO NOT EDIT BY HAND! ]*/"))
  {
    inputFile.Close();
    wxEndBusyCursor();
    document->Thaw();
    wxMessageBox(_("wxMaxima encountered an error loading ") + file, _("Error"), wxOK | wxICON_EXCLAMATION);
    return false;
  }

  wxmLines = new wxArrayString();
  wxString line;
  for (line = inputFile.GetFirstLine();
       !inputFile.Eof();
       line = inputFile.GetNextLine()) {
    wxmLines->Add(line);
  }
  wxmLines->Add(line);

  inputFile.Close();

  GroupCell *tree = CreateTreeFromWXMCode(wxmLines);

  delete wxmLines;

  // from here on code is identical for wxm and wxmx
  if (clearDocument)
    document->ClearDocument();

  document->InsertGroupCells(tree); // this also recalculates

  if (clearDocument) {
    m_currentFile = file;
    m_fileSaved = false; // to force reset title to update
    ResetTitle(true);
    document->SetSaved(true);
  }
  else
    ResetTitle(false);

  document->Thaw();
  document->Refresh(); // redraw document outside Freeze-Thaw

  m_console->SetDefaultHCaret();
  m_console->SetFocus();

  SetCWD(file);

  wxEndBusyCursor();
  return true;
}

bool wxMaxima::OpenWXMXFile(wxString file, MathCtrl *document, bool clearDocument)
{
  SetStatusText(_("Opening file"), 1);
  wxBeginBusyCursor();
  document->Freeze();

  // open wxmx file
  wxXmlDocument xmldoc;

  wxFileSystem fs;
  wxFSFile *fsfile = fs.OpenFile(wxT("file:") + file + wxT("#zip:content.xml"));

  if ((fsfile == NULL) || (!xmldoc.Load(*(fsfile->GetStream())))) {
    wxEndBusyCursor();
    document->Thaw();
    delete fsfile;
    wxMessageBox(_("wxMaxima encountered an error loading ") + file, _("Error"),
                 wxOK | wxICON_EXCLAMATION);
    return false;
  }

  delete fsfile;

  // start processing the XML file
  if (xmldoc.GetRoot()->GetName() != wxT("wxMaximaDocument")) {
    wxEndBusyCursor();
    document->Thaw();
    wxMessageBox(_("wxMaxima encountered an error loading ") + file, _("Error"),
                 wxOK | wxICON_EXCLAMATION);
    return false;
  }

  // read document version and complain
  wxString docversion = xmldoc.GetRoot()->GetAttribute(wxT("version"), wxT("1.0"));
  wxString ActiveCellNumber_String = xmldoc.GetRoot()->GetAttribute(wxT("activecell"), wxT("-1"));
  long ActiveCellNumber;
  if(!ActiveCellNumber_String.ToLong(&ActiveCellNumber))
    ActiveCellNumber = -1;
  
  double version = 1.0;
  if (docversion.ToDouble(&version)) {
    int version_major = int(version);
    int version_minor = int(10* (version - double(version_major)));

    if (version_major > DOCUMENT_VERSION_MAJOR) {
      wxEndBusyCursor();
      document->Thaw();
      wxMessageBox(_("Document ") + file +
                   _(" was saved using a newer version of wxMaxima. Please update your wxMaxima."),
                   _("Error"), wxOK | wxICON_EXCLAMATION);
      return false;
    }
    if (version_minor > DOCUMENT_VERSION_MINOR) {
      wxEndBusyCursor();
      wxMessageBox(_("Document ") + file +
                   _(" was saved using a newer version of wxMaxima so it may not load correctly. Please update your wxMaxima."),
                   _("Warning"), wxOK | wxICON_EXCLAMATION);
      wxBeginBusyCursor();
    }
  }

  // read zoom factor
  wxString doczoom = xmldoc.GetRoot()->GetAttribute(wxT("zoom"),wxT("100"));
  wxXmlNode *xmlcells = xmldoc.GetRoot()->GetChildren();
  GroupCell *tree = CreateTreeFromXMLNode(xmlcells, file);

  // from here on code is identical for wxm and wxmx
  if (clearDocument) {
    document->ClearDocument();
    long int zoom = 100;
    if (!(doczoom.ToLong(&zoom)))
      zoom = 100;
    document->SetZoomFactor( double(zoom) / 100.0, false); // Set zoom if opening, dont recalculate
  }

  document->InsertGroupCells(tree); // this also recalculates

  if (clearDocument) {
    m_currentFile = file;
    m_fileSaved = false; // to force reset title to update
    ResetTitle(true);
    document->SetSaved(true);
  }
  else
    ResetTitle(false);

  document->Thaw();
  document->Refresh(); // redraw document outside Freeze-Thaw

  m_console->SetDefaultHCaret();
  m_console->SetFocus();

  SetCWD(file);
  
  m_console->EnableEdit(true);
  wxEndBusyCursor();

  // We can set the cursor to the last known position.
  if(ActiveCellNumber == 0)
      m_console->SetHCaret(NULL);
  if(ActiveCellNumber > 0)
  {
    GroupCell *pos = m_console->GetTree();

    for(long i=1;i<ActiveCellNumber;i++)
      if(pos)
        pos=dynamic_cast<GroupCell*>(pos->m_next);

    if(pos)
      m_console->SetHCaret(pos);
  }
  return true;
}

GroupCell* wxMaxima::CreateTreeFromXMLNode(wxXmlNode *xmlcells, wxString wxmxfilename)
{
  MathParser mp(wxmxfilename);
  MathCell *tree = NULL;
  MathCell *last = NULL;

  bool warning = true;

  if (xmlcells) {
    last = tree = mp.ParseTag(xmlcells, false); // first cell
    while (xmlcells->GetNext()) {
      xmlcells = xmlcells->GetNext();
      MathCell *cell = mp.ParseTag(xmlcells, false);

      if (cell != NULL)
      {
        last->m_next = last->m_nextToDraw = cell;
        last->m_next->m_previous = last->m_next->m_previousToDraw = last;

        last = last->m_next;
      }
      else if (warning)
      {
        wxMessageBox(_("Parts of the document will not be loaded correctly!"), _("Warning"),
                     wxOK | wxICON_WARNING);
        warning = false;
      }
    }
  }

  return dynamic_cast<GroupCell*>(tree);
}

GroupCell* wxMaxima::CreateTreeFromWXMCode(wxArrayString* wxmLines)
{
  bool hide = false;
  GroupCell* tree = NULL;
  GroupCell* last = NULL;
  GroupCell* cell = NULL;

  while (wxmLines->GetCount()>0)
  {
    if (wxmLines->Item(0) == wxT("/* [wxMaxima: hide output   ] */"))
      hide = true;

    // Print title
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: title   start ]"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("   [wxMaxima: title   end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_TITLE, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }

    // Print section
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: section start ]"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("   [wxMaxima: section end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_SECTION, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }

    // Print subsection
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: subsect start ]"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("   [wxMaxima: subsect end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_SUBSECTION, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }
    
    // print subsubsection
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: subsubsect start ]"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("   [wxMaxima: subsubsect end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_SUBSUBSECTION, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }

    // Print comment
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: comment start ]"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("   [wxMaxima: comment end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_TEXT, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }

    // Print input
    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: input   start ] */"))
    {
      wxmLines->RemoveAt(0);

      wxString line;
      while (wxmLines->Item(0) != wxT("/* [wxMaxima: input   end   ] */"))
      {
        if (line.Length() == 0)
          line = wxmLines->Item(0);
        else
          line += wxT("\n") + wxmLines->Item(0);

        wxmLines->RemoveAt(0);
      }

      cell = new GroupCell(GC_TYPE_CODE, line);
      if (hide) {
        cell->Hide(true);
        hide = false;
      }
    }

    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: page break    ] */"))
    {
      wxmLines->RemoveAt(0);

      cell = new GroupCell(GC_TYPE_PAGEBREAK);
    }

    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: fold    start ] */"))
    {
      wxmLines->RemoveAt(0);

      last->HideTree(CreateTreeFromWXMCode(wxmLines));
    }

    else if (wxmLines->Item(0) == wxT("/* [wxMaxima: fold    end   ] */"))
    {
      wxmLines->RemoveAt(0);

      break;
    }

    if (cell) { // if we have created a cell in this pass
      if (!tree)
        tree = last = cell;
      else {

        last->m_next = last->m_nextToDraw = ((MathCell *)cell);
        last->m_next->m_previous = last->m_next->m_previousToDraw = ((MathCell *)last);

        last = (GroupCell *)last->m_next;

      }
      cell = NULL;
    }

    wxmLines->RemoveAt(0);
  }

  return tree;
}

/***
 * This works only for gcl by default - other lisps have different prompts.
 */
void wxMaxima::ReadLispError(wxString &data)
{
  static const wxString lispError = wxT("dbl:MAXIMA>>"); // gcl
  int end = data.Find(lispError);
  if (end > -1)
  {
    m_readingPrompt = false;
    m_inLispMode = true;
    wxString o = data.Left(end);
    ConsoleAppend(o, MC_TYPE_DEFAULT);
    ConsoleAppend(lispError, MC_TYPE_PROMPT);
    data = wxEmptyString;
  }
}

#ifndef __WXMSW__
void wxMaxima::ReadProcessOutput()
{
  wxString o;
  while (m_process->IsInputAvailable())
    o += m_input->GetC();

  int st = o.Find(wxT("Maxima"));
  if (st == -1)
    st = 0;

  FirstOutput(wxT("wxMaxima ")
              wxT(VERSION)
              wxT(" http://andrejv.github.io/wxmaxima/\n") +
              o.SubString(st, o.Length() - 1));
}
#endif

void wxMaxima::SetupVariables()
{
  SendMaxima(wxT(":lisp-quiet (setf *prompt-suffix* \"") +
             m_promptSuffix +
             wxT("\")"));
  SendMaxima(wxT(":lisp-quiet (setf *prompt-prefix* \"") +
             m_promptPrefix +
             wxT("\")"));
  SendMaxima(wxT(":lisp-quiet (setf $in_netmath nil)"));
  SendMaxima(wxT(":lisp-quiet (setf $show_openplot t)"));
  
  wxConfigBase *config = wxConfig::Get();
  
  bool wxcd;

  #if defined (__WXMSW__)
  wxcd = false;
  config->Read(wxT("wxcd"),&wxcd);
  #else
  wxcd = true;
  #endif
  
  if(wxcd) {
    SendMaxima(wxT(":lisp-quiet (defparameter $wxchangedir t)"));
  }
  else {
    SendMaxima(wxT(":lisp-quiet (defparameter $wxchangedir nil)"));
  }

#if defined (__WXMAC__)
  bool usepngCairo=true;
#else
  bool usepngCairo=false;
#endif
  config->Read(wxT("usepngCairo"),&usepngCairo);
  if(usepngCairo)
    SendMaxima(wxT(":lisp-quiet (defparameter $wxplot_pngcairo t)"));
  else
    SendMaxima(wxT(":lisp-quiet (defparameter $wxplot_pngcairo nil)"));
  
  int defaultPlotWidth = 600;
  config->Read(wxT("defaultPlotWidth"), &defaultPlotWidth);
  int defaultPlotHeight = 400;
  config->Read(wxT("defaultPlotHeight"), &defaultPlotHeight);
  SendMaxima(wxString::Format(wxT(":lisp-quiet (defparameter $wxplot_size '((mlist simp) %i %i))"),defaultPlotWidth,defaultPlotHeight));
  
#if defined (__WXMSW__)
  wxString cwd = wxGetCwd();
  cwd.Replace(wxT("\\"), wxT("/"));
  SendMaxima(wxT(":lisp-quiet ($load \"") + cwd + wxT("/data/wxmathml\")"));
#elif defined (__WXMAC__)
  wxString cwd = wxGetCwd();
  cwd = cwd + wxT("/") + wxT(MACPREFIX);
  SendMaxima(wxT(":lisp-quiet ($load \"") + cwd + wxT("wxmathml\")"));
  // check for Gnuplot.app - use it if it exists
  wxString gnuplotbin(wxT("/Applications/Gnuplot.app/Contents/Resources/bin/gnuplot"));
  if (wxFileExists(gnuplotbin))
    SendMaxima(wxT(":lisp-quiet (setf $gnuplot_command \"") + gnuplotbin + wxT("\")"));
#else
  wxString prefix = wxT(PREFIX);
  SendMaxima(wxT(":lisp-quiet ($load \"") + prefix +
             wxT("/share/wxMaxima/wxmathml\")"));
#endif

  if (m_currentFile != wxEmptyString)
  {
    wxString filename(m_currentFile);
    
    SetCWD(filename);
  }
}

///--------------------------------------------------------------------------------
///  Getting configuration
///--------------------------------------------------------------------------------

wxString wxMaxima::GetCommand(bool params)
{
#if defined (__WXMSW__)
  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxString maxima = wxGetCwd();
  wxString parameters;

  if (maxima.Right(8) == wxT("wxMaxima"))
    maxima.Replace(wxT("wxMaxima"), wxT("bin\\maxima.bat"));
  else
    maxima.Append("\\maxima.bat");

  if (!wxFileExists(maxima))
  {
    config->Read(wxT("maxima"), &maxima);
    if (!wxFileExists(maxima))
    {
      wxMessageBox(_("wxMaxima could not find Maxima!\n\n"
                     "Please configure wxMaxima with 'Edit->Configure'.\n"
                     "Then start Maxima with 'Maxima->Restart Maxima'."),
		   _("Warning"),
                   wxOK | wxICON_EXCLAMATION);
      SetStatusText(_("Please configure wxMaxima with 'Edit->Configure'."));
      return wxEmptyString;
    }
  }

  config->Read(wxT("parameters"), &parameters);
  if (params)
    return wxT("\"") + maxima + wxT("\" ") + parameters;
  return maxima;
#else
  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxString command, parameters;
  bool have_config = config->Read(wxT("maxima"), &command);

  //Fix wrong" maxima=1" paraneter in ~/.wxMaxima if upgrading from 0.7.0a
  if (!have_config || (have_config && command.IsSameAs (wxT("1"))))
  {
#if defined (__WXMAC__)
    if (wxFileExists("/Applications/Maxima.app"))
      command = wxT("/Applications/Maxima.app");
    else if (wxFileExists("/usr/local/bin/maxima"))
      command = wxT("/usr/local/bin/maxima");
    else
      command = wxT("maxima");
#else
    command = wxT("maxima");
#endif
    config->Write(wxT("maxima"), command);
  }

#if defined (__WXMAC__)
  if (command.Right(4) == wxT(".app")) // if pointing to a Maxima.app
    command.Append(wxT("/Contents/Resources/maxima.sh"));
#endif

  config->Read(wxT("parameters"), &parameters);
  command = wxT("\"") + command + wxT("\" ") + parameters;
  return command;
#endif
}

///--------------------------------------------------------------------------------
///  Tips and help
///--------------------------------------------------------------------------------

void wxMaxima::ShowTip(bool force)
{
  bool ShowTips = true;
  int tipNum = 0;
  wxConfig *config = (wxConfig *)wxConfig::Get();
  config->Read(wxT("ShowTips"), &ShowTips);
  config->Read(wxT("tipNum"), &tipNum);
  if (!ShowTips && !force)
    return ;
  wxString tips = wxT("tips.txt");
#if defined (__WXMSW__)
  wxString prefix = wxGetCwd() + wxT("\\data\\");
#elif defined (__WXMAC__)
  wxString prefix = wxT(MACPREFIX);
  prefix += wxT("/");
#else
  wxString prefix = wxT(PREFIX);
  prefix += wxT("/share/wxMaxima/");
#endif

  tips = prefix + tips;
  if (wxFileExists(tips))
  {
    MyTipProvider *t = new MyTipProvider(tips, tipNum);
    ShowTips = wxShowTip(this, t, ShowTips);
    config->Write(wxT("ShowTips"), ShowTips);
    tipNum = t->GetCurrentTip();
    config->Write(wxT("tipNum"), tipNum);
    config->Flush();
    delete t;
  }
  else
  {
    wxMessageBox(_("wxMaxima could not find tip files."
                   "\n\nPlease check your installation."),
                 _("Error"), wxICON_ERROR | wxOK);
  }
}

wxString wxMaxima::GetHelpFile()
{
#if defined __WXMSW__
  wxString command;
  wxString chm;

  command = GetCommand(false);

  if (command.empty())
    return wxEmptyString;

  command.Replace(wxT("bin\\maxima.bat"), wxT("share\\maxima"));

  chm = wxFindFirstFile(command + wxT("\\*"), wxDIR);

  if (chm.empty())
    return wxEmptyString;

  chm = chm + wxT("\\doc\\chm\\");

  wxString locale = wxGetApp().m_locale.GetCanonicalName().Left(2);

  if (wxFileExists(chm + locale + wxT("\\maxima.chm")))
    return chm + locale + wxT("\\maxima.chm");

  if (wxFileExists(chm + wxT("maxima.chm")))
    return chm + wxT("maxima.chm");

  return wxEmptyString;
#else
  wxString headerFile;
  wxConfig::Get()->Read(wxT("helpFile"), &headerFile);

  if (headerFile.Length() && wxFileExists(headerFile))
    return headerFile;
  else
    headerFile = wxEmptyString;

  wxProcess *process = new wxProcess(this, -1);
  process->Redirect();
  wxString command = GetCommand();
  command += wxT(" -d");
  wxArrayString output;
  wxExecute(command, output, wxEXEC_ASYNC);

  wxString line;
  wxString docdir;
  wxString langsubdir;

  for (unsigned int i=0; i<output.GetCount(); i++)
  {
    line = output[i];
    if (line.StartsWith(wxT("maxima-htmldir")))
      docdir = line.Mid(15);
    else if (line.StartsWith(wxT("maxima-lang-subdir")))
    {
      langsubdir = line.Mid(19);
      if (langsubdir == wxT("NIL"))
        langsubdir = wxEmptyString;
    }
  }

  if (docdir.Length() == 0)
    return wxEmptyString;

  headerFile = docdir + wxT("/");
  if (langsubdir.Length())
    headerFile += langsubdir + wxT("/");
  headerFile += wxT("header.hhp");

  if (!wxFileExists(headerFile))
    headerFile = docdir + wxT("/header.hhp");

  if (wxFileExists(headerFile))
    wxConfig::Get()->Write(wxT("helpFile"), headerFile);

  return headerFile;
#endif
}

void wxMaxima::ShowHTMLHelp(wxString helpfile,wxString otherhelpfile,wxString keyword)
{
#if defined (__WXMSW__)
  // Cygwin uses /c/something instead of c:/something and passes this path to the
  // web browser - which doesn't support cygwin paths => convert the path to a
  // native windows pathname if needed.
  if(helpfile.Length()>1 && helpfile[1]==wxT('/'))
  {
    helpfile[1]=helpfile[2];
    helpfile[2]=wxT(':');
  }
#endif

  if(!m_htmlHelpInitialized)
  {
    wxFileName otherhelpfilenname(otherhelpfile);
    if(otherhelpfilenname.FileExists())
      m_htmlhelpCtrl.AddBook(otherhelpfile);
    m_htmlhelpCtrl.AddBook(helpfile);
    m_htmlHelpInitialized = true;
  }
  
  if ((keyword == wxT("%")) ||
      (keyword == wxT(" << Graphics >> ")))
    m_htmlhelpCtrl.DisplayContents();
  else
    m_htmlhelpCtrl.KeywordSearch(keyword, wxHELP_SEARCH_INDEX);
}

#if defined (__WXMSW__)
void wxMaxima::ShowCHMHelp(wxString helpfile,wxString keyword)
{
  if (m_chmhelpFile != helpfile)
    m_chmhelpCtrl.LoadFile(helpfile);
  
  if ((keyword == wxT("%"))||
      (keyword == wxT(" << Graphics >> ")))
    m_chmhelpCtrl.DisplayContents();
  else
    m_chmhelpCtrl.KeywordSearch(keyword, wxHELP_SEARCH_INDEX);
}
#endif

void wxMaxima::ShowWxMaximaHelp()
{
  Dirstructure dirstructure;

  wxString htmldir = dirstructure.HelpDir();

#if CHM==true
  wxString helpfile = htmldir + wxT("wxmaxima.chm");
  ShowCHMHelp(helpfile,wxT("%"));
#else
  wxString helpfile = htmldir + wxT("wxmaxima.hhp");
  ShowHTMLHelp(helpfile,GetHelpFile(),wxT("%"));  
#endif
}

void wxMaxima::ShowMaximaHelp(wxString keyword)
{
  wxLogNull disableWarnings;
  wxString MaximaHelpFile = GetHelpFile();
  if (MaximaHelpFile.Length() == 0)
  {
    wxMessageBox(_("wxMaxima could not find help files."
                   "\n\nPlease check your installation."),
                 _("Error"), wxICON_ERROR | wxOK);
    return ;
  }
#if defined (__WXMSW__)
  ShowCHMHelp(MaximaHelpFile,keyword);
#else
  Dirstructure dirstructure;
  wxString htmldir = dirstructure.HelpDir();
  wxString wxMaximaHelpFile = htmldir + wxT("wxmaxima.hhp");

  ShowHTMLHelp(MaximaHelpFile,wxMaximaHelpFile,keyword);
#endif
}

///--------------------------------------------------------------------------------
///  Idle event
///--------------------------------------------------------------------------------

/***
 * On idle event we check if the document is saved.
 */
void wxMaxima::OnIdle(wxIdleEvent& event)
{
  ResetTitle(m_console->IsSaved());
  event.Skip();
}

///--------------------------------------------------------------------------------
///  Menu and button events
///--------------------------------------------------------------------------------

void wxMaxima::MenuCommand(wxString cmd)
{
  bool evaluating = !m_console->m_evaluationQueue->Empty();

  m_console->SetFocus();
//  m_console->SetSelection(NULL);
//  m_console->SetActiveCell(NULL);
  m_console->OpenHCaret(cmd);
  m_console->AddCellToEvaluationQueue(dynamic_cast<GroupCell*>(m_console->GetActiveCell()->GetParent()));
    if(!evaluating) TryEvaluateNextInQueue();
}

void wxMaxima::DumpProcessOutput()
{
  wxString o;
  o = _("Output from Maxima to stdout (there should be none):\n");
  while (m_process->IsInputAvailable())
  {
    o += m_input->GetC();
  }

  wxMessageBox(o, wxT("Process output (stdout)"));
  
  o = _("Output from Maxima to stderr (there should be none):\n");
  wxInputStream *error = m_process->GetErrorStream();
  while (m_process->IsErrorAvailable())
  {
    o += error->GetC();
  }

  wxMessageBox(o, wxT("Process output (stderr)"));

}

///--------------------------------------------------------------------------------
///  Menu and button events
///--------------------------------------------------------------------------------

void wxMaxima::PrintMenu(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case wxID_PRINT:
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_print:
#endif
  {
    wxPrintDialogData printDialogData;
    if (m_printData)
      printDialogData.SetPrintData(*m_printData);
    wxPrinter printer(&printDialogData);
    wxString title(_("wxMaxima document")), suffix;

    if (m_currentFile.Length())
    {
      wxString suffix;
      wxFileName::SplitPath(m_currentFile, NULL, NULL, &title, &suffix);
      title << wxT(".") << suffix;
    }

    MathPrintout printout(title);
    MathCell* copy = m_console->CopyTree();
    printout.SetData(copy);
    if (printer.Print(this, &printout, true)) {
      if (m_printData != NULL)
        delete m_printData;
      m_printData = new wxPrintData(printer.GetPrintDialogData().GetPrintData());
    }
    break;
  }
  }
}

void wxMaxima::UpdateMenus(wxUpdateUIEvent& event)
{
  wxMenuBar* menubar = GetMenuBar();

  if(m_console)
    wxASSERT_MSG((!m_console->HCaretActive())||(m_console->GetActiveCell()==NULL),_(wxT("Both horizontal and vertical cursor active at the same time")));

  menubar->Enable(menu_copy_from_console, m_console->CanCopy(true));
  menubar->Enable(menu_cut, m_console->CanCut());
  menubar->Enable(menu_copy_tex_from_console, m_console->CanCopy());
#if defined __WXMSW__ || defined __WXMAC__
  menubar->Enable(menu_copy_as_bitmap, m_console->CanCopy());
#endif
  menubar->Enable(menu_copy_to_file, m_console->CanCopy());
  menubar->Enable(menu_copy_text_from_console, m_console->CanCopy());
  menubar->Enable(menu_select_all, m_console->GetTree() != NULL);
  menubar->Enable(menu_undo, m_console->CanUndo());
  menubar->Enable(menu_redo, m_console->CanRedo());
  menubar->Enable(menu_interrupt_id, m_pid>0);
  menubar->Enable(menu_evaluate_all_visible, m_console->GetTree() != NULL);
  menubar->Enable(ToolBar::tb_evaltillhere,
                  (m_console->GetTree() != NULL) &&
                  (m_console->CanPaste()) &&
                  (m_console->GetHCaret() != NULL)
    );  
  menubar->Enable(menu_save_id, (!m_fileSaved)&&(!m_saving));
  menubar->Enable(menu_export_html, !m_saving);

  for (int id = menu_pane_math; id<=menu_pane_format; id++)
    menubar->Check(id, IsPaneDisplayed(static_cast<Event>(id)));
  if (GetToolBar() != NULL)
    menubar->Check(menu_show_toolbar, true);
  else
    menubar->Check(menu_show_toolbar, false);

  if (m_console->GetTree() != NULL)
  {
    menubar->Enable(MathCtrl::popid_divide_cell, m_console->GetActiveCell() != NULL);
    menubar->Enable(MathCtrl::popid_merge_cells, m_console->CanMergeSelection());
    menubar->Enable(wxID_PRINT, true);
  }
  else
  {
    menubar->Enable(MathCtrl::popid_divide_cell, false);
    menubar->Enable(MathCtrl::popid_merge_cells, false);
    menubar->Enable(wxID_PRINT, false);
  }
  double zf = m_console->GetZoomFactor();
  if (zf < 3.0)
    menubar->Enable(menu_zoom_in, true);
  else
    menubar->Enable(menu_zoom_in, false);
  if (zf > 0.8)
    menubar->Enable(menu_zoom_out, true);
  else
    menubar->Enable(menu_zoom_out, false);

}

#if defined (__WXMSW__) || defined (__WXGTK20__) || defined(__WXMAC__)

void wxMaxima::UpdateToolBar(wxUpdateUIEvent& event)
{
  if(!m_console->m_mainToolBar)
    return;
  
  m_console->m_mainToolBar->EnableTool(ToolBar::tb_copy,  m_console->CanCopy(true));
  m_console->m_mainToolBar->EnableTool(ToolBar::tb_cut, m_console->CanCut());
  m_console->m_mainToolBar->EnableTool(ToolBar::tb_save, (!m_fileSaved)&&(!m_saving));
  /*
    The interrupt button is now automatically enabled when maxima
    is actually working and disabled when it isn't.
    The code that does this can be found in StatusMaximaBusy().
    The old code was:

    if (m_pid > 0)
    m_console->m_mainToolBar->EnableTool(ToolBar::tb_interrupt, true);
    else
    m_console->m_mainToolBar->EnableTool(ToolBar::tb_interrupt, false);
  */
  if (m_console->GetTree() != NULL)
    m_console->m_mainToolBar->EnableTool(ToolBar::tb_print, true);
  else
    m_console->m_mainToolBar->EnableTool(ToolBar::tb_print, false);
  
  m_console->m_mainToolBar->EnableTool(ToolBar::tb_evaltillhere,
                                       (m_console->GetTree() != NULL) &&
                                       (m_console->CanPaste()) &&
                                       (m_console->GetHCaret() != NULL)
    );

  // On MSW it seems we cannot change an icon without side-effects that somehow
  // stop the animation => on this OS we have separate icons for the
  // animation start and stop. On the rest of the OSes we use one combined
  // start/stop button instead.
  if (m_console->CanAnimate())
  {
    if(m_console->AnimationRunning())
      m_console->m_mainToolBar->AnimationButtonState(ToolBar::Running);
    else
      m_console->m_mainToolBar->AnimationButtonState(ToolBar::Stopped);
  }
  else
    m_console->m_mainToolBar->AnimationButtonState(ToolBar::Inactive);
}

#endif

wxString wxMaxima::ExtractFirstExpression(wxString entry)
{
  int semicolon = entry.Find(';');
  int dollar = entry.Find('$');
  bool semiFound = (semicolon != wxNOT_FOUND);
  bool dollarFound = (dollar != wxNOT_FOUND);

  int index;
  if (semiFound && dollarFound)
    index = MIN(semicolon, dollar);
  else if (semiFound && !dollarFound)
    index = semicolon;
  else if (!semiFound && dollarFound)
    index = dollar;
  else // neither semicolon nor dollar found
    index = entry.Length();

  return entry.SubString(0, index - 1);
}

wxString wxMaxima::GetDefaultEntry()
{
  if (m_console->CanCopy(true))
    return (m_console->GetString()).Trim().Trim(false);
  if (m_console->GetActiveCell() != NULL)
    return ExtractFirstExpression(m_console->GetActiveCell()->ToString());
  return wxT("%");
}

void wxMaxima::OpenFile(wxString file, wxString cmd)
{
  if (file.Length() && wxFileExists(file))
  {
    AddRecentDocument(file);

    m_lastPath = wxPathOnly(file);
    wxString unixFilename(file);
#if defined __WXMSW__
    unixFilename.Replace(wxT("\\"), wxT("/"));
#endif

    if (cmd.Length() > 0)
    {
      MenuCommand(cmd + wxT("(\"") + unixFilename + wxT("\")$"));
    }

    else if (file.Right(4) == wxT(".wxm"))
      OpenWXMFile(file, m_console);

    else if (file.Right(5) == wxT(".wxmx"))
      OpenWXMXFile(file, m_console); // clearDocument = true

    else if (file.Right(4) == wxT(".dem"))
      MenuCommand(wxT("demo(\"") + unixFilename + wxT("\")$"));

    else
      MenuCommand(wxT("load(\"") + unixFilename + wxT("\")$"));
  }
  
  if((m_autoSaveInterval > 10000) && (m_currentFile.Length() > 0))
    m_autoSaveTimer.StartOnce(m_autoSaveInterval);

  if(m_console)m_console->TreeUndo_ClearBuffers();
}

bool wxMaxima::SaveFile(bool forceSave)
{  
  wxString file = m_currentFile;
  wxString fileExt=wxT("wxmx");
  int ext=0;
  
  wxConfig *config = (wxConfig *)wxConfig::Get();
  
  if (file.Length() == 0 || forceSave)
  {
    if (file.Length() == 0) {
      config->Read(wxT("defaultExt"), &fileExt);
      file = _("untitled") + wxT(".") + fileExt;
    }
    else
      wxFileName::SplitPath(file, NULL, NULL, &file, &fileExt);
      
    wxFileDialog fileDialog(this,
                            _("Save As"), m_lastPath,
                            file,
                            _(  "wxMaxima xml document (*.wxmx)|*.wxmx|"
                                "wxMaxima document (*.wxm)|*.wxm|"
                                "Maxima batch file (*.mac)|*.mac"),
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      
    if (fileExt == wxT("wxm"))
      fileDialog.SetFilterIndex(1);
    else if (fileExt == wxT("wxmx"))
      fileDialog.SetFilterIndex(0);
    else if (fileExt == wxT("mac"))
      fileDialog.SetFilterIndex(2);
    else
      fileDialog.SetFilterIndex(0);
        
    if (fileDialog.ShowModal() == wxID_OK)
    {
      file = fileDialog.GetPath();
      ext = fileDialog.GetFilterIndex();
    }
    else
    {
      m_autoSaveTimer.StartOnce(m_autoSaveInterval);
      m_saving = false;
      return false;
    }
  }

  if (file.Length())
  {
    if((file.Right(4) != wxT(".wxm"))&&
       (file.Right(5) != wxT(".wxmx"))&&
       (file.Right(4) != wxT(".mac"))
      )
    {
      switch(ext)
      {
      case 1:
        file += wxT(".wxm");
        break;
      case 0:
        file += wxT(".wxmx");
        break;
      case 2:
        file += wxT(".mac");
        break;
      }
    }
      
    StatusSaveStart();

    m_currentFile = file;
    m_lastPath = wxPathOnly(file);
    if (file.Right(5) == wxT(".wxmx")) {
      if (!m_console->ExportToWXMX(file))
      {
        StatusSaveFailed();
        if(m_autoSaveInterval > 10000)
          m_autoSaveTimer.StartOnce(m_autoSaveInterval);
        m_saving = false;
        return false;
      }
	
      config->Write(wxT("defaultExt"), wxT("wxmx"));
    }
    else {
      if (!m_console->ExportToMAC(file))
      {
        if (file.Right(4) == wxT(".mac"))
          config->Write(wxT("defaultExt"), wxT("mac"));
        else
          config->Write(wxT("defaultExt"), wxT("wxm"));
	    
        StatusSaveFailed();
        if(m_autoSaveInterval > 10000)
          m_autoSaveTimer.StartOnce(m_autoSaveInterval);
        m_saving = false;
        return false;
      }
    }

    AddRecentDocument(file);
    SetCWD(file);

    if(m_autoSaveInterval > 10000)
      m_autoSaveTimer.StartOnce(m_autoSaveInterval);
    StatusSaveFinished();
    m_saving = false;
    return true;
  }

  if(m_autoSaveInterval > 10000)
    m_autoSaveTimer.StartOnce(m_autoSaveInterval);
  m_saving = false;
  
  return false;
}

void wxMaxima::OnTimerEvent(wxTimerEvent& event)
{
  switch (event.GetId()) {
  case KEYBOARD_INACTIVITY_TIMER_ID:
    m_console->m_keyboardInactive = true;
    if((m_autoSaveIntervalExpired) && (m_currentFile.Length() > 0) && SaveNecessary())
    {
      if(!m_saving)SaveFile(false);
      m_autoSaveIntervalExpired = false;
      if(m_autoSaveInterval > 10000)
        m_autoSaveTimer.StartOnce(m_autoSaveInterval);
    }
    break;
  case AUTO_SAVE_TIMER_ID:
    m_autoSaveIntervalExpired = true;
    if((m_console->m_keyboardInactive) && (m_currentFile.Length() > 0) && SaveNecessary())
    {
      if(!m_saving)SaveFile(false);
	
      if(m_autoSaveInterval > 10000)
        m_autoSaveTimer.StartOnce(m_autoSaveInterval);
      m_autoSaveIntervalExpired = false;
    }
    break;
  }
}

void wxMaxima::FileMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  bool forceSave = false;
#if defined __WXMSW__
  wxString b = wxT("\\");
  wxString f = wxT("/");
#endif

  switch (event.GetId())
  {
#if defined __WXMAC__
  case mac_closeId:
    Close();
    break;
#elif defined __WXMSW__ || defined __WXGTK20__
  case menu_new_id:
  case ToolBar::tb_new:
    wxExecute(wxTheApp->argv[0]);
    break;
#endif
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_open:
#endif
  case menu_open_id:
  {
    if (SaveNecessary()) {
      int close = SaveDocumentP();
	
      if (close == wxID_CANCEL)
        return;
	
      if (close == wxID_YES) {
        if (!SaveFile())
          return;
      }
    }

    wxString file = wxFileSelector(_("Open"), m_lastPath,
                                   wxEmptyString, wxEmptyString,
                                   _("wxMaxima document (*.wxm, *.wxmx)|*.wxm;*.wxmx"),
                                   wxFD_OPEN);

    OpenFile(file);
  }
  break;

  case menu_save_as_id:
    forceSave = true;
    m_fileSaved = false;

#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_save:
#endif
  case menu_save_id:
    SaveFile(forceSave);
    break;

  case menu_export_html:
  {
    m_saving = true;
    // Saving calls wxYield(), exporting does do so, too => we need to
    // avoid triggering an autosave during export.
    m_autoSaveTimer.Stop();
    
    // Determine a sane default file name;
    wxString file = m_currentFile;

    if (file.Length() == 0)
      file = _("untitled");
    else
      wxFileName::SplitPath(file, NULL, NULL, &file, NULL);
    
    wxString fileExt="html";
    wxConfig::Get()->Read(wxT("defaultExportExt"), &fileExt);

    wxFileDialog fileDialog(this,
                            _("Export"), m_lastPath,
                            file + wxT(".") + fileExt,
                            _("HTML file (*.html)|*.html|"
                              "pdfLaTeX file (*.tex)|*.tex"),
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    
    if (fileExt == wxT("html"))
      fileDialog.SetFilterIndex(0);
    else fileDialog.SetFilterIndex(1);
    
    if (fileDialog.ShowModal() == wxID_OK)
    {
      file = fileDialog.GetPath();
      if (file.Length())
      {
        int ext = fileDialog.GetFilterIndex();
        if((file.Right(5) != wxT(".html"))&&
           (file.Right(4) != wxT(".tex"))
          )
        {
          switch(ext)
          {
          case 0:
            file += wxT(".html");
            break;
          case 1:
            file += wxT(".tex");
            break;
          default: 
            file += wxT(".html");
          }
        }
	    
        if (file.Right(4) == wxT(".tex")) {
          StatusExportStart();

          if (!m_console->ExportToTeX(file))
          {
            wxMessageBox(_("Exporting to TeX failed!"), _("Error!"),
                         wxOK);
            StatusExportFailed();
          }
          else
            StatusExportFinished();
        }
        else {
          StatusExportStart();
          if (!m_console->ExportToHTML(file))
          {
            wxMessageBox(_("Exporting to HTML failed!"), _("Error!"),
                         wxOK);
            StatusExportFailed();
          }
          else
            StatusExportFinished();
        }
        if(m_autoSaveInterval > 10000)
          m_autoSaveTimer.StartOnce(m_autoSaveInterval);   

        wxFileName::SplitPath(file, NULL, NULL, NULL, &fileExt);
        wxConfig::Get()->Write(wxT("defaultExportExt"), fileExt);
	      
      }
    }
  }
  m_saving = false;
  break;
    
  case menu_load_id:
  {
    wxString file = wxFileSelector(_("Load Package"), m_lastPath,
                                   wxEmptyString, wxEmptyString,
                                   _("Maxima package (*.mac)|*.mac|"
                                     "Lisp package (*.lisp)|*.lisp|All|*"),
                                   wxFD_OPEN);
    OpenFile(file, wxT("load"));
  }
  break;

  case menu_batch_id:
  {
    wxString file = wxFileSelector(_("Batch File"), m_lastPath,
                                   wxEmptyString, wxEmptyString,
                                   _("Maxima package (*.mac)|*.mac"),
                                   wxFD_OPEN);
    OpenFile(file, wxT("batch"));
  }
  break;

  case wxID_EXIT:
    Close();
    break;

  case ToolBar::tb_animation_startStop:
    if (m_console->CanAnimate())
    {
      if(m_console->AnimationRunning())
        m_console->Animate(false);
      else
        m_console->Animate(true);      
    }
    break;
    
  case MathCtrl::popid_animation_start:
    if (m_console->CanAnimate() && !m_console->AnimationRunning())
      m_console->Animate(true);
    break;
    
  default:
    break;
  }
}

void wxMaxima::EditMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  if (m_findDialog != NULL) {
    event.Skip();
    return;
  }

  switch (event.GetId())
  {
  case wxID_PREFERENCES:
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_pref:
#endif
  {
    wxConfigBase *config = wxConfig::Get();
    
#if defined (__WXMAC__)
    bool pngcairo_old=true;
#else
    bool pngcairo_old=false;
#endif
    config->Read(wxT("usepngCairo"),&pngcairo_old);
      
    Config *configW = new Config(this);
    configW->Centre(wxBOTH);
    if (configW->ShowModal() == wxID_OK)
    {
      configW->WriteSettings();
      // Write the changes in the configuration to the disk.
      config->Flush();
      // Refresh the display as the settings that affect it might have changed.
      m_console->RecalculateForce();
      m_console->Refresh();
    }

    configW->Destroy();

#if defined (__WXMSW__)
    bool wxcd = false;
    config->Read(wxT("wxcd"),&wxcd);
    if(wxcd)
    {
      SendMaxima(wxT(":lisp-quiet (setq $wxchangedir t)"));
      if (m_currentFile != wxEmptyString)
      {
        wxString filename(m_currentFile);
        SetCWD(filename);
      }
    }
    else
    {
      SetCWD(wxStandardPaths::Get().GetExecutablePath());
      SendMaxima(wxT(":lisp-quiet (setq $wxchangedir nil)"));
    }
#endif
    
#if defined (__WXMAC__)
    bool usepngCairo=true;
#else
    bool usepngCairo=false;
#endif
    config->Read(wxT("usepngCairo"),&usepngCairo);
    if(usepngCairo != pngcairo_old)
    {
      if(usepngCairo)
        SendMaxima(wxT(":lisp-quiet (setq $wxplot_pngcairo t)"));
      else
        SendMaxima(wxT(":lisp-quiet (setq $wxplot_pngcairo nil)"));
    }
      
    m_autoSaveInterval = 0;
    config->Read(wxT("autoSaveInterval"), &m_autoSaveInterval);
    m_autoSaveInterval *= 60000;

    if((m_autoSaveInterval > 10000) && (m_currentFile.Length() > 0 ))
      m_autoSaveTimer.StartOnce(m_autoSaveInterval);
    else
      m_autoSaveTimer.Stop();
      
    int defaultPlotWidth = 800;
    config->Read(wxT("defaultPlotWidth"), &defaultPlotWidth);
    int defaultPlotHeight = 600;
    config->Read(wxT("defaultPlotHeight"), &defaultPlotHeight);
    //      SendMaxima(wxString::Format(wxT(":lisp-quiet (setq $wxplot_size '((mlist simp) %i %i))"),defaultPlotWidth,defaultPlotHeight));  
  }
  break;
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_copy:
#endif
  case menu_copy_from_console:
    if (m_console->CanCopy(true))
      m_console->Copy();
    break;
  case menu_copy_text_from_console:
    if (m_console->CanCopy(true))
      m_console->Copy(true);
    break;
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_cut:
#endif
  case menu_cut:
    if (m_console->CanCut())
      m_console->CutToClipboard();
    break;
  case menu_select_all:
  case ToolBar::tb_select_all:
    m_console->SelectAll();
    break;
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_paste:
#endif
  case menu_paste:
    if (m_console->CanPaste())
      m_console->PasteFromClipboard();
    break;
  case menu_undo:
    if (m_console->CanUndo())
      m_console->Undo();
    break;
  case menu_redo:
    if (m_console->CanRedo())
      m_console->Redo();
    break;
  case menu_copy_tex_from_console:
    if (m_console->CanCopy())
      m_console->CopyTeX();
    break;
  case menu_copy_as_bitmap:
    if (m_console->CanCopy())
      m_console->CopyBitmap();
    break;
  case menu_copy_to_file:
  {
    wxString file = wxFileSelector(_("Save Selection to Image"), m_lastPath,
                                   wxT("image.png"), wxT("png"),
                                   _("PNG image (*.png)|*.png|"
                                     "JPEG image (*.jpg)|*.jpg|"
                                     "Windows bitmap (*.bmp)|*.bmp|"
                                     "X pixmap (*.xpm)|*.xpm"),
                                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (file.Length())
    {
      m_console->CopyToFile(file);
      m_lastPath = wxPathOnly(file);
    }
  }
  break;
  case MathCtrl::popid_delete:
    if (m_console->CanDeleteSelection())
    {
      m_console->DeleteSelection();
      m_console->Recalculate();
      m_console->Refresh();
      return;
    }
    break;
  case menu_zoom_in:
    if (m_console->GetZoomFactor() < 3.0) {
      m_console->SetZoomFactor(m_console->GetZoomFactor() + 0.1);
      wxString message = _("Zoom set to ");
      message << int(100.0 * m_console->GetZoomFactor()) << wxT("%");
      SetStatusText(message, 1);
    }
    break;
  case menu_zoom_out:
    if (m_console->GetZoomFactor() > 0.8) {
      m_console->SetZoomFactor(m_console->GetZoomFactor() - 0.1);
      wxString message = _("Zoom set to ");
      message << int(100.0 * m_console->GetZoomFactor()) << wxT("%");
      SetStatusText(message, 1);
    }
    break;
  case menu_zoom_80:
    m_console->SetZoomFactor(0.8);
    break;
  case menu_zoom_100:
    m_console->SetZoomFactor(1.0);
    break;
  case menu_zoom_120:
    m_console->SetZoomFactor(1.2);
    break;
  case menu_zoom_150:
    m_console->SetZoomFactor(1.5);
    break;
  case menu_zoom_200:
    m_console->SetZoomFactor(2.0);
    break;
  case menu_zoom_300:
    m_console->SetZoomFactor(3.0);
    break;
  case menu_fullscreen:
    ShowFullScreen( !IsFullScreen() );
    break;
  case menu_remove_output:
    m_console->RemoveAllOutput();
    break;
  case menu_show_toolbar:
    ShowToolBar(!(GetToolBar() != NULL));
    break;
  case menu_edit_find:
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_find:
#endif
    if ( m_findDialog != NULL )
    {
      delete m_findDialog;
      m_findDialog = NULL;
    }
    else
    {
      m_findDialog = new wxFindReplaceDialog(
        this,
        &m_findData,
        _("Find and Replace"),
        wxFR_REPLACEDIALOG |
        wxFR_NOWHOLEWORD);
      m_findDialog->Show(true);
    }
    break;
  case menu_history_next:
  {
    wxString command = m_history->GetCommand(true);
    if (command != wxEmptyString)
      m_console->SetActiveCellText(command);
  }
  break;
  case menu_history_previous:
  {
    wxString command = m_history->GetCommand(false);
    if (command != wxEmptyString)
      m_console->SetActiveCellText(command);
  }
  break;
  }
}

void wxMaxima::OnFind(wxFindDialogEvent& event)
{
  if (!m_console->FindNext(event.GetFindString(),
                           event.GetFlags() & wxFR_DOWN,
                           !(event.GetFlags() & wxFR_MATCHCASE)))
    wxMessageBox(_("No matches found!"));
}

void wxMaxima::OnFindClose(wxFindDialogEvent& event)
{
  m_findDialog->Destroy();
  m_findDialog = NULL;
}

void wxMaxima::OnReplace(wxFindDialogEvent& event)
{
  m_console->Replace(event.GetFindString(), event.GetReplaceString());

  if (!m_console->FindNext(event.GetFindString(),
                           event.GetFlags() & wxFR_DOWN,
                           !(event.GetFlags() & wxFR_MATCHCASE)))
    wxMessageBox(_("No matches found!"));
}

void wxMaxima::OnReplaceAll(wxFindDialogEvent& event)
{
  int count = m_console->ReplaceAll(event.GetFindString(), event.GetReplaceString());

  wxMessageBox(wxString::Format(_("Replaced %d occurrences."), count));
}

void wxMaxima::MaximaMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  wxString b = wxT("\\");
  wxString f = wxT("/");
  switch (event.GetId())
  {
  case ToolBar::menu_restart_id:
    m_closing = true;
    m_console->ClearEvaluationQueue();
    m_console->ResetInputPrompts();
    StartMaxima();
    break;
  case menu_soft_restart:
    MenuCommand(wxT("kill(all);"));
    break;
  case menu_functions:
    MenuCommand(wxT("functions;"));
    break;
  case menu_variables:
    MenuCommand(wxT("values;"));
    break;
  case menu_display:
  {
    wxString choices[] =
      {
        wxT("xml"), wxT("ascii"), wxT("none")
      };
    wxString choice = wxGetSingleChoice(
      _("Select math display algorithm"),
      _("Display algorithm"),
      3,
      choices,
      this
      );
    if (choice.Length())
    {
      cmd = wxT("set_display('") + choice + wxT(")$");
      MenuCommand(cmd);
    }
  }
  break;
  case menu_texform:
    cmd = wxT("tex(") + expr + wxT(")$");
    MenuCommand(cmd);
    break;
  case menu_time:
    cmd = wxT("if showtime#false then showtime:false else showtime:all$");
    MenuCommand(cmd);
    break;
  case menu_fun_def:
    cmd = GetTextFromUser(_("Show the definition of function:"),
                          _("Function"), wxEmptyString, this);
    if (cmd.Length())
    {
      cmd = wxT("fundef(") + cmd + wxT(");");
      MenuCommand(cmd);
    }
    break;
  case menu_add_path:
  {
    if (m_lastPath.Length() == 0)
      m_lastPath = wxGetHomeDir();
    wxString dir = wxDirSelector(_("Add dir to path:"), m_lastPath);
    if (dir.Length())
    {
      m_lastPath = dir;
#if defined (__WXMSW__)
      dir.Replace(wxT("\\"), wxT("/"));
#endif
      cmd = wxT("file_search_maxima : cons(sconcat(\"") + dir +
        wxT("/###.{lisp,mac,mc}\"), file_search_maxima)$");
      MenuCommand(cmd);
    }
  }
  break;
  case menu_evaluate_all_visible:
  {
    bool evaluating = !m_console->m_evaluationQueue->Empty();
    if(!m_isConnected)
      StartMaxima();
    m_console->AddDocumentToEvaluationQueue();
    if(!evaluating) TryEvaluateNextInQueue();
  }
  break;
  case menu_evaluate_all:
  {
    bool evaluating = !m_console->m_evaluationQueue->Empty();
    if(!m_isConnected)
      StartMaxima();
    m_console->AddEntireDocumentToEvaluationQueue();
    if(!evaluating) TryEvaluateNextInQueue();
  }
  break;
  case ToolBar::tb_evaltillhere:
  {
    bool evaluating = !m_console->m_evaluationQueue->Empty();
    if(!m_isConnected)
      StartMaxima();
    m_console->AddDocumentTillHereToEvaluationQueue();
    if(!evaluating) TryEvaluateNextInQueue();
  }
  break;
  case menu_clear_var:
    cmd = GetTextFromUser(_("Delete variable(s):"), _("Delete"),
                          wxT("all"), this);
    if (cmd.Length())
    {
      cmd = wxT("remvalue(") + cmd + wxT(");");
      MenuCommand(cmd);
    }
    break;
  case menu_clear_fun:
    cmd = GetTextFromUser(_("Delete function(s):"), _("Delete"),
                          wxT("all"), this);
    if (cmd.Length())
    {
      cmd = wxT("remfunction(") + cmd + wxT(");");
      MenuCommand(cmd);
    }
    break;
  case menu_subst:
  case button_subst:
  {
    SubstituteWiz *wiz = new SubstituteWiz(this, -1, _("Substitute"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  default:
    break;
  }
}

void wxMaxima::EquationsMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_allroots:
    cmd = wxT("allroots(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_bfallroots:
    cmd = wxT("bfallroots(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_realroots:
    cmd = wxT("realroots(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_solve:
  case menu_solve:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Equation(s):"), _("Variable(s):"),
                               expr, wxT("x"), this, -1, _("Solve"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("solve([") + wiz->GetValue1() + wxT("], [") +
        wiz->GetValue2() + wxT("]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_solve_to_poly:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Equation(s):"), _("Variable(s):"),
                               expr, wxT("x"), this, -1, _("Solve"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("to_poly_solve([") + wiz->GetValue1() + wxT("], [") +
        wiz->GetValue2() + wxT("]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_solve_num:
  {
    if (expr.StartsWith(wxT("%")))
      expr = wxT("''(") + expr + wxT(")");
    Gen4Wiz *wiz = new Gen4Wiz(_("Equation:"), _("Variable:"),
                               _("Lower bound:"), _("Upper bound:"),
                               expr, wxT("x"), wxT("-1"), wxT("1"),
                               this, -1, _("Find root"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("find_root(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") +
        wiz->GetValue3() + wxT(", ") +
        wiz->GetValue4() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case button_solve_ode:
  case menu_solve_ode:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Equation:"), _("Function:"), _("Variable:"),
                               expr, wxT("y"), wxT("x"),
                               this, -1, _("Solve ODE"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("ode2(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_ivp_1:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Solution:"), _("Point:"), _("Value:"),
                               expr, wxT("x="), wxT("y="),
                               this, -1, _("IC1"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("ic1(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_ivp_2:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Solution:"), _("Point:"),
                               _("Value:"), _("Derivative:"),
                               expr, wxT("x="), wxT("y="), wxT("'diff(y,x)="),
                               this, -1, _("IC2"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("ic2(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") + wiz->GetValue3() +
        wxT(", ") + wiz->GetValue4() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_bvp:
  {
    BC2Wiz *wiz = new BC2Wiz(this, -1, _("BC2"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_eliminate:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Equations:"),
                               _("Variables:"), expr, wxEmptyString,
                               this, -1, _("Eliminate"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("eliminate([") + wiz->GetValue1() + wxT("],[")
        + wiz->GetValue2() + wxT("]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_solve_algsys:
  {
    wxString sz = GetTextFromUser(_("Number of equations:"),
                                  _("Solve algebraic system"),
                                  wxT("3"), this);
    if (sz.Length() == 0)
      return ;
    long isz;
    if (!sz.ToLong(&isz) || isz <= 0)
    {
      wxMessageBox(_("Not a valid number of equations!"), _("Error!"),
                   wxOK | wxICON_ERROR);
      return ;
    }
    SysWiz *wiz = new SysWiz(this, -1, _("Solve algebraic system"), isz);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("algsys") + wiz->GetValue();
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_solve_lin:
  {
    wxString sz = GetTextFromUser(_("Number of equations:"),
                                  _("Solve linear system"),
                                  wxT("3"), this);
    if (sz.Length() == 0)
      return ;
    long isz;
    if (!sz.ToLong(&isz) || isz <= 0)
    {
      wxMessageBox(_("Not a valid number of equations!"), _("Error!"),
                   wxOK | wxICON_ERROR);
      return ;
    }
    SysWiz *wiz = new SysWiz(this, -1, _("Solve linear system"), isz);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("linsolve") + wiz->GetValue();
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_solve_de:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Equation(s):"), _("Function(s):"),
                               expr, wxT("y(x)"),
                               this, -1, _("Solve ODE"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("desolve([") + wiz->GetValue1() + wxT("],[")
        + wiz->GetValue2() + wxT("]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_atvalue:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("Point:"),
                               _("Value:"), expr, wxT("x=0"), wxT("0"),
                               this, -1, _("At value"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("atvalue(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() +
        wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  default:
    break;
  }
}

void wxMaxima::AlgebraMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_invert_mat:
    cmd = wxT("invert(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_determinant:
    cmd = wxT("determinant(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_eigen:
    cmd = wxT("eigenvalues(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_eigvect:
    cmd = wxT("eigenvectors(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_adjoint_mat:
    cmd = wxT("adjoint(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_transpose:
    cmd = wxT("transpose(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_map_mat:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Function:"), _("Matrix:"),
                               wxEmptyString, expr,
                               this, -1, _("Matrix map"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("matrixmap(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_enter_mat:
  case menu_stats_enterm:
  {
    MatDim *wiz = new MatDim(this, -1, _("Matrix"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      if (wiz->GetValue0() != wxEmptyString)
        cmd = wiz->GetValue0() + wxT(": ");
      long w, h;
      int type = wiz->GetMatrixType();
      if (!(wiz->GetValue2()).ToLong(&w) ||
          !(wiz->GetValue1()).ToLong(&h) ||
          w <= 0 || h <= 0)
      {
        wxMessageBox(_("Not a valid matrix dimension!"), _("Error!"),
                     wxOK | wxICON_ERROR);
        return ;
      }
      if (w != h)
        type = MatWiz::MATRIX_GENERAL;
      MatWiz *mwiz = new MatWiz(this, -1, _("Enter matrix"),
                                type, w, h);
      mwiz->Centre(wxBOTH);
      if (mwiz->ShowModal() == wxID_OK)
      {
        cmd += mwiz->GetValue();
        MenuCommand(cmd);
      }
      mwiz->Destroy();
    }
    wiz->Destroy();
  }
  break;
  case menu_cpoly:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Matrix:"), _("Variable:"),
                               expr, wxT("x"),
                               this, -1, _("Char poly"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("charpoly(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT("), expand;");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_gen_mat:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Array:"), _("Width:"), _("Height:"), _("Name:"),
                               expr, wxT("3"), wxT("3"), wxEmptyString,
                               this, -1, _("Generate Matrix"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("genmatrix(") + wiz->GetValue1() +
        wxT(", ") + wiz->GetValue2() +
        wxT(", ") + wiz->GetValue3() + wxT(");");
      if (wiz->GetValue4() != wxEmptyString)
        val = wiz->GetValue4() + wxT(": ") + val;
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_gen_mat_lambda:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("matrix[i,j]:"), _("Width:"), _("Height:"), _("Name:"),
                               expr, wxT("3"), wxT("3"), wxEmptyString,
                               this, -1, _("Generate Matrix"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("genmatrix(lambda([i,j], ") + wiz->GetValue1() +
        wxT("), ") + wiz->GetValue2() +
        wxT(", ") + wiz->GetValue3() + wxT(");");
      if (wiz->GetValue4() != wxEmptyString)
        val = wiz->GetValue4() + wxT(": ") + val;
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_map:
  case menu_map:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Function:"), _("List:"),
                               wxEmptyString, expr,
                               this, -1, _("Map"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("map(") + wiz->GetValue1() + wxT(", ") + wiz->GetValue2() +
        wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_make_list:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Expression:"), _("Variable:"),
                               _("From:"), _("To:"),
                               expr, wxT("k"), wxT("1"), wxT("10"),
                               this, -1, _("Make list"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("makelist(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") +
        wiz->GetValue3() + wxT(", ") +
        wiz->GetValue4() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_apply:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Function:"), _("List:"),
                               wxT("\"+\""), expr,
                               this, -1, _("Apply"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("apply(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  default:
    break;
  }
}

void wxMaxima::SimplifyMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_nouns:
    cmd = wxT("ev(") + expr + wxT(", nouns);");
    MenuCommand(cmd);
    break;
  case button_ratsimp:
  case menu_ratsimp:
    cmd = wxT("ratsimp(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_radcan:
  case menu_radsimp:
    cmd = wxT("radcan(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_to_fact:
    cmd = wxT("makefact(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_to_gamma:
    cmd = wxT("makegamma(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_factcomb:
    cmd = wxT("factcomb(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_factsimp:
    cmd = wxT("minfactorial(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_logcontract:
    cmd = wxT("logcontract(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_logexpand:
    cmd = expr + wxT(", logexpand=super;");
    MenuCommand(cmd);
    break;
  case button_expand:
  case menu_expand:
    cmd = wxT("expand(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_factor:
  case menu_factor:
    cmd = wxT("factor(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_gfactor:
    cmd = wxT("gfactor(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_trigreduce:
  case menu_trigreduce:
    cmd = wxT("trigreduce(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_trigsimp:
  case menu_trigsimp:
    cmd = wxT("trigsimp(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_trigexpand:
  case menu_trigexpand:
    cmd = wxT("trigexpand(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_trigrat:
  case button_trigrat:
    cmd = wxT("trigrat(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case button_rectform:
  case menu_rectform:
    cmd = wxT("rectform(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_polarform:
    cmd = wxT("polarform(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_demoivre:
    cmd = wxT("demoivre(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_exponentialize:
    cmd = wxT("exponentialize(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_realpart:
    cmd = wxT("realpart(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_imagpart:
    cmd = wxT("imagpart(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_talg:
    cmd = wxT("algebraic : not(algebraic);");
    MenuCommand(cmd);
    break;
  case menu_tellrat:
    cmd = GetTextFromUser(_("Enter an equation for rational simplification:"),
                          _("Tellrat"), wxEmptyString, this);
    if (cmd.Length())
    {
      cmd = wxT("tellrat(") + cmd + wxT(");");
      MenuCommand(cmd);
    }
    break;
  case menu_modulus:
    cmd = GetTextFromUser(_("Calculate modulus:"),
                          _("Modulus"), wxT("false"), this);
    if (cmd.Length())
    {
      cmd = wxT("modulus : ") + cmd + wxT(";");
      MenuCommand(cmd);
    }
    break;
  default:
    break;
  }
}

void wxMaxima::CalculusMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_change_var:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Integral/Sum:"), _("Old variable:"),
                               _("New variable:"), _("Equation:"),
                               expr, wxT("x"), wxT("y"), wxT("y=x"),
                               this, -1, _("Change variable"), true);
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("changevar(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue4() + wxT(", ") + wiz->GetValue3() + wxT(", ") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_pade:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Taylor series:"), _("Num. deg:"),
                               _("Denom. deg:"), expr, wxT("4"), wxT("4"),
                               this, -1, _("Pade approximation"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("pade(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_continued_fraction:
    cmd += wxT("cfdisrep(cf(") + expr + wxT("));");
    MenuCommand(cmd);
    break;
  case menu_lcm:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                               wxEmptyString, wxEmptyString,
                               this, -1, _("LCM"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("lcm(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_gcd:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                               wxEmptyString, wxEmptyString,
                               this, -1, _("GCD"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("gcd(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_divide:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                               expr, wxEmptyString,
                               this, -1, _("Divide"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("divide(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_partfrac:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Expression:"), _("Variable:"),
                               expr, wxT("n"),
                               this, -1, _("Partial fractions"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("partfrac(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_risch:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Expression:"), _("Variable:"),
                               expr, wxT("x"),
                               this, -1, _("Integrate (risch)"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("risch(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case button_integrate:
  case menu_integrate:
  {
    IntegrateWiz *wiz = new IntegrateWiz(this, -1, _("Integrate"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_laplace:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("Old variable:"),
                               _("New variable:"), expr, wxT("t"), wxT("s"),
                               this, -1, _("Laplace"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("laplace(") + wiz->GetValue1() + wxT(", ")
        + wiz->GetValue2() +
        wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_ilt:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("Old variable:"),
                               _("New variable:"), expr, wxT("s"), wxT("t"),
                               this, -1, _("Inverse Laplace"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wxT("ilt(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_diff:
  case menu_diff:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("Variable(s):"),
                               _("Times:"), expr, wxT("x"), wxT("1"),
                               this, -1, _("Differentiate"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxStringTokenizer vars(wiz->GetValue2(), wxT(","));
      wxStringTokenizer times(wiz->GetValue3(), wxT(","));

      wxString val = wxT("diff(") + wiz->GetValue1();

      while (vars.HasMoreTokens() && times.HasMoreTokens()) {
        val += wxT(",") + vars.GetNextToken();
        val += wxT(",") + times.GetNextToken();
      }

      val += wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_taylor:
  case menu_series:
  {
    SeriesWiz *wiz = new SeriesWiz(this, -1, _("Series"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_limit:
  case menu_limit:
  {
    LimitWiz *wiz = new LimitWiz(this, -1, _("Limit"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_lbfgs:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Expression:"),
                               _("Variables:"),
                               _("Initial Estimates:"),
                               _("Epsilon:"),
                               expr, wxT("x"), wxT("1.0"), wxT("1e-4"),
                               this, -1, _("Find minimum"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("lbfgs(") + wiz->GetValue1() + wxT(", [") +
        wiz->GetValue2() + wxT("], [") +
        wiz->GetValue3() + wxT("], ") +
        wiz->GetValue4() + wxT(", [-1,0]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case button_sum:
  case menu_sum:
  {
    SumWiz *wiz = new SumWiz(this, -1, _("Sum"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_product:
  case menu_product:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Expression:"), _("Variable:"), _("From:"),
                               _("To:"), expr, wxT("k"), wxT("1"), wxT("n"),
                               this, -1, _("Product"));
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      cmd = wxT("product(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") +
        wiz->GetValue3() + wxT(", ") +
        wiz->GetValue4() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  default:
    break;
  }
}

void wxMaxima::PlotMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case button_plot3:
  case gp_plot3:
  {
    Plot3DWiz *wiz = new Plot3DWiz(this, -1, _("Plot 3D"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case button_plot2:
  case gp_plot2:
  {
    Plot2DWiz *wiz = new Plot2DWiz(this, -1, _("Plot 2D"));
    wiz->SetValue(expr);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case menu_plot_format:
  {
    PlotFormatWiz *wiz = new PlotFormatWiz(this, -1, _("Plot format"));
    wiz->Center(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      MenuCommand(wiz->GetValue());
    }
    wiz->Destroy();
    /*wxString format = GetTextFromUser(_("Enter new plot format:"),
      _("Plot format"),
      wxT("gnuplot"), this);
      if (format.Length())
      {
      MenuCommand(wxT("set_plot_option(['plot_format, '") + format +
      wxT("])$"));
      }*/
  }
  default:
    break;
  }
}

void wxMaxima::NumericalMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_to_float:
    cmd = wxT("float(") + expr + wxT("), numer;");
    MenuCommand(cmd);
    break;
  case menu_to_bfloat:
    cmd = wxT("bfloat(") + expr + wxT(");");
    MenuCommand(cmd);
    break;
  case menu_to_numer:
    cmd = expr + wxT(",numer;");
    MenuCommand(cmd);
    break;
  case menu_num_out:
    cmd = wxT("if numer#false then numer:false else numer:true;");
    MenuCommand(cmd);
    break;
  case menu_set_precision:
    cmd = GetTextFromUser(_("Enter new precision for bigfloats:"), _("Precision"),
                          wxT("16"), this);
    if (cmd.Length())
    {
      cmd = wxT("fpprec : ") + cmd + wxT(";");
      MenuCommand(cmd);
    }
    break;
  default:
    break;
  }
}

#ifndef __WXGTK__
MyAboutDialog::MyAboutDialog(wxWindow *parent, int id, const wxString title, wxString description) :
  wxDialog(parent, id, title)
{

  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

  wxHtmlWindow* html_top = new wxHtmlWindow(this, -1, wxDefaultPosition, wxSize(380, 250), wxHW_SCROLLBAR_NEVER);
  html_top->SetBorders(5);

  wxHtmlWindow* html_bottom = new wxHtmlWindow(this, -1, wxDefaultPosition, wxSize(380, 280));
  html_bottom->SetBorders(5);

  wxString cwd = wxGetCwd();
#if defined __WXMAC__
  cwd = cwd + wxT("/") + wxT(MACPREFIX);
#else
  cwd.Replace(wxT("\\"), wxT("/"));
  cwd = cwd + wxT("/data/");
#endif

  wxString page_top = wxString::Format(
    wxT("<html>"
        "<head>"
        "</head>"
        "<body>"
        "<center>"
        "<p>"
        "<img src=\"%s/wxmaxima.png\">"
        "</p>"
        "<h1>wxMaxima %s</h1>"
        "<p><small>(C) 2004 - 2015 Andrej Vodopivec</small><br></p>"
        "</center>"
        "</body>"
        "</html>"),
    cwd,
    wxT(VERSION));
  
  wxString page_bottom = wxString::Format(
    wxT("<html>"
        "<head>"
        "</head>"
        "<body>"
        "<center>"
        "<p>"
        "%s"
        "</p>"
        "<p><a href=\"http://andrejv.github.io/wxmaxima/\">wxMaxima</a><br>"
        "   <a href=\"http://maxima.sourceforge.net/\">Maxima</a></p>"
        "<h4>%s</h4>"
        "<p>"
        "wxWidgets: %d.%d.%d<br>"
        "%s: %s<br>"
        "%s"
        "</p>"
        "<h4>%s</h4>"
        "<p>"
        "Andrej Vodopivec<br>"
        "Ziga Lenarcic<br>"
        "Doug Ilijev<br>"
        "Gunter Königsmann<br>"
        "</p>"
        "<h4>Patches</h4>"
        "Sandro Montanar (SF-patch 2537150)"
        "</p>"
        "<h4>%s</h4>"
        "<p>"
        "%s: <a href=\"http://4pple.de/index.php/maxima-ein-opensource-computer-algebra-system-cas/\">Sven Hodapp</a><br>"
        "%s: <a href=\"http://tango.freedesktop.org/Tango_Desktop_Project\">TANGO project</a>"
        "</p>"
        "<h4>%s</h4>"
        "<p>"
        "Innocent De Marchi (ca)<br>"
        "Josef Barak (cs)<br>"
        "Robert Marik (cs)<br>"
        "Jens Thostrup (da)<br>"
        "Harald Geyer (de)<br>"
        "Dieter Kaiser (de)<br>"
        "Gunter Königsmann (de)<br>"
        "Alkis Akritas (el)<br>"
        "Evgenia Kelepesi-Akritas (el)<br>"
        "Kostantinos Derekas (el)<br>"
        "Mario Rodriguez Riotorto (es)<br>"
        "Antonio Ullan (es)<br>"
        "Eric Delevaux (fr)<br>"
        "Michele Gosse (fr)<br>"
        "Marco Ciampa (it)<br>"
#if wxUSE_UNICODE
        "Blahota István (hu)<br>"
#else
        "Blahota Istvan (hu)<br>"
#endif
#if wxUSE_UNICODE
        "Asbjørn Apeland (nb)<br>"
#else
        "Asbjorn Apeland (nb)<br>"
#endif
        "Rafal Topolnicki (pl)<br>"
        "Eduardo M. Kalinowski (pt_br)<br>"
        "Alexey Beshenov (ru)<br>"
        "Vadim V. Zhytnikov (ru)<br>"
        "Sergey Semerikov (uk)<br>"
#if wxUSE_UNICODE
        "Tufan Şirin (tr)<br>"
#else
        "Tufan Sirin (tr)<br>"
#endif
        "Frank Weng (zh_TW)<br>"
        "cw.ahbong (zh_TW)"
        "  </p>"
        "</center>"
        "</body>"
        "</html>"),
    _("wxMaxima is a graphical user interface for the computer algebra system MAXIMA based on wxWidgets."),
    _("System info"),
    wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER,
    _("Unicode Support"),
#if wxUSE_UNICODE
    wxT("yes"),
#else
    wxT("no"),
#endif
    description.c_str(),
    _("Written by"),
    _("Artwork by"),
    _("wxMaxima icon"),
    _("Toolbar icons"),
    _("Translated by"));

  html_top->SetPage(page_top);
  html_bottom->SetPage(page_bottom);

  html_top->SetSize(wxDefaultCoord,
                    html_top->GetInternalRepresentation()->GetHeight());

  sizer->Add(html_top, 0, wxALL, 0);
  sizer->Add(html_bottom, 0, wxALL, 0);

  SetSizer(sizer);
  sizer->Fit(this);
  sizer->SetSizeHints(this);

  SetAutoLayout(true);
  Layout();
}

void MyAboutDialog::OnLinkClicked(wxHtmlLinkEvent& event)
{
  wxLaunchDefaultBrowser(event.GetLinkInfo().GetHref());
}

BEGIN_EVENT_TABLE(MyAboutDialog, wxDialog)
EVT_HTML_LINK_CLICKED(wxID_ANY, MyAboutDialog::OnLinkClicked)
END_EVENT_TABLE()

#endif

void wxMaxima::HelpMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();
  wxString cmd;
  wxString helpSearchString = wxT("%");
  if (m_console->CanCopy(true))
    helpSearchString = m_console->GetString();
  else if (m_console->GetActiveCell() != NULL) {
    helpSearchString = m_console->GetActiveCell()->SelectWordUnderCaret(false);
  }
  if (helpSearchString == wxT(""))
    helpSearchString = wxT("%");

  switch (event.GetId())
  {

  case wxID_ABOUT:
#if defined __WXGTK__
  {
    wxAboutDialogInfo info;
    wxString description;

    description = _("wxMaxima is a graphical user interface for the computer algebra system Maxima based on wxWidgets.");

    description += wxString::Format(
      _("\n\nwxWidgets: %d.%d.%d\nUnicode support: %s"),
      wxMAJOR_VERSION, wxMINOR_VERSION, wxRELEASE_NUMBER,
#if wxUSE_UNICODE
      _("yes")
#else
      _("no")
#endif
      );
    if (m_maximaVersion != wxEmptyString)
      description += _("\nMaxima version: ") + m_maximaVersion;
    else
      description += _("\nNot connected.");
    if (m_lispVersion != wxEmptyString)
      description += _("\nLisp: ") + m_lispVersion;

    wxString iconName = wxT(PREFIX);
    iconName += wxT("/share/wxMaxima/wxmaxima.png");
    info.SetIcon(wxIcon(iconName,wxBITMAP_TYPE_PNG));
    info.SetDescription(description);
    info.SetName(_("wxMaxima"));
    info.SetVersion(wxT(VERSION));
    info.SetCopyright(wxT("(C) 2004-2015 Andrej Vodopivec"));
    info.SetWebSite(wxT("http://andrejv.github.io/wxmaxima/"));

    info.AddDeveloper(wxT("Andrej Vodopivec <andrej.vodopivec@gmail.com>"));
    info.AddDeveloper(wxT("Ziga Lenarcic <ziga.lenarcic@gmail.com>"));
    info.AddDeveloper(wxT("Doug Ilijev <doug.ilijev@gmail.com>"));
    info.AddDeveloper(wxT("Gunter Königsmann <wxMaxima@physikbuch.de>"));

    info.AddTranslator(wxT("Innocent de Marchi (ca)"));
    info.AddTranslator(wxT("Josef Barak (cs)"));
    info.AddTranslator(wxT("Robert Marik (cs)"));
    info.AddTranslator(wxT("Jens Thostrup (da)"));
    info.AddTranslator(wxT("Harald Geyer (de)"));
    info.AddTranslator(wxT("Dieter Kaiser (de)"));
    info.AddTranslator(wxT("Gunter Königsmann (de)"));
    info.AddTranslator(wxT("Alkis Akritas (el)"));
    info.AddTranslator(wxT("Evgenia Kelepesi-Akritas (el)"));
    info.AddTranslator(wxT("Kostantinos Derekas (el)"));
    info.AddTranslator(wxT("Mario Rodriguez Riotorto (es)"));
    info.AddTranslator(wxT("Antonio Ullan (es)"));
    info.AddTranslator(wxT("Eric Delevaux (fr)"));
    info.AddTranslator(wxT("Michele Gosse (fr)"));
#if wxUSE_UNICODE
    info.AddTranslator(wxT("Blahota István (hu)"));
#else
    info.AddTranslator(wxT("Blahota Istvan (hu)"));
#endif
    info.AddTranslator(wxT("Marco Ciampa (it)"));
#if wxUSE_UNICODE
    info.AddTranslator(wxT("Asbjørn Apeland (nb)"));
#else
    info.AddTranslator(wxT("Asbjorn Apeland (nb)"));
#endif
    info.AddTranslator(wxT("Rafal Topolnicki (pl)"));
    info.AddTranslator(wxT("Eduardo M. Kalinowski (pt_br)"));
    info.AddTranslator(wxT("Alexey Beshenov (ru)"));
    info.AddTranslator(wxT("Vadim V. Zhytnikov (ru)"));
#if wxUSE_UNICODE
    info.AddTranslator(wxT("Tufan Şirin (tr)"));
#else
    info.AddTranslator(wxT("Tufan Sirin (tr)"));
#endif
    info.AddTranslator(wxT("Sergey Semerikov (uk)"));
    info.AddTranslator(wxT("Frank Weng (zh_TW)"));
    info.AddTranslator(wxT("cw.ahbong (zh_TW)"));

    info.AddArtist(wxT("wxMaxima icon: Sven Hodapp"));
    info.AddArtist(wxT("Toolbar and config icons: The TANGO Project"));
    info.AddArtist(wxT("svg version of the icon: Gunter Königsmann"));

    wxAboutBox(info);
  }
#else
  {
    wxString description;

    if (m_maximaVersion != wxEmptyString)
      description += _("Maxima version: ") + m_maximaVersion;
    else
      description += _("Not connected.");
    if (m_lispVersion != wxEmptyString)
      description += _("<br>Lisp: ") + m_lispVersion;

    MyAboutDialog dlg(this, wxID_ANY, wxString(_("About")), description);
    dlg.Center();
    dlg.ShowModal();
  }
#endif

  break;

  case wxID_HELP:
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
  case ToolBar::tb_help:
#endif
    if(helpSearchString==wxT("%"))
      ShowWxMaximaHelp();
    else
      ShowMaximaHelp(helpSearchString);
    break;

  case menu_maximahelp:
    ShowMaximaHelp(expr);
    break;

  case menu_example:
    if (expr == wxT("%"))
      cmd = GetTextFromUser(_("Show an example for the command:"), _("Example"),
                            wxEmptyString, this);
    else
      cmd = expr;
    if (cmd.Length())
    {
      cmd = wxT("example(") + cmd + wxT(");");
      MenuCommand(cmd);
    }
    break;

  case menu_apropos:
    if (expr == wxT("%"))
      cmd = GetTextFromUser(_("Show all commands similar to:"), _("Apropos"),
                            wxEmptyString, this);
    else
      cmd = expr;
    if (cmd.Length())
    {
      cmd = wxT("apropos(\"") + cmd + wxT("\");");
      MenuCommand(cmd);
    }
    break;

  case menu_show_tip:
    ShowTip(true);
    break;

  case menu_build_info:
    MenuCommand(wxT("wxbuild_info()$"));
    break;

  case menu_bug_report:
    MenuCommand(wxT("wxbug_report()$"));
    break;

  case menu_help_tutorials:
    wxLaunchDefaultBrowser(wxT("http://andrejv.github.io/wxmaxima/help.html"));
    break;

  case menu_check_updates:
    CheckForUpdates(true);
    break;

  default:
    break;
  }
}

void wxMaxima::StatsMenu(wxCommandEvent &ev)
{
  if(ev.GetEventType() != (wxEVT_MENU))
    return;
  wxString expr = GetDefaultEntry();

  switch (ev.GetId())
  {
  case menu_stats_histogram:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Data:"), _("Classes:"),
                               expr, wxT("10"), this, -1, _("Histogram"), false);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("wxhistogram(") + wiz->GetValue1() + wxT(", nclasses=") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_stats_scatterplot:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Data:"), _("Classes:"),
                               expr, wxT("10"), this, -1, _("Scatterplot"), false);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("wxscatterplot(") + wiz->GetValue1() + wxT(", nclasses=") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_stats_barsplot:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("wxbarsplot(") + data + wxT(");"));
  }
  break;
  case menu_stats_boxplot:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("wxboxplot([") + data + wxT("]);"));
  }
  break;
  case menu_stats_piechart:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("wxpiechart(") + data + wxT(");"));
  }
  break;
  case menu_stats_mean:
  {

    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("mean(") + data + wxT(");"));
  }
  break;
  case menu_stats_median:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("median(") + data + wxT(");"));
  }
  break;
  case menu_stats_var:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("var(") + data + wxT(");"));
  }
  break;
  case menu_stats_dev:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("std(") + data + wxT(");"));
  }
  break;
  case menu_stats_tt1:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Sample:"), _("Mean:"),
                               expr, wxT("0"), this, -1, _("One sample t-test"), false);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("test_mean(") + wiz->GetValue1() + wxT(", mean=") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_stats_tt2:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Sample 1:"), _("Sample 2:"),
                               wxEmptyString, wxEmptyString, this, -1,
                               _("Two sample t-test"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("test_means_difference(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_stats_tnorm:
  {
    wxString data = GetTextFromUser(_("Data:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("test_normality(") + data + wxT(");"));
  }
  break;
  case menu_stats_linreg:
  {

    wxString data = GetTextFromUser(_("Data Matrix:"), _("Enter Data"), expr, this);
    if (data.Length() > 0)
      MenuCommand(wxT("simple_linear_regression(") + data + wxT(");"));
  }
  break;
  case menu_stats_lsquares:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Data Matrix:"), _("Col. names:"),
                               _("Equation:"), _("Variables:"),
                               expr, wxT("x,y"), wxT("y=A*x+B"), wxT("A,B"),
                               this, -1, _("Least Squares Fit"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("lsquares_estimates(") + wiz->GetValue1() + wxT(", [") +
        wiz->GetValue2() + wxT("], ") +
        wiz->GetValue3() + wxT(", [") +
        wiz->GetValue4() + wxT("], iprint=[-1,0]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case menu_stats_readm:
  {
    wxString file = wxFileSelector(_("Open matrix"), m_lastPath,
                                   wxEmptyString, wxEmptyString,
                                   _("Data file (*.csv, *.tab, *.txt)|*.csv;*.tab;*.txt"),
                                   wxFD_OPEN);
    if (file != wxEmptyString) {
      m_lastPath = wxPathOnly(file);

#if defined __WXMSW__
      file.Replace(wxT("\\"), wxT("/"));
#endif

      wxString name = wxGetTextFromUser(wxT("Enter matrix name:"), wxT("Marix name"));
      wxString cmd;

      if (name != wxEmptyString)
        cmd << name << wxT(": ");

      wxString format;
      if (file.EndsWith(wxT(".csv")))
        format = wxT("csv");
      else if (file.EndsWith(wxT(".tab")))
        format = wxT("tab");

      if (format != wxEmptyString)
        MenuCommand(cmd + wxT("read_matrix(\"") + file + wxT("\", '") + format + wxT(");"));
      else
        MenuCommand(cmd + wxT("read_matrix(\"") + file + wxT("\");"));
    }
  }
  break;
  case menu_stats_subsample:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Data Matrix:"), _("Condition:"),
                               _("Include columns:"), _("Matrix name:"),
                               expr, wxT("col[1]#'NA"),
                               wxEmptyString, wxEmptyString,
                               this, -1, _("Select Subsample"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString name = wiz->GetValue4();

      wxString cmd;

      if (name != wxEmptyString)
        cmd << name << wxT(": ");

      cmd += wxT("subsample(\n   ") + wiz->GetValue1() + wxT(",\n   ") +
        wxT("lambda([col], is( ");

      if (wiz->GetValue2() != wxEmptyString)
        cmd += wiz->GetValue2() + wxT(" ))");
      else
        cmd += wxT("true ))");

      if (wiz->GetValue3() != wxEmptyString)
        cmd += wxT(",\n   ") + wiz->GetValue3();

      cmd += wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  }
}

void wxMaxima::OnClose(wxCloseEvent& event)
{
  if (SaveNecessary())
  {
    int close = SaveDocumentP();

    if (close == wxID_CANCEL) {
      event.Veto();
      return;
    }

    if (close == wxID_YES) {
      if (!SaveFile()) {
        event.Veto();
        return;
      }
    }
  }

  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxSize size = GetSize();
  wxPoint pos = GetPosition();
  bool maximized = IsMaximized();
  config->Write(wxT("pos-x"), pos.x);
  config->Write(wxT("pos-y"), pos.y);
  config->Write(wxT("pos-w"), size.GetWidth());
  config->Write(wxT("pos-h"), size.GetHeight());
  if (maximized)
    config->Write(wxT("pos-max"), 1);
  else
    config->Write(wxT("pos-max"), 0);
  if (m_lastPath.Length() > 0)
    config->Write(wxT("lastPath"), m_lastPath);
  m_closing = true;
#if defined __WXMAC__
  wxGetApp().topLevelWindows.Erase(wxGetApp().topLevelWindows.Find(this));
#endif
  wxTheClipboard->Flush();
  CleanUp();
  Destroy();
}

void wxMaxima::PopupMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  wxString selection = m_console->GetString();
  switch (event.GetId())
  {
  case MathCtrl::popid_copy:
    if (m_console->CanCopy(true))
      m_console->Copy();
    break;
  case MathCtrl::popid_copy_tex:
    if (m_console->CanCopy(true))
      m_console->CopyTeX();
    break;
  case MathCtrl::popid_cut:
    if (m_console->CanCopy(true))
      m_console->CutToClipboard();
    break;
  case MathCtrl::popid_paste:
    m_console->PasteFromClipboard();
    break;
  case MathCtrl::popid_select_all:
    m_console->SelectAll();
    break;
  case MathCtrl::popid_comment_selection:
    m_console->CommentSelection();
    break;
  case MathCtrl::popid_divide_cell:
    m_console->DivideCell();
    break;
  case MathCtrl::popid_copy_image:
    if (m_console->CanCopy())
      m_console->CopyBitmap();
    break;
  case MathCtrl::popid_simplify:
    MenuCommand(wxT("ratsimp(") + selection + wxT(");"));
    break;
  case MathCtrl::popid_expand:
    MenuCommand(wxT("expand(") + selection + wxT(");"));
    break;
  case MathCtrl::popid_factor:
    MenuCommand(wxT("factor(") + selection + wxT(");"));
    break;
  case MathCtrl::popid_solve:
  {
    Gen2Wiz *wiz = new Gen2Wiz(_("Equation(s):"), _("Variable(s):"),
                               selection, wxT("x"), this, -1, _("Solve"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("solve([") + wiz->GetValue1() + wxT("], [") +
        wiz->GetValue2() + wxT("]);");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_solve_num:
  {
    Gen4Wiz *wiz = new Gen4Wiz(_("Equation:"), _("Variable:"),
                               _("Lower bound:"), _("Upper bound:"),
                               selection, wxT("x"), wxT("-1"), wxT("1"),
                               this, -1, _("Find root"), true);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString cmd = wxT("find_root(") + wiz->GetValue1() + wxT(", ") +
        wiz->GetValue2() + wxT(", ") +
        wiz->GetValue3() + wxT(", ") +
        wiz->GetValue4() + wxT(");");
      MenuCommand(cmd);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_integrate:
  {
    IntegrateWiz *wiz = new IntegrateWiz(this, -1, _("Integrate"));
    wiz->SetValue(selection);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_diff:
  {
    Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("Variable(s):"),
                               _("Times:"), selection, wxT("x"), wxT("1"),
                               this, -1, _("Differentiate"));
    wiz->SetValue(selection);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxStringTokenizer vars(wiz->GetValue2(), wxT(","));
      wxStringTokenizer times(wiz->GetValue3(), wxT(","));

      wxString val = wxT("diff(") + wiz->GetValue1();

      while (vars.HasMoreTokens() && times.HasMoreTokens()) {
        val += wxT(",") + vars.GetNextToken();
        val += wxT(",") + times.GetNextToken();
      }

      val += wxT(");");
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_subst:
  {
    SubstituteWiz *wiz = new SubstituteWiz(this, -1, _("Substitute"));
    wiz->SetValue(selection);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_plot2d:
  {
    Plot2DWiz *wiz = new Plot2DWiz(this, -1, _("Plot 2D"));
    wiz->SetValue(selection);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_plot3d:
  {
    Plot3DWiz *wiz = new Plot3DWiz(this, -1, _("Plot 3D"));
    wiz->SetValue(selection);
    wiz->Centre(wxBOTH);
    if (wiz->ShowModal() == wxID_OK)
    {
      wxString val = wiz->GetValue();
      MenuCommand(val);
    }
    wiz->Destroy();
  }
  break;
  case MathCtrl::popid_float:
    MenuCommand(wxT("float(") + selection + wxT("), numer;"));
    break;
  case MathCtrl::popid_image:
  {
    wxString file = wxFileSelector(_("Save selection to file"), m_lastPath,
                                   wxT("image.png"), wxT("png"),
                                   _("PNG image (*.png)|*.png|"
                                     "JPEG image (*.jpg)|*.jpg|"
                                     "Windows bitmap (*.bmp)|*.bmp|"
                                     "X pixmap (*.xpm)|*.xpm"),
                                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (file.Length())
    {
      m_console->CopyToFile(file);
      m_lastPath = wxPathOnly(file);
    }
  }
  break;
  case MathCtrl::popid_animation_save:
  {
    wxString file = wxFileSelector(_("Save animation to file"), m_lastPath,
                                   wxT("animation.gif"), wxT("gif"),
                                   _("GIF image (*.gif)|*.gif"),
                                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (file.Length())
    {
      MathCell *selection = m_console->GetSelectionStart();
      if (selection != NULL && selection->GetType() == MC_TYPE_SLIDE)
        ((SlideShow *)(selection))->ToGif(file);
    }
  }
  break;
  case MathCtrl::popid_evaluate:
  {
    bool evaluating = !m_console->m_evaluationQueue->Empty();
    m_console->AddSelectionToEvaluationQueue();
    if(!evaluating) TryEvaluateNextInQueue();
  }
  break;
  case MathCtrl::popid_merge_cells:
    m_console->MergeCells();
    break;
  }
}

void wxMaxima::OnRecentDocument(wxCommandEvent& event)
{
  if (SaveNecessary())
  {
    int close = SaveDocumentP();

    if (close == wxID_CANCEL)
      return;

    if (close == wxID_YES) {
      if (!SaveFile())
        return;
    }
  }

  wxString file = GetRecentDocument(event.GetId() - menu_recent_document_0);
  if (wxFileExists(file))
    OpenFile(file);
  else {
    wxMessageBox(_("File you tried to open does not exist."), _("File not found"), wxOK);
    RemoveRecentDocument(file);
  }
}

bool wxMaxima::SaveNecessary()
{
  return !m_fileSaved && (m_console->GetTree()!=NULL) &&
    // No data in the document
    (m_console->GetTree()!=NULL) &&
    // Only one math cell that consists only of a prompt
    !(dynamic_cast<GroupCell*>(m_console->GetTree())->Empty());
}

void wxMaxima::EditInputMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  if (!m_console->CanEdit())
    return ;

  EditorCell* tmp = dynamic_cast<EditorCell*>(m_console->GetSelectionStart());

  if (tmp == NULL)
    return ;

  m_console->SetActiveCell(tmp);
}
//! Handle the evaluation event
// 
// User tried to evaluate, find out what is the case
// Normally just add the respective groupcells to evaluationqueue
// If there is a special case - eg sending from output section
// of the working group, handle it carefully.
void wxMaxima::EvaluateEvent(wxCommandEvent& event)
{
  m_console->FollowEvaluation(true);
  bool evaluating = !m_console->m_evaluationQueue->Empty();
  if(m_console->QuestionPending())evaluating=false;
  MathCell* tmp = m_console->GetActiveCell();
  if (tmp != NULL) // we have an active cell
  {
    if (tmp->GetType() == MC_TYPE_INPUT && !m_inLispMode)
      tmp->AddEnding();
    // if active cell is part of a working group, we have a special
    // case - answering a question. Manually send answer to Maxima.
    if (m_console->GCContainsCurrentQuestion(dynamic_cast<GroupCell*>(tmp->GetParent()))) {
      SendMaxima(tmp->ToString(), true);
    }
    else { // normally just add to queue
      m_console->AddCellToEvaluationQueue(dynamic_cast<GroupCell*>(tmp->GetParent()));
    }
  }
  else { // no evaluate has been called on no active cell?
    m_console->AddSelectionToEvaluationQueue();
  }
  if(!evaluating) TryEvaluateNextInQueue();;
}

wxString wxMaxima::GetUnmatchedParenthesisState(wxString text)
{
  int len=text.Length();
  int index=0;

  std::list<wxChar> delimiters;
  
  while(index<len)
  {
    wxChar c=text[index];
    
    switch(c)
    {
    case wxT('('):
      delimiters.push_back(wxT(')'));
      break;
    case wxT('['):
      delimiters.push_back(wxT(']'));
      break;
    case wxT('{'):
      delimiters.push_back(wxT('}'));
      break;

    case wxT(')'):
    case wxT(']'):
    case wxT('}'):
      if(c!=delimiters.back()) return(_("Mismatched parenthesis"));
      delimiters.pop_back();
      break;

    case wxT('\\'):
      index++;
      break;

    case wxT('"'):
      //
      index++;
      while((index<len)&&(c=text[index])!=wxT('"'))
      {
        if(c==wxT('\\'))
          index++;
        index++;
      }
      if(c!='"') return(_("Unterminated string."));
      break;
    
    case '/':
      if(index<len-1)
      {
        if(text[index + 1]==wxT('*'))
        {
          index=text.find(wxT("*/"),index);
          if(index==wxNOT_FOUND)
            return(_("Unterminated comment."));
        }
      }
    }

    index++;
  }
  if(!delimiters.empty())
  {
    return _("Un-closed parenthesis");
  }
  return wxEmptyString;
}

void wxMaxima::TriggerEvaluation()
{
  if(m_console->m_evaluationQueue->Empty())
    TryEvaluateNextInQueue();
}

//! Tries to evaluate next group cell in queue
//
// Calling this function should not do anything dangerous
void wxMaxima::TryEvaluateNextInQueue()
{

  if (!m_isConnected) {
    wxMessageBox(_("\nNot connected to Maxima!\n"), _("Error"), wxOK | wxICON_ERROR);

    if (!m_console->m_evaluationQueue->Empty())
    {
      if (m_console->m_evaluationQueue->GetFirst()->GetInput()->ToString() ==
          wxT("wxmaxima_debug_dump_output;"))
        DumpProcessOutput();
    }

    // Clear the evaluation queue.
    while (!m_console->m_evaluationQueue->Empty())
      m_console->m_evaluationQueue->RemoveFirst();

    m_console->Refresh();

    return ;
  }

  
  // Maxima is connected. Let's test if the evaluation queue is empty.
  GroupCell *tmp = m_console->m_evaluationQueue->GetFirst();
  if (tmp == NULL)
  {
      StatusMaximaBusy(waiting);
    return; //empty queue
  }

  // We don't want to evaluate a new cell if the user still has to answer
  // a question.
  if(m_console->QuestionPending())
    return;

  // Maxima is connected and the queue contains an item.
  if (tmp->GetEditable()->GetValue() != wxEmptyString)
  {
    tmp->GetEditable()->AddEnding();
    tmp->GetEditable()->ContainsChanges(false);
    wxString text = tmp->GetEditable()->ToString();

    // override evaluation when input equals wxmaxima_debug_dump_output
    if (text.IsSameAs(wxT("wxmaxima_debug_dump_output;"))) {
      DumpProcessOutput();
      return;
    }
        
    tmp->RemoveOutput();
    wxString parenthesisError=GetUnmatchedParenthesisState(tmp->GetEditable()->ToString());
    if(parenthesisError==wxEmptyString)
    {          
      if(m_console->FollowEvaluation())
      {
        m_console->SetSelection(tmp);
        if(!m_console->GetWorkingGroup())
        {
          m_console->SetHCaret(tmp);
          m_console->ScrollToCaret();
        }
      }
      else
        m_console->Recalculate();

      
      m_console->SetWorkingGroup(tmp);
      tmp->GetPrompt()->SetValue(m_lastPrompt);
      SendMaxima(text, true);
    }
    else
    {
      TextCell* cell = new TextCell(_(wxT("Refusing to send cell to maxima: ")) +
                                    parenthesisError + wxT("\n"));
      cell->SetType(MC_TYPE_ERROR);
      cell->SetParent(tmp);
      tmp->SetOutput(cell);
      m_console->RecalculateForce();

      if(m_console->FollowEvaluation())
        m_console->SetSelection(NULL);
        
      m_console->SetWorkingGroup(NULL);
      m_console->Recalculate();
      m_console->Refresh();
//      TryEvaluateNextInQueue();
    }
  }
  else
  {
    TryEvaluateNextInQueue();
  }
}

void wxMaxima::InsertMenu(wxCommandEvent& event)
{
  if(event.GetEventType() != (wxEVT_MENU))
    return;
  int type = 0;
  bool output = false;
  switch (event.GetId())
  {
  case menu_insert_previous_output:
    output = true;
  case MathCtrl::popid_insert_input:
  case menu_insert_input:
  case menu_insert_previous_input:
    type = GC_TYPE_CODE;
    break;
  case menu_autocomplete:
    m_console->Autocomplete();
    return ;
    break;
  case menu_autocomplete_templates:
    m_console->Autocomplete(AutoComplete::tmplte);
    return ;
    break;
  case menu_add_comment:
  case MathCtrl::popid_add_comment:
  case menu_format_text:
  case MathCtrl::popid_insert_text:
    type = GC_TYPE_TEXT;
    break;
  case menu_add_title:
  case menu_format_title:
  case MathCtrl::popid_insert_title:
    type = GC_TYPE_TITLE;
    break;
  case menu_add_section:
  case menu_format_section:
  case MathCtrl::popid_insert_section:
    type = GC_TYPE_SECTION;
    break;
  case menu_add_subsection:
  case menu_format_subsection:
  case MathCtrl::popid_insert_subsection:
    type = GC_TYPE_SUBSECTION;
    break;
  case menu_add_subsubsection:
  case menu_format_subsubsection:
  case MathCtrl::popid_insert_subsubsection:
    type = GC_TYPE_SUBSUBSECTION;
    break;
  case menu_add_pagebreak:
  case menu_format_pagebreak:
    m_console->InsertGroupCells(new GroupCell(GC_TYPE_PAGEBREAK),
                                m_console->GetHCaret());
    m_console->Refresh();
    m_console->SetFocus();
    return;
    break;
  case menu_insert_image:
  case menu_format_image:
  {
    wxString file = wxFileSelector(_("Insert Image"), m_lastPath,
                                   wxEmptyString, wxEmptyString,
                                   _("Image files (*.png, *.jpg, *.bmp, *.xpm)|*.png;*.jpg;*.bmp;*.xpm"),
                                   wxFD_OPEN);
    if (file != wxEmptyString) {
      m_console->OpenHCaret(file, GC_TYPE_IMAGE);
    }
    m_console->SetFocus();
    return ;
  }
  break;
  case menu_fold_all_cells:
    m_console->FoldAll();
    m_console->Recalculate(true);
    // send cursor to the top
    m_console->SetHCaret(NULL);
    break;
  case menu_unfold_all_cells:
    m_console->UnfoldAll();
    m_console->Recalculate(true);
    // refresh without moving cursor
    m_console->SetHCaret(m_console->GetHCaret());
    break;
  }

  m_console->SetFocus();

  if (event.GetId() == menu_insert_previous_input ||
      event.GetId() == menu_insert_previous_output)
  {
    wxString input;

    if (output == true) 
      input = m_console->GetOutputAboveCaret(); 
    else
      input = m_console->GetInputAboveCaret();
    if (input != wxEmptyString)
      m_console->OpenHCaret(input, type);
  }
  else if (event.GetId() == menu_unfold_all_cells ||
           event.GetId() == menu_fold_all_cells)
  {
    // don't do anything else
  }
  else
    m_console->OpenHCaret(wxEmptyString, type);
}

void wxMaxima::ResetTitle(bool saved)
{
  if (saved != m_fileSaved)
  {
    m_fileSaved = saved;
    if (m_currentFile.Length() == 0) {
#ifndef __WXMAC__
      if (saved)
        SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) + _("[ unsaved ]"));
      else
        SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) + _("[ unsaved* ]"));
#endif
    }
    else
    {
      wxString name, ext;
      wxFileName::SplitPath(m_currentFile, NULL, NULL, &name, &ext);
#ifndef __WXMAC__
      if (m_fileSaved)
        SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) +
                 wxT(" [ ") + name + wxT(".") + ext + wxT(" ]"));
      else
        SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) +
                 wxT(" [ ") + name + wxT(".") + ext + wxT("* ]"));
#else
      SetTitle(name + wxT(".") + ext);
#endif
    }
#if defined __WXMAC__
#if defined __WXOSX_COCOA__
    OSXSetModified(!saved);
    if (m_currentFile != wxEmptyString)
      SetRepresentedFilename(m_currentFile);
#else
    WindowRef win = (WindowRef)MacGetTopLevelWindowRef();
    SetWindowModified(win,!saved);
    if (m_currentFile != wxEmptyString)
    {
      FSRef fsref;
      wxMacPathToFSRef(m_currentFile, &fsref);
      HIWindowSetProxyFSRef(win, &fsref);
    }
#endif
#endif
  }
}

///--------------------------------------------------------------------------------
///  Plot Slider
///--------------------------------------------------------------------------------

void wxMaxima::UpdateSlider(wxUpdateUIEvent &ev)
{
  if(m_console->m_mainToolBar)
  {
    if (m_console->m_mainToolBar->m_plotSlider)
    {
      if (m_console->IsSelected(MC_TYPE_SLIDE))
      {    
        SlideShow *cell = (SlideShow *)m_console->GetSelectionStart();
        
        m_console->m_mainToolBar->m_plotSlider->SetRange(0, cell->Length() - 1);
        m_console->m_mainToolBar->m_plotSlider->SetValue(cell->GetDisplayedIndex());
      }
    }
  }
}

void wxMaxima::SliderEvent(wxScrollEvent &ev)
{
  if (m_console->AnimationRunning())
    m_console->Animate(false);

  SlideShow *cell = (SlideShow *)m_console->GetSelectionStart();
  if (cell != NULL)
  {
    cell->SetDisplayedIndex(ev.GetPosition());

    wxRect rect = cell->GetRect();
    m_console->CalcScrolledPosition(rect.x, rect.y, &rect.x, &rect.y);
    m_console->RefreshRect(rect);
  }
}

void wxMaxima::ShowPane(wxCommandEvent &ev)
{
  int id = ev.GetId();

  wxMaximaFrame::ShowPane(static_cast<Event>(id),
			  !IsPaneDisplayed(static_cast<Event>(id)));
}

void wxMaxima::HistoryDClick(wxCommandEvent& ev)
{
  m_console->OpenHCaret(ev.GetString(), GC_TYPE_CODE);
  m_console->SetFocus();
}

void wxMaxima::StructureDClick(wxCommandEvent& ev)
{
  m_console->ScrollToCell(((GroupCell *)m_console->m_structure->GetCell(ev.GetSelection())->GetParent()));
}

//! Called when the "Scroll to currently evaluated" button is pressed.
void wxMaxima::OnFollow(wxCommandEvent& event)
{
  m_console->OnFollow();
}


long *VersionToInt(wxString version)
{
  long *intV = new long[3];

  wxStringTokenizer tokens(version, wxT("."));

  for (int i=0; i<3 && tokens.HasMoreTokens(); i++)
    tokens.GetNextToken().ToLong(&intV[i]);

  return intV;
}

/***
 * Checks the file http://andrejv.github.io/wxmaxima/version.txt to
 * see if there is a newer version available.
 */
void wxMaxima::CheckForUpdates(bool reportUpToDate)
{
  wxHTTP connection;
  connection.SetHeader(wxT("Content-type"), wxT("text/html; charset=utf-8"));
  connection.SetTimeout(2);

  if (!connection.Connect(wxT("andrejv.github.io")))
  {
    wxMessageBox(_("Can not connect to the web server."), _("Error"),
                 wxOK | wxICON_ERROR);
    return;
  }

  wxInputStream *inputStream = connection.GetInputStream(_T("/wxmaxima/version.txt"));

  if (connection.GetError() == wxPROTO_NOERR)
  {
    wxString version;
    wxStringOutputStream outputStream(&version);
    inputStream->Read(outputStream);

    if (version.StartsWith(wxT("wxmaxima = "))) {
      version = version.Mid(11, version.Length()).Trim();
      long *myVersion = VersionToInt(wxT(VERSION));
      long *currVersion = VersionToInt(version);

      bool upgrade = myVersion[0] < currVersion[0] ||
                                    (myVersion[0] == currVersion[0] && myVersion[1]<currVersion[1]) ||
        (myVersion[0] == currVersion[0] &&
         myVersion[1] == currVersion[1] &&
         myVersion[2] < currVersion[2]);

      if (upgrade) {
        bool visit = wxMessageBox(wxString::Format(
                                    _("You have version %s. Current version is %s.\n\n"
                                      "Select OK to visit the wxMaxima webpage."),
                                    wxT(VERSION), version.c_str()),
                                  _("Upgrade"),
                                  wxOK | wxCANCEL | wxICON_INFORMATION) == wxOK;

        if (visit)
          wxLaunchDefaultBrowser(wxT("http://andrejv.github.io/wxmaxima/"));
      }
      else if (reportUpToDate)
        wxMessageBox(_("Your version of wxMaxima is up to date."), _("Upgrade"),
                     wxOK | wxICON_INFORMATION);

      delete [] myVersion;
      delete [] currVersion;
    }
  }
  else
  {
    wxMessageBox(_("Can not download version info."), _("Error"),
                 wxOK | wxICON_ERROR);
  }

  wxDELETE(inputStream);
  connection.Close();
}

int wxMaxima::SaveDocumentP()
{
  wxString file, ext;
  if (m_currentFile == wxEmptyString)
  {
    // Check if we want to save modified untitled documents on exit
    bool save = true;
    wxConfig::Get()->Read(wxT("saveUntitled"), &save);
    if (!save)
      return wxID_NO;

#if defined __WXMAC__
    file = GetTitle();
#else
    file = _("unsaved");
#endif
  }
  else {
    if(m_autoSaveInterval > 10000)
      if(SaveFile())
	return wxID_YES;

    wxString ext;
    wxFileName::SplitPath(m_currentFile, NULL, NULL, &file, &ext);
    file += wxT(".") + ext;
  }

  wxMessageDialog dialog(this,
                         _("Do you want to save the changes you made in the document \"") +
                         file + wxT("\"?"),
			 wxEmptyString, wxCENTER | wxYES_NO | wxCANCEL);

  dialog.SetExtendedMessage(_("Your changes will be lost if you don't save them."));
  dialog.SetYesNoCancelLabels(_("Save"), _("Don't save"), _("Cancel"));

  return dialog.ShowModal();
}

BEGIN_EVENT_TABLE(wxMaxima, wxFrame)

#if defined __WXMAC__
EVT_MENU(mac_closeId, wxMaxima::FileMenu)
#endif
EVT_MENU(menu_check_updates, wxMaxima::HelpMenu)
EVT_TIMER(KEYBOARD_INACTIVITY_TIMER_ID, wxMaxima::OnTimerEvent)
EVT_TIMER(wxID_ANY, wxMaxima::OnTimerEvent)
EVT_COMMAND_SCROLL(ToolBar::plot_slider_id, wxMaxima::SliderEvent)
EVT_MENU(MathCtrl::popid_copy, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_copy_image, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_insert_text, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_insert_title, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_insert_section, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_insert_subsection, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_insert_subsubsection, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_delete, wxMaxima::EditMenu)
EVT_MENU(MathCtrl::popid_simplify, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_factor, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_expand, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_solve, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_solve_num, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_subst, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_plot2d, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_plot3d, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_diff, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_integrate, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_float, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_copy_tex, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_image, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_animation_save, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_animation_start, wxMaxima::FileMenu)
EVT_BUTTON(button_integrate, wxMaxima::CalculusMenu)
EVT_BUTTON(button_diff, wxMaxima::CalculusMenu)
EVT_BUTTON(button_solve, wxMaxima::EquationsMenu)
EVT_BUTTON(button_solve_ode, wxMaxima::EquationsMenu)
EVT_BUTTON(button_sum, wxMaxima::CalculusMenu)
EVT_BUTTON(button_expand, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_factor, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_taylor, wxMaxima::CalculusMenu)
EVT_BUTTON(button_limit, wxMaxima::CalculusMenu)
EVT_BUTTON(button_ratsimp, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_trigexpand, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_trigreduce, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_trigsimp, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_product, wxMaxima::CalculusMenu)
EVT_BUTTON(button_radcan, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_subst, wxMaxima::MaximaMenu)
EVT_BUTTON(button_plot2, wxMaxima::PlotMenu)
EVT_BUTTON(button_plot3, wxMaxima::PlotMenu)
EVT_BUTTON(button_map, wxMaxima::AlgebraMenu)
EVT_BUTTON(button_rectform, wxMaxima::SimplifyMenu)
EVT_BUTTON(button_trigrat, wxMaxima::SimplifyMenu)
EVT_MENU(menu_polarform, wxMaxima::SimplifyMenu)
EVT_MENU(ToolBar::menu_restart_id, wxMaxima::MaximaMenu)
#ifndef __WXMAC__
EVT_MENU(wxID_EXIT, wxMaxima::FileMenu)
#endif
EVT_MENU(wxID_ABOUT, wxMaxima::HelpMenu)
EVT_MENU(menu_save_id, wxMaxima::FileMenu)
EVT_MENU(menu_save_as_id, wxMaxima::FileMenu)
EVT_MENU(menu_load_id, wxMaxima::FileMenu)
EVT_MENU(menu_functions, wxMaxima::MaximaMenu)
EVT_MENU(menu_variables, wxMaxima::MaximaMenu)
EVT_MENU(wxID_PREFERENCES, wxMaxima::EditMenu)
EVT_MENU(menu_sconsole_id, wxMaxima::FileMenu)
EVT_MENU(menu_export_html, wxMaxima::FileMenu)
EVT_MENU(wxID_HELP, wxMaxima::HelpMenu)
EVT_MENU(menu_help_tutorials, wxMaxima::HelpMenu)
EVT_MENU(menu_bug_report, wxMaxima::HelpMenu)
EVT_MENU(menu_build_info, wxMaxima::HelpMenu)
EVT_MENU(menu_interrupt_id, wxMaxima::Interrupt)
EVT_MENU(menu_new_id, wxMaxima::FileMenu)
EVT_MENU(menu_open_id, wxMaxima::FileMenu)
EVT_MENU(menu_batch_id, wxMaxima::FileMenu)
EVT_MENU(menu_ratsimp, wxMaxima::SimplifyMenu)
EVT_MENU(menu_radsimp, wxMaxima::SimplifyMenu)
EVT_MENU(menu_expand, wxMaxima::SimplifyMenu)
EVT_MENU(menu_factor, wxMaxima::SimplifyMenu)
EVT_MENU(menu_gfactor, wxMaxima::SimplifyMenu)
EVT_MENU(menu_trigsimp, wxMaxima::SimplifyMenu)
EVT_MENU(menu_trigexpand, wxMaxima::SimplifyMenu)
EVT_MENU(menu_trigreduce, wxMaxima::SimplifyMenu)
EVT_MENU(menu_rectform, wxMaxima::SimplifyMenu)
EVT_MENU(menu_demoivre, wxMaxima::SimplifyMenu)
EVT_MENU(menu_num_out, wxMaxima::NumericalMenu)
EVT_MENU(menu_to_float, wxMaxima::NumericalMenu)
EVT_MENU(menu_to_bfloat, wxMaxima::NumericalMenu)
EVT_MENU(menu_to_numer, wxMaxima::NumericalMenu)
EVT_MENU(menu_exponentialize, wxMaxima::SimplifyMenu)
EVT_MENU(menu_invert_mat, wxMaxima::AlgebraMenu)
EVT_MENU(menu_determinant, wxMaxima::AlgebraMenu)
EVT_MENU(menu_eigen, wxMaxima::AlgebraMenu)
EVT_MENU(menu_eigvect, wxMaxima::AlgebraMenu)
EVT_MENU(menu_adjoint_mat, wxMaxima::AlgebraMenu)
EVT_MENU(menu_transpose, wxMaxima::AlgebraMenu)
EVT_MENU(menu_set_precision, wxMaxima::NumericalMenu)
EVT_MENU(menu_talg, wxMaxima::SimplifyMenu)
EVT_MENU(menu_tellrat, wxMaxima::SimplifyMenu)
EVT_MENU(menu_modulus, wxMaxima::SimplifyMenu)
EVT_MENU(menu_allroots, wxMaxima::EquationsMenu)
EVT_MENU(menu_bfallroots, wxMaxima::EquationsMenu)
EVT_MENU(menu_realroots, wxMaxima::EquationsMenu)
EVT_MENU(menu_solve, wxMaxima::EquationsMenu)
EVT_MENU(menu_solve_to_poly, wxMaxima::EquationsMenu)
EVT_MENU(menu_solve_num, wxMaxima::EquationsMenu)
EVT_MENU(menu_solve_ode, wxMaxima::EquationsMenu)
EVT_MENU(menu_map_mat, wxMaxima::AlgebraMenu)
EVT_MENU(menu_enter_mat, wxMaxima::AlgebraMenu)
EVT_MENU(menu_cpoly, wxMaxima::AlgebraMenu)
EVT_MENU(menu_solve_lin, wxMaxima::EquationsMenu)
EVT_MENU(menu_solve_algsys, wxMaxima::EquationsMenu)
EVT_MENU(menu_eliminate, wxMaxima::EquationsMenu)
EVT_MENU(menu_clear_var, wxMaxima::MaximaMenu)
EVT_MENU(menu_clear_fun, wxMaxima::MaximaMenu)
EVT_MENU(menu_ivp_1, wxMaxima::EquationsMenu)
EVT_MENU(menu_ivp_2, wxMaxima::EquationsMenu)
EVT_MENU(menu_bvp, wxMaxima::EquationsMenu)
EVT_MENU(menu_bvp, wxMaxima::EquationsMenu)
EVT_MENU(menu_fun_def, wxMaxima::MaximaMenu)
EVT_MENU(menu_divide, wxMaxima::CalculusMenu)
EVT_MENU(menu_gcd, wxMaxima::CalculusMenu)
EVT_MENU(menu_lcm, wxMaxima::CalculusMenu)
EVT_MENU(menu_continued_fraction, wxMaxima::CalculusMenu)
EVT_MENU(menu_partfrac, wxMaxima::CalculusMenu)
EVT_MENU(menu_risch, wxMaxima::CalculusMenu)
EVT_MENU(menu_integrate, wxMaxima::CalculusMenu)
EVT_MENU(menu_laplace, wxMaxima::CalculusMenu)
EVT_MENU(menu_ilt, wxMaxima::CalculusMenu)
EVT_MENU(menu_diff, wxMaxima::CalculusMenu)
EVT_MENU(menu_series, wxMaxima::CalculusMenu)
EVT_MENU(menu_limit, wxMaxima::CalculusMenu)
EVT_MENU(menu_lbfgs, wxMaxima::CalculusMenu)
EVT_MENU(menu_gen_mat, wxMaxima::AlgebraMenu)
EVT_MENU(menu_gen_mat_lambda, wxMaxima::AlgebraMenu)
EVT_MENU(menu_map, wxMaxima::AlgebraMenu)
EVT_MENU(menu_sum, wxMaxima::CalculusMenu)
EVT_MENU(menu_maximahelp, wxMaxima::HelpMenu)
EVT_MENU(menu_example, wxMaxima::HelpMenu)
EVT_MENU(menu_apropos, wxMaxima::HelpMenu)
EVT_MENU(menu_show_tip, wxMaxima::HelpMenu)
EVT_MENU(menu_trigrat, wxMaxima::SimplifyMenu)
EVT_MENU(menu_solve_de, wxMaxima::EquationsMenu)
EVT_MENU(menu_atvalue, wxMaxima::EquationsMenu)
EVT_MENU(menu_sum, wxMaxima::CalculusMenu)
EVT_MENU(menu_product, wxMaxima::CalculusMenu)
EVT_MENU(menu_change_var, wxMaxima::CalculusMenu)
EVT_MENU(menu_make_list, wxMaxima::AlgebraMenu)
EVT_MENU(menu_apply, wxMaxima::AlgebraMenu)
EVT_MENU(menu_time, wxMaxima::MaximaMenu)
EVT_MENU(menu_factsimp, wxMaxima::SimplifyMenu)
EVT_MENU(menu_factcomb, wxMaxima::SimplifyMenu)
EVT_MENU(menu_realpart, wxMaxima::SimplifyMenu)
EVT_MENU(menu_imagpart, wxMaxima::SimplifyMenu)
EVT_MENU(menu_nouns, wxMaxima::SimplifyMenu)
EVT_MENU(menu_logcontract, wxMaxima::SimplifyMenu)
EVT_MENU(menu_logexpand, wxMaxima::SimplifyMenu)
EVT_MENU(gp_plot2, wxMaxima::PlotMenu)
EVT_MENU(gp_plot3, wxMaxima::PlotMenu)
EVT_MENU(menu_plot_format, wxMaxima::PlotMenu)
EVT_MENU(menu_soft_restart, wxMaxima::MaximaMenu)
EVT_MENU(menu_display, wxMaxima::MaximaMenu)
EVT_MENU(menu_pade, wxMaxima::CalculusMenu)
EVT_MENU(menu_add_path, wxMaxima::MaximaMenu)
EVT_MENU(menu_copy_from_console, wxMaxima::EditMenu)
EVT_MENU(menu_copy_text_from_console, wxMaxima::EditMenu)
EVT_MENU(menu_copy_tex_from_console, wxMaxima::EditMenu)
EVT_MENU(menu_undo, wxMaxima::EditMenu)
EVT_MENU(menu_redo, wxMaxima::EditMenu)
EVT_MENU(menu_texform, wxMaxima::MaximaMenu)
EVT_MENU(menu_to_fact, wxMaxima::SimplifyMenu)
EVT_MENU(menu_to_gamma, wxMaxima::SimplifyMenu)
EVT_MENU(wxID_PRINT, wxMaxima::PrintMenu)
#if defined (__WXMSW__) || (__WXGTK20__) || defined (__WXMAC__)
EVT_TOOL(ToolBar::tb_print, wxMaxima::PrintMenu)
#endif
EVT_MENU(menu_zoom_in,  wxMaxima::EditMenu)
EVT_MENU(menu_zoom_out, wxMaxima::EditMenu)
EVT_MENU(menu_zoom_80,  wxMaxima::EditMenu)
EVT_MENU(menu_zoom_100, wxMaxima::EditMenu)
EVT_MENU(menu_zoom_120, wxMaxima::EditMenu)
EVT_MENU(menu_zoom_150, wxMaxima::EditMenu)
EVT_MENU(menu_zoom_200, wxMaxima::EditMenu)
EVT_MENU(menu_zoom_300, wxMaxima::EditMenu)
EVT_MENU(menu_fullscreen, wxMaxima::EditMenu)
EVT_MENU(menu_copy_as_bitmap, wxMaxima::EditMenu)
EVT_MENU(menu_copy_to_file, wxMaxima::EditMenu)
  EVT_MENU(menu_select_all, wxMaxima::EditMenu)
EVT_MENU(menu_subst, wxMaxima::MaximaMenu)
#if defined (__WXMSW__) || defined (__WXGTK20__)
EVT_TOOL(ToolBar::tb_new, wxMaxima::FileMenu)
#endif
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
EVT_TOOL(ToolBar::tb_open, wxMaxima::FileMenu)
EVT_TOOL(ToolBar::tb_save, wxMaxima::FileMenu)
EVT_TOOL(ToolBar::tb_copy, wxMaxima::EditMenu)
EVT_TOOL(ToolBar::tb_paste, wxMaxima::EditMenu)
EVT_TOOL(ToolBar::tb_select_all, wxMaxima::EditMenu)
EVT_TOOL(ToolBar::tb_cut, wxMaxima::EditMenu)
EVT_TOOL(ToolBar::tb_pref, wxMaxima::EditMenu)
EVT_TOOL(ToolBar::tb_interrupt, wxMaxima::Interrupt)
EVT_TOOL(ToolBar::tb_help, wxMaxima::HelpMenu)
EVT_TOOL(ToolBar::tb_animation_startStop, wxMaxima::FileMenu)
EVT_TOOL(ToolBar::tb_animation_start, wxMaxima::FileMenu)
EVT_TOOL(ToolBar::tb_animation_stop, wxMaxima::FileMenu)
EVT_TOOL(ToolBar::tb_find, wxMaxima::EditMenu)
#endif
EVT_TOOL(ToolBar::tb_follow,wxMaxima::OnFollow)
EVT_SOCKET(socket_server_id, wxMaxima::ServerEvent)
EVT_SOCKET(socket_client_id, wxMaxima::ClientEvent)
EVT_UPDATE_UI(menu_interrupt_id, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(ToolBar::plot_slider_id, wxMaxima::UpdateSlider)
EVT_UPDATE_UI(menu_copy_from_console, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_copy_text_from_console, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_copy_tex_from_console, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_zoom_in, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_zoom_out, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(wxID_PRINT, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_copy_as_bitmap, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_copy_to_file, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_evaluate, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_evaluate_all, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(ToolBar::tb_evaltillhere, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_select_all, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_undo, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_hideall, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_math, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_stats, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_history, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_structure, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_pane_format, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_remove_output, wxMaxima::UpdateMenus)
#if defined (__WXMSW__) || defined (__WXGTK20__) || defined (__WXMAC__)
EVT_UPDATE_UI(ToolBar::tb_print, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_follow, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_copy, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_cut, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_interrupt, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_save, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_animation_startStop, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_animation_start, wxMaxima::UpdateToolBar)
EVT_UPDATE_UI(ToolBar::tb_animation_stop, wxMaxima::UpdateToolBar)
#endif
EVT_UPDATE_UI(menu_save_id, wxMaxima::UpdateMenus)
EVT_UPDATE_UI(menu_show_toolbar, wxMaxima::UpdateMenus)
EVT_CLOSE(wxMaxima::OnClose)
EVT_END_PROCESS(maxima_process_id, wxMaxima::OnProcessEvent)
EVT_MENU(MathCtrl::popid_edit, wxMaxima::EditInputMenu)
EVT_MENU(menu_evaluate, wxMaxima::EvaluateEvent)
EVT_MENU(menu_add_comment, wxMaxima::InsertMenu)
EVT_MENU(menu_add_section, wxMaxima::InsertMenu)
EVT_MENU(menu_add_subsection, wxMaxima::InsertMenu)
EVT_MENU(menu_add_subsubsection, wxMaxima::InsertMenu)
EVT_MENU(menu_add_title, wxMaxima::InsertMenu)
EVT_MENU(menu_add_pagebreak, wxMaxima::InsertMenu)
EVT_MENU(menu_fold_all_cells, wxMaxima::InsertMenu)
EVT_MENU(menu_unfold_all_cells, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_add_comment, wxMaxima::InsertMenu)
EVT_MENU(menu_insert_previous_input, wxMaxima::InsertMenu)
EVT_MENU(menu_insert_previous_output, wxMaxima::InsertMenu)
EVT_MENU(menu_autocomplete, wxMaxima::InsertMenu)
EVT_MENU(menu_autocomplete_templates, wxMaxima::InsertMenu)
EVT_MENU(menu_insert_input, wxMaxima::InsertMenu)
EVT_MENU(MathCtrl::popid_insert_input, wxMaxima::InsertMenu)
EVT_MENU(menu_history_previous, wxMaxima::EditMenu)
EVT_MENU(menu_history_next, wxMaxima::EditMenu)
EVT_MENU(menu_cut, wxMaxima::EditMenu)
EVT_MENU(menu_paste, wxMaxima::EditMenu)
EVT_MENU(menu_paste_input, wxMaxima::EditMenu)
EVT_MENU(MathCtrl::popid_cut, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_paste, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_select_all, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_comment_selection, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_divide_cell, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_evaluate, wxMaxima::PopupMenu)
EVT_MENU(MathCtrl::popid_merge_cells, wxMaxima::PopupMenu)
EVT_MENU(menu_evaluate_all_visible, wxMaxima::MaximaMenu)
EVT_MENU(menu_evaluate_all, wxMaxima::MaximaMenu)
EVT_MENU(ToolBar::tb_evaltillhere, wxMaxima::MaximaMenu)
EVT_IDLE(wxMaxima::OnIdle)
EVT_MENU(menu_remove_output, wxMaxima::EditMenu)
EVT_MENU_RANGE(menu_recent_document_0, menu_recent_document_9, wxMaxima::OnRecentDocument)
EVT_MENU(menu_insert_image, wxMaxima::InsertMenu)
EVT_MENU_RANGE(menu_pane_hideall, menu_pane_stats, wxMaxima::ShowPane)
EVT_MENU(menu_show_toolbar, wxMaxima::EditMenu)
EVT_LISTBOX_DCLICK(history_ctrl_id, wxMaxima::HistoryDClick)
EVT_LISTBOX_DCLICK(structure_ctrl_id, wxMaxima::StructureDClick)
EVT_BUTTON(menu_stats_histogram, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_piechart, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_scatterplot, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_barsplot, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_boxplot, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_mean, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_median, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_var, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_dev, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_tt1, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_tt2, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_tnorm, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_linreg, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_lsquares, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_readm, wxMaxima::StatsMenu)
EVT_BUTTON(menu_stats_enterm, wxMaxima::AlgebraMenu)
EVT_BUTTON(menu_stats_subsample, wxMaxima::StatsMenu)
EVT_BUTTON(menu_format_title, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_text, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_subsubsection, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_subsection, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_section, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_pagebreak, wxMaxima::InsertMenu)
EVT_BUTTON(menu_format_image, wxMaxima::InsertMenu)
EVT_MENU(menu_edit_find, wxMaxima::EditMenu)
EVT_FIND(wxID_ANY, wxMaxima::OnFind)
EVT_FIND_NEXT(wxID_ANY, wxMaxima::OnFind)
EVT_FIND_REPLACE(wxID_ANY, wxMaxima::OnReplace)
EVT_FIND_REPLACE_ALL(wxID_ANY, wxMaxima::OnReplaceAll)
EVT_FIND_CLOSE(wxID_ANY, wxMaxima::OnFindClose)

END_EVENT_TABLE()

/* Local Variables:       */
/* mode: text             */
/* c-file-style:  "linux" */
/* c-basic-offset: 2      */
/* indent-tabs-mode: nil  */
