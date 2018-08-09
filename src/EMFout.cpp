﻿// -*- mode: c++; c-file-style: "linux"; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
//  Copyright (C) 2004-2015 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//  Copyright (C) 2015-2018 Gunter Königsmann <wxMaxima@physikbuch.de>
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
  This file defines the class Emfout that renders math as scalable emf graphics.
 */

#if wxUSE_ENH_METAFILE==1

#include "EMFout.h"
#include <wx/txtstrm.h> 
#include <wx/filename.h> 
#include <wx/wfstream.h>
#include "Configuration.h"
#include "GroupCell.h"
#include <wx/config.h>
#include <wx/clipbrd.h>

Emfout::Emfout(Configuration **configuration, wxString filename, double scale)
{
  m_width = m_height = -1;
  m_configuration = configuration;
  m_oldconfig = *m_configuration;
  m_tree = NULL;
  m_scale = scale;
  m_emfFormat = wxDataFormat(wxT("image/x-emf"));

  m_filename = filename;
  if (m_filename == wxEmptyString)
    m_filename = wxFileName::CreateTempFileName(wxT("wxmaxima_"));

  m_dc = NULL;
  
  wxString m_tempFileName = wxFileName::CreateTempFileName(wxT("wxmaxima_size_"));
  m_recalculationDc = new wxEnhMetaFileDC(m_tempFileName,700*m_scale,50000*m_scale,20*m_scale);
#if wxCHECK_VERSION(3, 1, 0)
  m_recalculationDc->SetBitmapHandler(new wxEMFBitmapEmbedHandler());
#endif
  *m_configuration = new Configuration(*m_recalculationDc);
  (*m_configuration)->ShowCodeCells(m_oldconfig->ShowCodeCells());
  (*m_configuration)->SetClientWidth(700*m_scale);
  (*m_configuration)->SetZoomFactor_temporarily(1);
  // The last time I tried it the vertical positioning of the elements
  // of a big unicode parenthesis wasn't accurate enough in emf to be
  // usable. Also the probability was high that the right font wasn't
  // available in inkscape.
  (*m_configuration)->SetGrouphesisDrawMode(Configuration::handdrawn);
  MathCell::ClipToDrawRegion(false);
  (*m_configuration)->SetForceUpdate(true);
}

Emfout::~Emfout()
{
  wxDELETE(m_tree);
  wxDELETE(*m_configuration);
  wxDELETE(m_dc);
  wxDELETE(m_recalculationDc);
  if(wxFileExists(m_tempFileName))
  {
    // We don't want a braindead virus scanner that disallows us to delete our temp
    // files to trigger asserts.
    wxRemoveFile(m_tempFileName);
  }
  *m_configuration = m_oldconfig;
  MathCell::ClipToDrawRegion(true);
  (*m_configuration)->SetForceUpdate(false);
}

wxSize Emfout::SetData(MathCell *tree)
{
  wxDELETE(m_tree);
  m_tree = tree;
  if(m_tree != NULL)
  {
    m_tree = tree;
    m_tree->ResetSize();
    if(Layout())
      return wxSize(m_width / m_scale, m_height / m_scale);  
    else
      return wxSize(-1,-1);
  }
  else
    return wxSize(-1,-1);
}

bool Emfout::Layout()
{
  (*m_configuration)->SetContext(*m_recalculationDc);
  
  if (m_tree->GetType() != MC_TYPE_GROUP)
  {
    RecalculateWidths();
    BreakUpCells();
    BreakLines();
    RecalculateHeight();
  }
  else
  {
    GroupCell *tmp = dynamic_cast<GroupCell *>(m_tree);
    while (tmp != NULL)
    {
      tmp->Recalculate();
      tmp = dynamic_cast<GroupCell *>(tmp->m_next);
    }
  }

  if(!m_recalculationDc->IsOk())
  {
    return false;
  }

  GetMaxPoint(&m_width, &m_height);

  wxDELETE(m_dc);
  // Let's switch to a DC of the right size for our object.
  m_dc = new wxEnhMetaFileDC(m_filename, m_width, m_height, 20*m_scale);
#if wxCHECK_VERSION(3, 1, 0)
  m_dc->SetBitmapHandler(new wxEMFBitmapEmbedHandler());
#endif
  (*m_configuration)->SetContext(*m_dc);
  
  Draw();
  wxDELETE(m_dc);
  m_dc = NULL;
  return true;
}

double Emfout::GetRealWidth()
{
  return m_width / m_scale;
}

double Emfout::GetRealHeight()
{
  return m_height / m_scale;
}

void Emfout::RecalculateHeight()
{
  int fontsize = 12;
  wxConfig::Get()->Read(wxT("fontSize"), &fontsize);
  int mfontsize = fontsize;
  wxConfig::Get()->Read(wxT("mathfontsize"), &mfontsize);
  MathCell *tmp = m_tree;

  while (tmp != NULL)
  {
    tmp->RecalculateHeight(tmp->IsMath() ? mfontsize : fontsize);
    tmp = tmp->m_next;
  }
}

void Emfout::RecalculateWidths()
{
  int fontsize = 12;
  wxConfig::Get()->Read(wxT("fontSize"), &fontsize);
  int mfontsize = fontsize;
  wxConfig::Get()->Read(wxT("mathfontsize"), &mfontsize);

  MathCell *tmp = m_tree;

  while (tmp != NULL)
  {
    tmp->RecalculateWidths(tmp->IsMath() ? mfontsize : fontsize);
    tmp = tmp->m_next;
  }
}

void Emfout::BreakLines()
{
  int fullWidth = 500*m_scale;
  int currentWidth = 0;

  MathCell *tmp = m_tree;

  while (tmp != NULL)
  {
    if (!tmp->m_isBroken)
    {
      tmp->BreakLine(false);
      tmp->ResetData();
      if (tmp->BreakLineHere() ||
          (currentWidth + tmp->GetWidth() >= fullWidth))
      {
        currentWidth = tmp->GetWidth();
        tmp->BreakLine(true);
      }
      else
        currentWidth += (tmp->GetWidth() + MC_CELL_SKIP);
    }
    tmp = tmp->m_nextToDraw;
  }
}

void Emfout::GetMaxPoint(int *width, int *height)
{
  MathCell *tmp = m_tree;
  int currentHeight = 0;
  int currentWidth = 0;
  *width = 0;
  *height = 0;
  bool bigSkip = false;
  bool firstCell = true;
  while (tmp != NULL)
  {
    if (!tmp->m_isBroken)
    {
      if (tmp->BreakLineHere() || firstCell)
      {
        firstCell = false;
        currentHeight += tmp->GetMaxHeight();
        if (bigSkip)
          currentHeight += MC_LINE_SKIP;
        *height = currentHeight;
        currentWidth = tmp->GetWidth();
        *width = MAX(currentWidth, *width);
      }
      else
      {
        currentWidth += (tmp->GetWidth() + MC_CELL_SKIP);
        *width = MAX(currentWidth - MC_CELL_SKIP, *width);
      }
      bigSkip = tmp->m_bigSkip;
    }
    tmp = tmp->m_nextToDraw;
  }
}

void Emfout::Draw()
{
  MathCell *tmp = m_tree;

  if (tmp != NULL)
  {
    wxPoint point;
    point.x = 0;
    point.y = tmp->GetMaxCenter();
    int fontsize = 12;
    int drop = tmp->GetMaxDrop();

    wxConfig::Get()->Read(wxT("fontSize"), &fontsize);
    int mfontsize = fontsize;
    wxConfig::Get()->Read(wxT("mathfontsize"), &mfontsize);

    while (tmp != NULL)
    {
      if (!tmp->m_isBroken)
      {
        tmp->Draw(point, tmp->IsMath() ? mfontsize : fontsize);
        if ((tmp->m_next != NULL) && (tmp->m_next->BreakLineHere()))
        {
          point.x = 0;
          point.y += drop + tmp->m_next->GetMaxCenter();
          if (tmp->m_bigSkip)
            point.y += MC_LINE_SKIP;
          drop = tmp->m_next->GetMaxDrop();
        }
        else
          point.x += (tmp->GetWidth() + MC_CELL_SKIP);
      }
      else
      {
        if ((tmp->m_next != NULL) && (tmp->m_next->BreakLineHere()))
        {
          point.x = 0;
          point.y += drop + tmp->m_next->GetMaxCenter();
          if (tmp->m_bigSkip)
            point.y += MC_LINE_SKIP;
          drop = tmp->m_next->GetMaxDrop();
        }
      }
      tmp = tmp->m_nextToDraw;
    }
  }
}

Emfout::EMFDataObject::EMFDataObject() : wxCustomDataObject(m_emfFormat)
{
}

Emfout::EMFDataObject::EMFDataObject(wxMemoryBuffer data) : wxCustomDataObject(m_emfFormat)
{
  SetData(data.GetBufSize(), data.GetData());
}


wxDataFormat Emfout::m_emfFormat;

Emfout::EMFDataObject *Emfout::GetDataObject()
{
  wxMemoryBuffer emfContents;
  {
    char *data =(char *) malloc(8192);
    wxFileInputStream str(m_filename);
    if(str.IsOk())
      while (!str.Eof())
      {
        str.Read(data,8192);
        emfContents.AppendData(data,str.LastRead());
      }
    free(data);
  }
  if((m_filename != wxEmptyString) && (wxFileExists(m_filename)))
  {
    wxRemoveFile(m_filename);
  }
  m_filename = wxEmptyString;
  
  return new EMFDataObject(emfContents);
}

bool Emfout::ToClipboard()
{
  if (wxTheClipboard->Open())
  {
    bool res = wxTheClipboard->SetData(GetDataObject());
    wxTheClipboard->Close();
    m_filename = wxEmptyString;
    return res;
  }
  return false;
}

void Emfout::BreakUpCells()
{
  MathCell *tmp = m_tree;
  int fontsize = 12;
  wxConfig::Get()->Read(wxT("fontSize"), &fontsize);
  int mfontsize = fontsize;
  wxConfig::Get()->Read(wxT("mathfontsize"), &mfontsize);

  while (tmp != NULL)
  {
    if (tmp->GetWidth() > 500*m_scale)
    {
      if (tmp->BreakUp())
      {
        tmp->RecalculateWidths(tmp->IsMath() ? mfontsize : fontsize);
        tmp->RecalculateHeight(tmp->IsMath() ? mfontsize : fontsize);
      }
    }
    tmp = tmp->m_nextToDraw;
  }
}
#endif // wxUSE_ENH_METAFILE
