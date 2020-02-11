// -*- mode: c++; c-file-style: "linux"; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
//  Copyright (C) 2009-2015 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//  Copyright (C) 2015-2019 Gunter Königsmann     <wxMaxima@physikbuch.de>
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
//  SPDX-License-Identifier: GPL-2.0+

/*! \file
  This file defines the class AutoComplete.

  AutoComplete creates the list of autocompletions for a string and allows
  dynamically appending maxima commands to this list as soon as they are defined.
*/

#include <wx/sstream.h>
#include "Autocomplete.h"
#include "Dirstructure.h"
#include "Version.h"
#include <wx/textfile.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>
#include "ErrorRedirector.h"

AutoComplete::AutoComplete(Configuration *configuration)
{
  m_configuration = configuration;
}

void AutoComplete::ClearWorksheetWords()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #endif
  m_worksheetWords.clear();
}

void AutoComplete::ClearLoadfileList()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteFiles)
  #endif
  m_wordList[loadfile] = m_builtInLoadFiles;
}
void AutoComplete::ClearDemofileList()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteFiles)
  #endif
  m_wordList[demofile] = m_builtInDemoFiles;
}

void AutoComplete::AddSymbols(wxString xml)
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #endif
  {
    wxXmlDocument xmldoc;
    wxStringInputStream xmlStream(xml);
    xmldoc.Load(xmlStream, wxT("UTF-8"));
    wxXmlNode *node = xmldoc.GetRoot();
    if(node != NULL)
    {
      wxXmlNode *children = node->GetChildren();
      while (children != NULL)
      {
        if(children->GetType() == wxXML_ELEMENT_NODE)
        { 
          if (children->GetName() == wxT("function"))
          {
            wxXmlNode *val = children->GetChildren();
            if(val)
            {
              wxString name = val->GetContent();
              AddSymbol_nowait(name, command);
            }
          }
          
          if (children->GetName() == wxT("template"))
          {
            wxXmlNode *val = children->GetChildren();
            if(val)
            {
              wxString name = val->GetContent();
              AddSymbol_nowait(name, tmplte);
            }
          }
          
          if (children->GetName() == wxT("unit"))
          {
            wxXmlNode *val = children->GetChildren();
            if(val)
            {
              wxString name = val->GetContent();
              AddSymbol_nowait(name, unit);
            }
          }
          
          if (children->GetName() == wxT("value"))
          {
            wxXmlNode *val = children->GetChildren();
            if(val)
            {
              wxString name = val->GetContent();
              AddSymbol_nowait(name, command);
            }
          }
        }
        children = children->GetNext();
      }
    }
  }
}
void AutoComplete::AddWorksheetWords(wxArrayString wordlist)
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #endif
  {
    wxArrayString::const_iterator it;
    for (it = wordlist.begin(); it != wordlist.end(); ++it)
      m_worksheetWords[*it] = 1;
  }
}

AutoComplete::~AutoComplete()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp taskwait
  #endif
}

void AutoComplete::LoadSymbols()
{
  #ifdef HAVE_OPENMP_TASKS
  wxLogMessage(_("Starting a background task that setups the autocomplete builtins list."));
  #pragma omp task
  #endif
  BuiltinSymbols_BackgroundTask();
  #ifdef HAVE_OPENMP_TASKS
  wxLogMessage(_("Starting a background task that setups the autocompletable files list."));
  #pragma omp task
  #endif
  LoadSymbols_BackgroundTask();
}

void AutoComplete::BuiltinSymbols_BackgroundTask()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #endif
  {
    m_wordList[command].Clear();
    m_wordList[tmplte].Clear();
    m_wordList[esccommand].Clear();
    m_wordList[unit].Clear();
  
    LoadBuiltinSymbols();

    for(Configuration::StringHash::const_iterator it = m_configuration->m_escCodes.begin();
        it != m_configuration->m_escCodes.end();
        ++it)
      m_wordList[esccommand].Add(it->first);
    m_wordList[command].Sort();
    m_wordList[tmplte].Sort();
    m_wordList[unit].Sort();
  }
}

void AutoComplete::LoadSymbols_BackgroundTask()
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteFiles)
  #endif
  {
    m_wordList[loadfile].Clear();
    m_wordList[demofile].Clear();
    m_wordList[generalfile].Clear();
    // Error dialogues need to be created by the foreground thread.
    SuppressErrorDialogs suppressor;
    wxString line;

    /// Load private symbol list (do something different on Windows).
    wxString privateList;
    privateList = Dirstructure::Get()->UserAutocompleteFile();

    if (wxFileExists(privateList))
    {
      wxTextFile priv(privateList);

      priv.Open();

      for (line = priv.GetFirstLine(); !priv.Eof(); line = priv.GetNextLine())
      {
        if (line.StartsWith(wxT("FUNCTION: ")) ||
            line.StartsWith(wxT("OPTION  : ")))
          m_wordList[command].Add(line.Mid(10));
        else if (line.StartsWith(wxT("TEMPLATE: ")))
          m_wordList[tmplte].Add(FixTemplate(line.Mid(10)));
        else if (line.StartsWith(wxT("UNIT: ")))
          m_wordList[unit].Add(FixTemplate(line.Mid(6)));
      }

      priv.Close();
    }
  
    // Prepare a list of all built-in loadable files of maxima.
    {
      GetMacFiles_includingSubdirs maximaLispIterator (m_builtInLoadFiles);
      if(m_configuration->MaximaShareDir() != wxEmptyString)
      {
        wxFileName shareDir(m_configuration->MaximaShareDir() + "/");
        shareDir.MakeAbsolute();
        wxLogMessage(
          wxString::Format(
            _("Autocompletion: Scanning %s for loadable lisp files."),
            shareDir.GetFullPath().utf8_str()));
        wxDir maximadir(shareDir.GetFullPath());
        if(maximadir.IsOpened())
          maximadir.Traverse(maximaLispIterator);
      }
      GetMacFiles userLispIterator (m_builtInLoadFiles);
 
      wxFileName userDir(Dirstructure::Get()->UserConfDir() + "/");
      userDir.MakeAbsolute();
      wxDir maximauserfilesdir(userDir.GetFullPath());
      wxLogMessage(
        wxString::Format(
          _("Autocompletion: Scanning %s for loadable lisp files."),
          userDir.GetFullPath().utf8_str()));
      if(maximauserfilesdir.IsOpened())
        maximauserfilesdir.Traverse(userLispIterator);
    }
  

    // Prepare a list of all built-in demos of maxima.
    {
      wxFileName demoDir(m_configuration->MaximaShareDir() + "/");
      demoDir.MakeAbsolute();
      demoDir.RemoveLastDir();
      GetDemoFiles_includingSubdirs maximaLispIterator (m_builtInDemoFiles);
      wxLogMessage(
        wxString::Format(
          _("Autocompletion: Scanning %s for loadable demo files."),
          demoDir.GetFullPath().utf8_str()));

      wxDir maximadir(demoDir.GetFullPath());
      if(maximadir.IsOpened())
        maximadir.Traverse(maximaLispIterator);
    }
    m_builtInLoadFiles.Sort();
    m_builtInDemoFiles.Sort();
  }
}

void AutoComplete::UpdateDemoFiles(wxString partial, wxString maximaDir)
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteFiles)
  #endif
  {
    // Remove the opening quote from the partial.
    if(partial[0] == wxT('\"'))
      partial = partial.Right(partial.Length()-1);
  
    partial.Replace(wxFileName::GetPathSeparator(), "/");
    int pos;
    if ((pos = partial.Find(wxT('/'), true)) == wxNOT_FOUND)
      partial = wxEmptyString;
    else
      partial = partial.Left(pos);
    wxString prefix = partial + wxT("/");
  
    // Determine if we need to add the path to maxima's current dir to the path in partial
    if(!wxFileName(partial).IsAbsolute())
    {
      partial = maximaDir + wxFileName::GetPathSeparator() + partial;
      partial.Replace(wxFileName::GetPathSeparator(), "/");
    }
  
    // Determine the name of the directory
    if((partial != wxEmptyString) && wxDirExists(partial))
      partial += "/";

    // Remove all files from the maxima directory from the demo file list
    ClearDemofileList();

    // Add all files from the maxima directory to the demo file list
    if(partial != wxT("//"))
    {
      GetDemoFiles userLispIterator(m_wordList[demofile], prefix);
      wxDir demofilesdir(partial);
      if(demofilesdir.IsOpened())
        demofilesdir.Traverse(userLispIterator);
    }
  }
}

void AutoComplete::UpdateGeneralFiles(wxString partial, wxString maximaDir)
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteFiles)
  #endif
  {
    // Remove the opening quote from the partial.
    if(partial[0] == wxT('\"'))
      partial = partial.Right(partial.Length()-1);
  
    partial.Replace(wxFileName::GetPathSeparator(), "/");
    int pos;
    if ((pos = partial.Find(wxT('/'), true)) == wxNOT_FOUND)
      partial = wxEmptyString;
    else
      partial = partial.Left(pos);
    wxString prefix = partial + wxT("/");
  
    // Determine if we need to add the path to maxima's current dir to the path in partial
    if(!wxFileName(partial).IsAbsolute())
    {
      partial = maximaDir + wxFileName::GetPathSeparator() + partial;
      partial.Replace(wxFileName::GetPathSeparator(), "/");
    }
  
    // Determine the name of the directory
    if((partial != wxEmptyString) && wxDirExists(partial))
      partial += "/";
  
    // Add all files from the maxima directory to the demo file list
    if(partial != wxT("//"))
    {
      GetGeneralFiles fileIterator(m_wordList[generalfile], prefix);
      wxDir generalfilesdir(partial);
      if(generalfilesdir.IsOpened())
        generalfilesdir.Traverse(fileIterator);
    }
  }
}

void AutoComplete::UpdateLoadFiles(wxString partial, wxString maximaDir)
{
  #ifdef HAVE_OPENMP_TASKS
  wxLogMessage(_("Starting a background task that scans for autocompletible file names."));
  #pragma omp critical (AutocompleteFiles)
  #endif
  {
    // Remove the opening quote from the partial.
    if(partial[0] == wxT('\"'))
      partial = partial.Right(partial.Length()-1);
  
    partial.Replace(wxFileName::GetPathSeparator(), "/");
    int pos;
    if ((pos = partial.Find(wxT('/'), true)) == wxNOT_FOUND)
      partial = wxEmptyString;
    else
      partial = partial.Left(pos);
    wxString prefix = partial + wxT("/");
  
    // Determine if we need to add the path to maxima's current dir to the path in partial
    if(!wxFileName(partial).IsAbsolute())
    {
      partial = maximaDir + wxFileName::GetPathSeparator() + partial;
      partial.Replace(wxFileName::GetPathSeparator(), "/");
    }
  
    // Determine the name of the directory
    if((partial != wxEmptyString) && wxDirExists(partial))
      partial += "/";

    // Remove all files from the maxima directory from the load file list
    ClearLoadfileList();

    // Add all files from the maxima directory to the load file list
    if(partial != wxT("//"))
    {
      GetMacFiles userLispIterator(m_wordList[loadfile], prefix);
      wxDir loadfilesdir(partial);
      if(loadfilesdir.IsOpened())
        loadfilesdir.Traverse(userLispIterator);
    }
  }
}

/// Returns a string array with functions which start with partial.
wxArrayString AutoComplete::CompleteSymbol(wxString partial, autoCompletionType type)
{
  wxArrayString completions;
  wxArrayString perfectCompletions;

  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #pragma omp critical (AutocompleteFiles)
  #endif
  {
    if(
      ((type == AutoComplete::demofile) || (type == AutoComplete::loadfile)) &&
      (partial.EndsWith("\""))
      )
      partial = partial.Left(partial.Length() - 1);
  
    wxASSERT_MSG((type >= command) && (type <= unit), _("Bug: Autocompletion requested for unknown type of item."));
  
    if (type != tmplte)
    {
      for (size_t i = 0; i < m_wordList[type].GetCount(); i++)
      {
        if (m_wordList[type][i].StartsWith(partial) &&
            completions.Index(m_wordList[type][i]) == wxNOT_FOUND)
          completions.Add(m_wordList[type][i]);
      }
    }
    else
    {
      for (size_t i = 0; i < m_wordList[type].GetCount(); i++)
      {
        wxString templ = m_wordList[type][i];
        if (templ.StartsWith(partial))
        {
          if (completions.Index(templ) == wxNOT_FOUND)
            completions.Add(templ);
          if (templ.SubString(0, templ.Find(wxT("(")) - 1) == partial &&
              perfectCompletions.Index(templ) == wxNOT_FOUND)
            perfectCompletions.Add(templ);
        }
      }
    }

    // Add a list of words that were definied on the work sheet but that aren't
    // defined as maxima commands or functions.
    if (type == command)
    {
      WorksheetWords::const_iterator it;
      for (it = m_worksheetWords.begin(); it != m_worksheetWords.end(); ++it)
      {
        if (it->first.StartsWith(partial))
        {
          if (completions.Index(it->first) == wxNOT_FOUND)
          {
            completions.Add(it->first);
          }
        }
      }
    }

    completions.Sort();
  }
  if (perfectCompletions.Count() > 0)
    return perfectCompletions;
  return completions;
}

void AutoComplete::AddSymbol(wxString fun, autoCompletionType type)
{
  #ifdef HAVE_OPENMP_TASKS
  #pragma omp critical (AutocompleteBuiltins)
  #pragma omp critical (AutocompleteFiles)
  #endif
  AddSymbol_nowait(fun, type);
}

void AutoComplete::AddSymbol_nowait(wxString fun, autoCompletionType type)
{
  /// Check for function of template
  if (fun.StartsWith(wxT("FUNCTION: ")))
  {
    fun = fun.Mid(10);
    type = command;
  }
  else if (fun.StartsWith(wxT("TEMPLATE: ")))
  {
    fun = fun.Mid(10);
    type = tmplte;
  }
  else if (fun.StartsWith(wxT("UNIT: ")))
  {
    fun = fun.Mid(6);
    type = unit;
  }

  /// Add symbols
  if ((type != tmplte) && m_wordList[type].Index(fun, true, true) == wxNOT_FOUND)
    m_wordList[type].Add(fun);

  /// Add templates - for given function and given argument count we
  /// only add one template. We count the arguments by counting '<'
  if (type == tmplte)
  {
    fun = FixTemplate(fun);
    wxString funName = fun.SubString(0, fun.Find(wxT("(")));
    long count = fun.Freq('<');
    size_t i = 0;
    for (i = 0; i < m_wordList[type].GetCount(); i++)
    {
      wxString t = m_wordList[type][i];
      if (t.StartsWith(funName) && (t.Freq('<') == count))
        break;
    }
    if (i == m_wordList[type].GetCount())
      m_wordList[type].Add(fun);
  }
}


wxString AutoComplete::FixTemplate(wxString templ)
{
  templ.Replace(wxT(" "), wxEmptyString);
  templ.Replace(wxT(",..."), wxEmptyString);

  /// This will change optional arguments
  m_args.ReplaceAll(&templ, wxT("<[\\1]>"));

  return templ;
}

wxRegEx AutoComplete::m_args("[[]<([^>]*)>[]]");
