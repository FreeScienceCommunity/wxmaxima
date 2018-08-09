﻿// -*- mode: c++; c-file-style: "linux"; c-basic-offset: 2; indent-tabs-mode: nil -*-
//
//  Copyright (C) 2004-2015 Andrej Vodopivec <andrej.vodopivec@gmail.com>
//  Copyright (C) 2015      Gunter Königsmann <wxMaxima@physikbuch.de>
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

#ifndef EMFOUT_H
#define EMFOUT_H

#if wxUSE_ENH_METAFILE==1
#include "MathCell.h"
#include <wx/dcgraph.h>
/* Renders portions of the work sheet (including 2D maths) as emf.

   This is used for exporting HTML with embedded maths as a scalable vector
   graphics and for them on the clipboard
 */
class Emfout
{
public:
  /*! The constructor.
  */
  Emfout(Configuration **configuration, wxString filename = wxEmptyString, double scale = 1.0);

  ~Emfout();
  
  /*! Renders tree as emf
    
    \param tree The list of cells that is to be rendered
    \return true, if the emfout could be created.
   */
  wxSize SetData(MathCell *tree);
  
  //! Copies the emf representation of the list of cells that was passed to SetData()
  bool ToClipboard();

protected:
  void DestroyTree();

  void RecalculateWidths();

  void BreakLines();

  void RecalculateHeight();

  void GetMaxPoint(int *width, int *height);

  void BreakUpCells();

  bool Layout();

  void Draw();

  MathCell *m_tree;

  double GetRealHeight();

  double GetRealWidth();

  
  /*! An object that can be filled with EMF data for the clipboard
   */
  class EMFDataObject : public wxCustomDataObject
  {
  public:
    EMFDataObject(wxMemoryBuffer data);

    EMFDataObject();

  private:
    //! A class that publishes MathML data to the clipboard
    wxCharBuffer m_databuf;
  };

private:
  //! The name of a temp file we create while calculating the emf size.
  wxString m_tempFileName;
  //! The draw context we draw to during recalculation.
  wxEnhMetaFileDC *m_recalculationDc;
  //! The draw context we draw to.
  wxEnhMetaFileDC *m_dc;
  static wxDataFormat m_emfFormat;
  wxString m_filename;
  Configuration **m_configuration, *m_oldconfig;
  //! How many times the natural resolution do we want this emfout to be?
  double m_scale;
  //! The width of the current emfout;
  int m_width;
  //! The height of the current emfout;
  int m_height;
  //! The resolution of the emfout.
  wxSize m_ppi;

public:
  //! Returns the emf representation in a format that can be placed on the clipBoard.
  EMFDataObject *GetDataObject();
};
#endif // wxUSE_ENH_METAFILE
#endif // EMFOUT_H
