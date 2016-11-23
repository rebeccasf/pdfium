// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FWL_CORE_CFWL_COMBOBOX_H_
#define XFA_FWL_CORE_CFWL_COMBOBOX_H_

#include <memory>
#include <vector>

#include "xfa/fwl/core/cfwl_widget.h"
#include "xfa/fwl/core/ifwl_combobox.h"

class CFWL_ComboBox : public CFWL_Widget, public IFWL_ComboBox::DataProvider {
 public:
  explicit CFWL_ComboBox(const CFWL_App* pApp);
  ~CFWL_ComboBox() override;

  void Initialize();

  // IFWL_ComboBox::DataProvider
  FX_FLOAT GetListHeight(IFWL_Widget* pWidget) override;

  void AddString(const CFX_WideStringC& wsText);
  bool RemoveAt(int32_t iIndex);  // Returns false iff |iIndex| out of range.
  void RemoveAll();

  void GetTextByIndex(int32_t iIndex, CFX_WideString& wsText) const;
  int32_t GetCurSel() const;
  void SetCurSel(int32_t iSel);

  void SetEditText(const CFX_WideString& wsText);
  void GetEditText(CFX_WideString& wsText,
                   int32_t nStart = 0,
                   int32_t nCount = -1) const;

  void OpenDropDownList(bool bActivate);

  bool EditCanUndo();
  bool EditCanRedo();
  bool EditUndo();
  bool EditRedo();
  bool EditCanCopy();
  bool EditCanCut();
  bool EditCanSelectAll();
  bool EditCopy(CFX_WideString& wsCopy);
  bool EditCut(CFX_WideString& wsCut);
  bool EditPaste(const CFX_WideString& wsPaste);
  void EditSelectAll();
  void EditDelete();
  void EditDeSelect();

  void GetBBox(CFX_RectF& rect);
  void EditModifyStylesEx(uint32_t dwStylesExAdded, uint32_t dwStylesExRemoved);

 private:
  FX_FLOAT m_fMaxListHeight;
};

#endif  // XFA_FWL_CORE_CFWL_COMBOBOX_H_
