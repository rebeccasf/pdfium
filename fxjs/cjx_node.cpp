// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "fxjs/cjx_node.h"

#include <memory>
#include <vector>

#include "core/fxcrt/cfx_decimal.h"
#include "core/fxcrt/cfx_memorystream.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/xml/cfx_xmlelement.h"
#include "core/fxcrt/xml/cfx_xmlnode.h"
#include "core/fxcrt/xml/cfx_xmltext.h"
#include "fxjs/cfxjse_arguments.h"
#include "fxjs/cfxjse_engine.h"
#include "xfa/fxfa/cxfa_ffnotify.h"
#include "xfa/fxfa/cxfa_ffwidget.h"
#include "xfa/fxfa/parser/cxfa_arraynodelist.h"
#include "xfa/fxfa/parser/cxfa_attachnodelist.h"
#include "xfa/fxfa/parser/cxfa_layoutprocessor.h"
#include "xfa/fxfa/parser/cxfa_measurement.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_occur.h"
#include "xfa/fxfa/parser/cxfa_simple_parser.h"
#include "xfa/fxfa/parser/xfa_utils.h"

namespace {

void XFA_DeleteWideString(void* pData) {
  delete static_cast<WideString*>(pData);
}

void XFA_CopyWideString(void*& pData) {
  if (pData) {
    WideString* pNewData =
        new WideString(*reinterpret_cast<WideString*>(pData));
    pData = pNewData;
  }
}

XFA_MAPDATABLOCKCALLBACKINFO deleteWideStringCallBack = {XFA_DeleteWideString,
                                                         XFA_CopyWideString};

void StrToRGB(const WideString& strRGB, int32_t& r, int32_t& g, int32_t& b) {
  r = 0;
  g = 0;
  b = 0;

  wchar_t zero = '0';
  int32_t iIndex = 0;
  int32_t iLen = strRGB.GetLength();
  for (int32_t i = 0; i < iLen; ++i) {
    wchar_t ch = strRGB[i];
    if (ch == L',')
      ++iIndex;
    if (iIndex > 2)
      break;

    int32_t iValue = ch - zero;
    if (iValue >= 0 && iValue <= 9) {
      switch (iIndex) {
        case 0:
          r = r * 10 + iValue;
          break;
        case 1:
          g = g * 10 + iValue;
          break;
        default:
          b = b * 10 + iValue;
          break;
      }
    }
  }
}

enum XFA_KEYTYPE {
  XFA_KEYTYPE_Custom,
  XFA_KEYTYPE_Element,
};

void* GetMapKey_Custom(const WideStringView& wsKey) {
  uint32_t dwKey = FX_HashCode_GetW(wsKey, false);
  return (void*)(uintptr_t)((dwKey << 1) | XFA_KEYTYPE_Custom);
}

void* GetMapKey_Element(XFA_Element eType, XFA_ATTRIBUTE eAttribute) {
  return (void*)(uintptr_t)((static_cast<int32_t>(eType) << 16) |
                            (eAttribute << 8) | XFA_KEYTYPE_Element);
}

const XFA_ATTRIBUTEINFO* GetAttributeOfElement(XFA_Element eElement,
                                               XFA_ATTRIBUTE eAttribute,
                                               uint32_t dwPacket) {
  int32_t iCount = 0;
  const uint8_t* pAttr = XFA_GetElementAttributes(eElement, iCount);
  if (!pAttr || iCount < 1)
    return nullptr;

  if (!std::binary_search(pAttr, pAttr + iCount, eAttribute))
    return nullptr;

  const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttribute);
  ASSERT(pInfo);
  if (dwPacket == XFA_XDPPACKET_UNKNOWN)
    return pInfo;
  return (dwPacket & pInfo->dwPackets) ? pInfo : nullptr;
}

struct XFA_ExecEventParaInfo {
 public:
  uint32_t m_uHash;
  const wchar_t* m_lpcEventName;
  XFA_EVENTTYPE m_eventType;
  uint32_t m_validFlags;
};

const XFA_ExecEventParaInfo gs_eventParaInfos[] = {
    {0x02a6c55a, L"postSubmit", XFA_EVENT_PostSubmit, 0},
    {0x0ab466bb, L"preSubmit", XFA_EVENT_PreSubmit, 0},
    {0x109d7ce7, L"mouseEnter", XFA_EVENT_MouseEnter, 5},
    {0x17fad373, L"postPrint", XFA_EVENT_PostPrint, 0},
    {0x1bfc72d9, L"preOpen", XFA_EVENT_PreOpen, 7},
    {0x2196a452, L"initialize", XFA_EVENT_Initialize, 1},
    {0x27410f03, L"mouseExit", XFA_EVENT_MouseExit, 5},
    {0x33c43dec, L"docClose", XFA_EVENT_DocClose, 0},
    {0x361fa1b6, L"preSave", XFA_EVENT_PreSave, 0},
    {0x36f1c6d8, L"preSign", XFA_EVENT_PreSign, 6},
    {0x4731d6ba, L"exit", XFA_EVENT_Exit, 2},
    {0x56bf456b, L"docReady", XFA_EVENT_DocReady, 0},
    {0x7233018a, L"validate", XFA_EVENT_Validate, 1},
    {0x8808385e, L"indexChange", XFA_EVENT_IndexChange, 3},
    {0x891f4606, L"change", XFA_EVENT_Change, 4},
    {0x9528a7b4, L"prePrint", XFA_EVENT_PrePrint, 0},
    {0x9f693b21, L"mouseDown", XFA_EVENT_MouseDown, 5},
    {0xcdce56b3, L"full", XFA_EVENT_Full, 4},
    {0xd576d08e, L"mouseUp", XFA_EVENT_MouseUp, 5},
    {0xd95657a6, L"click", XFA_EVENT_Click, 4},
    {0xdbfbe02e, L"calculate", XFA_EVENT_Calculate, 1},
    {0xe25fa7b8, L"postOpen", XFA_EVENT_PostOpen, 7},
    {0xe28dce7e, L"enter", XFA_EVENT_Enter, 2},
    {0xfc82d695, L"postSave", XFA_EVENT_PostSave, 0},
    {0xfd54fbb7, L"postSign", XFA_EVENT_PostSign, 6},
};

const XFA_ExecEventParaInfo* GetEventParaInfoByName(
    const WideStringView& wsEventName) {
  uint32_t uHash = FX_HashCode_GetW(wsEventName, false);
  int32_t iStart = 0;
  int32_t iEnd = (sizeof(gs_eventParaInfos) / sizeof(gs_eventParaInfos[0])) - 1;
  do {
    int32_t iMid = (iStart + iEnd) / 2;
    const XFA_ExecEventParaInfo* eventParaInfo = &gs_eventParaInfos[iMid];
    if (uHash == eventParaInfo->m_uHash)
      return eventParaInfo;
    if (uHash < eventParaInfo->m_uHash)
      iEnd = iMid - 1;
    else
      iStart = iMid + 1;
  } while (iStart <= iEnd);
  return nullptr;
}

}  // namespace

static void XFA_DefaultFreeData(void* pData) {}

static XFA_MAPDATABLOCKCALLBACKINFO gs_XFADefaultFreeData = {
    XFA_DefaultFreeData, nullptr};

XFA_MAPMODULEDATA::XFA_MAPMODULEDATA() {}

XFA_MAPMODULEDATA::~XFA_MAPMODULEDATA() {}

CJX_Node::CJX_Node(CXFA_Node* node) : node_(node), map_module_data_(nullptr) {}

CJX_Node::~CJX_Node() {
  RemoveMapModuleKey();
}

bool CJX_Node::HasAttribute(XFA_ATTRIBUTE eAttr, bool bCanInherit) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  return HasMapModuleKey(pKey, bCanInherit);
}

bool CJX_Node::SetAttribute(XFA_ATTRIBUTE eAttr,
                            const WideStringView& wsValue,
                            bool bNotify) {
  const XFA_ATTRIBUTEINFO* pAttr = XFA_GetAttributeByID(eAttr);
  if (!pAttr)
    return false;

  XFA_ATTRIBUTETYPE eType = pAttr->eType;
  if (eType == XFA_ATTRIBUTETYPE_NOTSURE) {
    const XFA_NOTSUREATTRIBUTE* pNotsure =
        XFA_GetNotsureAttribute(node_->GetElementType(), pAttr->eName);
    eType = pNotsure ? pNotsure->eType : XFA_ATTRIBUTETYPE_Cdata;
  }
  switch (eType) {
    case XFA_ATTRIBUTETYPE_Enum: {
      const XFA_ATTRIBUTEENUMINFO* pEnum = XFA_GetAttributeEnumByName(wsValue);
      return SetEnum(pAttr->eName,
                     pEnum ? pEnum->eName
                           : (XFA_ATTRIBUTEENUM)(intptr_t)(pAttr->pDefValue),
                     bNotify);
    } break;
    case XFA_ATTRIBUTETYPE_Cdata:
      return SetCData(pAttr->eName, WideString(wsValue), bNotify);
    case XFA_ATTRIBUTETYPE_Boolean:
      return SetBoolean(pAttr->eName, wsValue != L"0", bNotify);
    case XFA_ATTRIBUTETYPE_Integer:
      return SetInteger(pAttr->eName,
                        FXSYS_round(FXSYS_wcstof(wsValue.unterminated_c_str(),
                                                 wsValue.GetLength(), nullptr)),
                        bNotify);
    case XFA_ATTRIBUTETYPE_Measure:
      return SetMeasure(pAttr->eName, CXFA_Measurement(wsValue), bNotify);
    default:
      break;
  }
  return false;
}

bool CJX_Node::SetAttribute(const WideStringView& wsAttr,
                            const WideStringView& wsValue,
                            bool bNotify) {
  const XFA_ATTRIBUTEINFO* pAttributeInfo = XFA_GetAttributeByName(wsValue);
  if (pAttributeInfo) {
    return SetAttribute(pAttributeInfo->eName, wsValue, bNotify);
  }
  void* pKey = GetMapKey_Custom(wsAttr);
  SetMapModuleString(pKey, wsValue);
  return true;
}

bool CJX_Node::GetAttribute(XFA_ATTRIBUTE eAttr,
                            WideString& wsValue,
                            bool bUseDefault) {
  const XFA_ATTRIBUTEINFO* pAttr = XFA_GetAttributeByID(eAttr);
  if (!pAttr)
    return false;

  XFA_ATTRIBUTETYPE eType = pAttr->eType;
  if (eType == XFA_ATTRIBUTETYPE_NOTSURE) {
    const XFA_NOTSUREATTRIBUTE* pNotsure =
        XFA_GetNotsureAttribute(node_->GetElementType(), pAttr->eName);
    eType = pNotsure ? pNotsure->eType : XFA_ATTRIBUTETYPE_Cdata;
  }
  switch (eType) {
    case XFA_ATTRIBUTETYPE_Enum: {
      XFA_ATTRIBUTEENUM eValue;
      if (!TryEnum(pAttr->eName, eValue, bUseDefault))
        return false;

      wsValue = GetAttributeEnumByID(eValue)->pName;
      return true;
    }
    case XFA_ATTRIBUTETYPE_Cdata: {
      WideStringView wsValueC;
      if (!TryCData(pAttr->eName, wsValueC, bUseDefault))
        return false;

      wsValue = wsValueC;
      return true;
    }
    case XFA_ATTRIBUTETYPE_Boolean: {
      bool bValue;
      if (!TryBoolean(pAttr->eName, bValue, bUseDefault))
        return false;

      wsValue = bValue ? L"1" : L"0";
      return true;
    }
    case XFA_ATTRIBUTETYPE_Integer: {
      int32_t iValue;
      if (!TryInteger(pAttr->eName, iValue, bUseDefault))
        return false;

      wsValue.Format(L"%d", iValue);
      return true;
    }
    case XFA_ATTRIBUTETYPE_Measure: {
      CXFA_Measurement mValue;
      if (!TryMeasure(pAttr->eName, mValue, bUseDefault))
        return false;

      mValue.ToString(&wsValue);
      return true;
    }
    default:
      return false;
  }
}

bool CJX_Node::GetAttribute(const WideStringView& wsAttr,
                            WideString& wsValue,
                            bool bUseDefault) {
  const XFA_ATTRIBUTEINFO* pAttributeInfo = XFA_GetAttributeByName(wsAttr);
  if (pAttributeInfo) {
    return GetAttribute(pAttributeInfo->eName, wsValue, bUseDefault);
  }
  void* pKey = GetMapKey_Custom(wsAttr);
  WideStringView wsValueC;
  if (GetMapModuleString(pKey, wsValueC)) {
    wsValue = wsValueC;
  }
  return true;
}

bool CJX_Node::RemoveAttribute(const WideStringView& wsAttr) {
  void* pKey = GetMapKey_Custom(wsAttr);
  RemoveMapModuleKey(pKey);
  return true;
}

void CJX_Node::Script_TreeClass_ResolveNode(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"resolveNode");
    return;
  }
  WideString wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  CFXJSE_Engine* pScriptContext = node_->GetDocument()->GetScriptContext();
  if (!pScriptContext)
    return;
  CXFA_Node* refNode = node_.Get();
  if (refNode->GetElementType() == XFA_Element::Xfa)
    refNode = ToNode(pScriptContext->GetThisObject());
  uint32_t dwFlag = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Attributes |
                    XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                    XFA_RESOLVENODE_Siblings;
  XFA_RESOLVENODE_RS resoveNodeRS;
  int32_t iRet = pScriptContext->ResolveObjects(
      refNode, wsExpression.AsStringView(), resoveNodeRS, dwFlag);
  if (iRet < 1) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  if (resoveNodeRS.dwFlags == XFA_RESOVENODE_RSTYPE_Nodes) {
    CXFA_Object* pObject = resoveNodeRS.objects.front();
    pArguments->GetReturnValue()->Assign(
        pScriptContext->GetJSValueFromMap(pObject));
  } else {
    const XFA_SCRIPTATTRIBUTEINFO* lpAttributeInfo =
        resoveNodeRS.pScriptAttribute;
    if (lpAttributeInfo && lpAttributeInfo->eValueType == XFA_SCRIPT_Object) {
      auto pValue =
          pdfium::MakeUnique<CFXJSE_Value>(pScriptContext->GetRuntime());
      (resoveNodeRS.objects.front()->*(lpAttributeInfo->lpfnCallback))(
          pValue.get(), false, (XFA_ATTRIBUTE)lpAttributeInfo->eAttribute);
      pArguments->GetReturnValue()->Assign(pValue.get());
    } else {
      pArguments->GetReturnValue()->SetNull();
    }
  }
}

void CJX_Node::Script_TreeClass_ResolveNodes(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"resolveNodes");
    return;
  }
  WideString wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (!pValue)
    return;
  uint32_t dwFlag = XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Attributes |
                    XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                    XFA_RESOLVENODE_Siblings;
  CXFA_Node* refNode = node_.Get();
  if (refNode->GetElementType() == XFA_Element::Xfa)
    refNode = ToNode(node_->GetDocument()->GetScriptContext()->GetThisObject());
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag, refNode);
}

void CJX_Node::Script_Som_ResolveNodeList(CFXJSE_Value* pValue,
                                          WideString wsExpression,
                                          uint32_t dwFlag,
                                          CXFA_Node* refNode) {
  CFXJSE_Engine* pScriptContext = node_->GetDocument()->GetScriptContext();
  if (!pScriptContext)
    return;
  XFA_RESOLVENODE_RS resoveNodeRS;
  if (!refNode)
    refNode = node_.Get();
  pScriptContext->ResolveObjects(refNode, wsExpression.AsStringView(),
                                 resoveNodeRS, dwFlag);
  CXFA_ArrayNodeList* pNodeList = new CXFA_ArrayNodeList(node_->GetDocument());
  if (resoveNodeRS.dwFlags == XFA_RESOVENODE_RSTYPE_Nodes) {
    for (CXFA_Object* pObject : resoveNodeRS.objects) {
      if (pObject->IsNode())
        pNodeList->Append(pObject->AsNode());
    }
  } else {
    CXFA_ValueArray valueArray(pScriptContext->GetRuntime());
    if (resoveNodeRS.GetAttributeResult(&valueArray) > 0) {
      for (CXFA_Object* pObject : valueArray.GetAttributeObject()) {
        if (pObject->IsNode())
          pNodeList->Append(pObject->AsNode());
      }
    }
  }
  pValue->SetObject(pNodeList, pScriptContext->GetJseNormalClass());
}

void CJX_Node::Script_TreeClass_All(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  uint32_t dwFlag = XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_ALL;
  WideString wsName;
  GetAttribute(XFA_ATTRIBUTE_Name, wsName);
  WideString wsExpression = wsName + L"[*]";
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag);
}

void CJX_Node::Script_TreeClass_Nodes(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  CFXJSE_Engine* pScriptContext = node_->GetDocument()->GetScriptContext();
  if (!pScriptContext)
    return;

  if (bSetting) {
    WideString wsMessage = L"Unable to set ";
    FXJSE_ThrowMessage(wsMessage.UTF8Encode().AsStringView());
  } else {
    CXFA_AttachNodeList* pNodeList =
        new CXFA_AttachNodeList(node_->GetDocument(), node_.Get());
    pValue->SetObject(pNodeList, pScriptContext->GetJseNormalClass());
  }
}

void CJX_Node::Script_TreeClass_ClassAll(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  uint32_t dwFlag = XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_ALL;
  WideString wsExpression = L"#" + node_->GetClassName() + L"[*]";
  Script_Som_ResolveNodeList(pValue, wsExpression, dwFlag);
}

void CJX_Node::Script_TreeClass_Parent(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  CXFA_Node* pParent = node_->GetNodeItem(XFA_NODEITEM_Parent);
  if (pParent)
    pValue->Assign(
        node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pParent));
  else
    pValue->SetNull();
}

void CJX_Node::Script_TreeClass_Index(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(node_->GetNodeSameNameIndex());
}

void CJX_Node::Script_TreeClass_ClassIndex(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(node_->GetNodeSameClassIndex());
}

void CJX_Node::Script_TreeClass_SomExpression(CFXJSE_Value* pValue,
                                              bool bSetting,
                                              XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  WideString wsSOMExpression;
  node_->GetSOMExpression(wsSOMExpression);
  pValue->SetString(wsSOMExpression.UTF8Encode().AsStringView());
}

void CJX_Node::Script_NodeClass_ApplyXSL(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"applyXSL");
    return;
  }
  WideString wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)wsExpression;
}

void CJX_Node::Script_NodeClass_AssignNode(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    node_->ThrowParamCountMismatchException(L"assignNode");
    return;
  }
  WideString wsExpression;
  WideString wsValue;
  int32_t iAction = 0;
  wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  if (iLength >= 2) {
    wsValue = WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringView());
  }
  if (iLength >= 3)
    iAction = pArguments->GetInt32(2);
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)wsExpression;
  (void)wsValue;
  (void)iAction;
}

void CJX_Node::Script_NodeClass_Clone(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"clone");
    return;
  }
  bool bClone = !!pArguments->GetInt32(0);
  CXFA_Node* pCloneNode = node_->Clone(bClone);
  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pCloneNode));
}

void CJX_Node::Script_NodeClass_GetAttribute(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"getAttribute");
    return;
  }
  WideString wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  WideString wsValue;
  GetAttribute(wsExpression.AsStringView(), wsValue);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetString(wsValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_NodeClass_GetElement(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 2) {
    node_->ThrowParamCountMismatchException(L"getElement");
    return;
  }
  WideString wsExpression;
  int32_t iValue = 0;
  wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  if (iLength >= 2)
    iValue = pArguments->GetInt32(1);
  CXFA_Node* pNode = GetProperty(
      iValue, XFA_GetElementTypeForName(wsExpression.AsStringView()));
  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pNode));
}

void CJX_Node::Script_NodeClass_IsPropertySpecified(
    CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    node_->ThrowParamCountMismatchException(L"isPropertySpecified");
    return;
  }

  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (!pValue)
    return;

  WideString wsExpression =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  const XFA_ATTRIBUTEINFO* pAttributeInfo =
      XFA_GetAttributeByName(wsExpression.AsStringView());
  bool bHas = pAttributeInfo ? HasAttribute(pAttributeInfo->eName) : false;
  if (!bHas) {
    bool bParent = iLength < 2 || pArguments->GetInt32(1);
    int32_t iIndex = iLength == 3 ? pArguments->GetInt32(2) : 0;
    XFA_Element eType = XFA_GetElementTypeForName(wsExpression.AsStringView());
    bHas = !!GetProperty(iIndex, eType);
    if (!bHas && bParent && node_->GetParent()) {
      // Also check on the parent.
      bHas = node_->GetParent()->JSNode()->HasAttribute(pAttributeInfo->eName);
      if (!bHas)
        bHas = !!node_->GetParent()->JSNode()->GetProperty(iIndex, eType);
    }
  }
  pValue->SetBoolean(bHas);
}

void CJX_Node::Script_NodeClass_LoadXML(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 3) {
    node_->ThrowParamCountMismatchException(L"loadXML");
    return;
  }

  bool bIgnoreRoot = true;
  bool bOverwrite = 0;
  ByteString wsExpression = pArguments->GetUTF8String(0);
  if (wsExpression.IsEmpty())
    return;
  if (iLength >= 2)
    bIgnoreRoot = !!pArguments->GetInt32(1);
  if (iLength >= 3)
    bOverwrite = !!pArguments->GetInt32(2);
  auto pParser =
      pdfium::MakeUnique<CXFA_SimpleParser>(node_->GetDocument(), false);
  if (!pParser)
    return;
  CFX_XMLNode* pXMLNode = pParser->ParseXMLData(wsExpression);
  if (!pXMLNode)
    return;
  if (bIgnoreRoot &&
      (pXMLNode->GetType() != FX_XMLNODE_Element ||
       XFA_RecognizeRichText(static_cast<CFX_XMLElement*>(pXMLNode)))) {
    bIgnoreRoot = false;
  }
  CXFA_Node* pFakeRoot = node_->Clone(false);
  WideStringView wsContentType = GetCData(XFA_ATTRIBUTE_ContentType);
  if (!wsContentType.IsEmpty()) {
    pFakeRoot->JSNode()->SetCData(XFA_ATTRIBUTE_ContentType,
                                  WideString(wsContentType));
  }

  std::unique_ptr<CFX_XMLNode> pFakeXMLRoot(pFakeRoot->GetXMLMappingNode());
  if (!pFakeXMLRoot) {
    CFX_XMLNode* pThisXMLRoot = node_->GetXMLMappingNode();
    pFakeXMLRoot = pThisXMLRoot ? pThisXMLRoot->Clone() : nullptr;
  }
  if (!pFakeXMLRoot) {
    pFakeXMLRoot =
        pdfium::MakeUnique<CFX_XMLElement>(WideString(node_->GetClassName()));
  }

  if (bIgnoreRoot) {
    CFX_XMLNode* pXMLChild = pXMLNode->GetNodeItem(CFX_XMLNode::FirstChild);
    while (pXMLChild) {
      CFX_XMLNode* pXMLSibling =
          pXMLChild->GetNodeItem(CFX_XMLNode::NextSibling);
      pXMLNode->RemoveChildNode(pXMLChild);
      pFakeXMLRoot->InsertChildNode(pXMLChild);
      pXMLChild = pXMLSibling;
    }
  } else {
    CFX_XMLNode* pXMLParent = pXMLNode->GetNodeItem(CFX_XMLNode::Parent);
    if (pXMLParent) {
      pXMLParent->RemoveChildNode(pXMLNode);
    }
    pFakeXMLRoot->InsertChildNode(pXMLNode);
  }
  pParser->ConstructXFANode(pFakeRoot, pFakeXMLRoot.get());
  pFakeRoot = pParser->GetRootNode();
  if (!pFakeRoot)
    return;

  if (bOverwrite) {
    CXFA_Node* pChild = node_->GetNodeItem(XFA_NODEITEM_FirstChild);
    CXFA_Node* pNewChild = pFakeRoot->GetNodeItem(XFA_NODEITEM_FirstChild);
    int32_t index = 0;
    while (pNewChild) {
      CXFA_Node* pItem = pNewChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      pFakeRoot->RemoveChild(pNewChild);
      node_->InsertChild(index++, pNewChild);
      pNewChild->SetFlag(XFA_NodeFlag_Initialized, true);
      pNewChild = pItem;
    }
    while (pChild) {
      CXFA_Node* pItem = pChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      node_->RemoveChild(pChild);
      pFakeRoot->InsertChild(pChild);
      pChild = pItem;
    }
    if (node_->GetPacketID() == XFA_XDPPACKET_Form &&
        node_->GetElementType() == XFA_Element::ExData) {
      CFX_XMLNode* pTempXMLNode = node_->GetXMLMappingNode();
      node_->SetXMLMappingNode(pFakeXMLRoot.release());
      node_->SetFlag(XFA_NodeFlag_OwnXMLNode, false);
      if (pTempXMLNode && !pTempXMLNode->GetNodeItem(CFX_XMLNode::Parent))
        pFakeXMLRoot.reset(pTempXMLNode);
      else
        pFakeXMLRoot = nullptr;
    }
    MoveBufferMapData(pFakeRoot, node_.Get(), XFA_CalcData, true);
  } else {
    CXFA_Node* pChild = pFakeRoot->GetNodeItem(XFA_NODEITEM_FirstChild);
    while (pChild) {
      CXFA_Node* pItem = pChild->GetNodeItem(XFA_NODEITEM_NextSibling);
      pFakeRoot->RemoveChild(pChild);
      node_->InsertChild(pChild);
      pChild->SetFlag(XFA_NodeFlag_Initialized, true);
      pChild = pItem;
    }
  }
  if (pFakeXMLRoot) {
    pFakeRoot->SetXMLMappingNode(pFakeXMLRoot.release());
    pFakeRoot->SetFlag(XFA_NodeFlag_OwnXMLNode, false);
  }
  pFakeRoot->SetFlag(XFA_NodeFlag_HasRemovedChildren, false);
}

void CJX_Node::Script_NodeClass_SaveFilteredXML(CFXJSE_Arguments* pArguments) {
  // TODO(weili): Check whether we need to implement this, pdfium:501.
}

void CJX_Node::Script_NodeClass_SaveXML(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 0 || iLength > 1) {
    node_->ThrowParamCountMismatchException(L"saveXML");
    return;
  }
  bool bPrettyMode = false;
  if (iLength == 1) {
    if (pArguments->GetUTF8String(0) != "pretty") {
      node_->ThrowArgumentMismatchException();
      return;
    }
    bPrettyMode = true;
  }
  WideString bsXMLHeader = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  if (node_->GetPacketID() == XFA_XDPPACKET_Form ||
      node_->GetPacketID() == XFA_XDPPACKET_Datasets) {
    CFX_XMLNode* pElement = nullptr;
    if (node_->GetPacketID() == XFA_XDPPACKET_Datasets) {
      pElement = node_->GetXMLMappingNode();
      if (!pElement || pElement->GetType() != FX_XMLNODE_Element) {
        pArguments->GetReturnValue()->SetString(
            bsXMLHeader.UTF8Encode().AsStringView());
        return;
      }
      XFA_DataExporter_DealWithDataGroupNode(node_.Get());
    }
    auto pMemoryStream = pdfium::MakeRetain<CFX_MemoryStream>(true);
    auto pStream =
        pdfium::MakeRetain<CFX_SeekableStreamProxy>(pMemoryStream, true);
    pStream->SetCodePage(FX_CODEPAGE_UTF8);
    pStream->WriteString(bsXMLHeader.AsStringView());

    if (node_->GetPacketID() == XFA_XDPPACKET_Form)
      XFA_DataExporter_RegenerateFormFile(node_.Get(), pStream, nullptr, true);
    else
      pElement->SaveXMLNode(pStream);
    // TODO(weili): Check whether we need to save pretty print XML, pdfium:501.
    // For now, just put it here to avoid unused variable warning.
    (void)bPrettyMode;
    pArguments->GetReturnValue()->SetString(
        ByteStringView(pMemoryStream->GetBuffer(), pMemoryStream->GetSize()));
    return;
  }
  pArguments->GetReturnValue()->SetString("");
}

void CJX_Node::Script_NodeClass_SetAttribute(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 2) {
    node_->ThrowParamCountMismatchException(L"setAttribute");
    return;
  }
  WideString wsAttributeValue =
      WideString::FromUTF8(pArguments->GetUTF8String(0).AsStringView());
  WideString wsAttribute =
      WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringView());
  SetAttribute(wsAttribute.AsStringView(), wsAttributeValue.AsStringView(),
               true);
}

void CJX_Node::Script_NodeClass_SetElement(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1 && iLength != 2) {
    node_->ThrowParamCountMismatchException(L"setElement");
    return;
  }
  CXFA_Node* pNode = nullptr;
  WideString wsName;
  pNode = static_cast<CXFA_Node*>(pArguments->GetObject(0));
  if (iLength == 2)
    wsName = WideString::FromUTF8(pArguments->GetUTF8String(1).AsStringView());
  // TODO(weili): check whether we need to implement this, pdfium:501.
  // For now, just put the variables here to avoid unused variable warning.
  (void)pNode;
  (void)wsName;
}

void CJX_Node::Script_NodeClass_Ns(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  WideString wsNameSpace;
  TryNamespace(wsNameSpace);
  pValue->SetString(wsNameSpace.UTF8Encode().AsStringView());
}

void CJX_Node::Script_NodeClass_Model(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->Assign(node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(
      node_->GetModelNode()));
}

void CJX_Node::Script_NodeClass_IsContainer(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetBoolean(node_->IsContainerNode());
}

void CJX_Node::Script_NodeClass_IsNull(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  if (node_->GetElementType() == XFA_Element::Subform) {
    pValue->SetBoolean(false);
    return;
  }
  WideString strValue;
  pValue->SetBoolean(!TryContent(strValue) || strValue.IsEmpty());
}

void CJX_Node::Script_NodeClass_OneOfChild(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  std::vector<CXFA_Node*> properties =
      node_->GetNodeList(XFA_NODEFILTER_OneOfProperty);
  if (!properties.empty()) {
    pValue->Assign(node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(
        properties.front()));
  }
}

void CJX_Node::Script_ContainerClass_GetDelta(CFXJSE_Arguments* pArguments) {}

void CJX_Node::Script_ContainerClass_GetDeltas(CFXJSE_Arguments* pArguments) {
  CXFA_ArrayNodeList* pFormNodes = new CXFA_ArrayNodeList(node_->GetDocument());
  pArguments->GetReturnValue()->SetObject(
      pFormNodes,
      node_->GetDocument()->GetScriptContext()->GetJseNormalClass());
}
void CJX_Node::Script_ModelClass_ClearErrorList(CFXJSE_Arguments* pArguments) {}

void CJX_Node::Script_ModelClass_CreateNode(CFXJSE_Arguments* pArguments) {
  Script_Template_CreateNode(pArguments);
}

void CJX_Node::Script_ModelClass_IsCompatibleNS(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1) {
    node_->ThrowParamCountMismatchException(L"isCompatibleNS");
    return;
  }
  WideString wsNameSpace;
  if (iLength >= 1) {
    ByteString bsNameSpace = pArguments->GetUTF8String(0);
    wsNameSpace = WideString::FromUTF8(bsNameSpace.AsStringView());
  }
  WideString wsNodeNameSpace;
  TryNamespace(wsNodeNameSpace);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(wsNodeNameSpace == wsNameSpace);
}

void CJX_Node::Script_ModelClass_Context(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_ModelClass_AliasNode(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Attribute_Integer(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetInteger(eAttribute, pValue->ToInteger(), true);
  } else {
    pValue->SetInteger(GetInteger(eAttribute));
  }
}

void CJX_Node::Script_Attribute_IntegerRead(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetInteger(GetInteger(eAttribute));
}

void CJX_Node::Script_Attribute_BOOL(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetBoolean(eAttribute, pValue->ToBoolean(), true);
  } else {
    pValue->SetString(GetBoolean(eAttribute) ? "1" : "0");
  }
}

void CJX_Node::Script_Attribute_BOOLRead(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetString(GetBoolean(eAttribute) ? "1" : "0");
}

void CJX_Node::Script_Attribute_SendAttributeChangeMessage(
    XFA_ATTRIBUTE eAttribute,
    bool bScriptModify) {
  CXFA_LayoutProcessor* pLayoutPro = node_->GetDocument()->GetLayoutProcessor();
  if (!pLayoutPro)
    return;

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  uint32_t dwPacket = node_->GetPacketID();
  if (!(dwPacket & XFA_XDPPACKET_Form)) {
    pNotify->OnValueChanged(node_.Get(), eAttribute, node_.Get(), node_.Get());
    return;
  }

  bool bNeedFindContainer = false;
  switch (node_->GetElementType()) {
    case XFA_Element::Caption:
      bNeedFindContainer = true;
      pNotify->OnValueChanged(node_.Get(), eAttribute, node_.Get(),
                              node_->GetNodeItem(XFA_NODEITEM_Parent));
      break;
    case XFA_Element::Font:
    case XFA_Element::Para: {
      bNeedFindContainer = true;
      CXFA_Node* pParentNode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      if (pParentNode->GetElementType() == XFA_Element::Caption) {
        pNotify->OnValueChanged(node_.Get(), eAttribute, pParentNode,
                                pParentNode->GetNodeItem(XFA_NODEITEM_Parent));
      } else {
        pNotify->OnValueChanged(node_.Get(), eAttribute, node_.Get(),
                                pParentNode);
      }
    } break;
    case XFA_Element::Margin: {
      bNeedFindContainer = true;
      CXFA_Node* pParentNode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      XFA_Element eParentType = pParentNode->GetElementType();
      if (pParentNode->IsContainerNode()) {
        pNotify->OnValueChanged(node_.Get(), eAttribute, node_.Get(),
                                pParentNode);
      } else if (eParentType == XFA_Element::Caption) {
        pNotify->OnValueChanged(node_.Get(), eAttribute, pParentNode,
                                pParentNode->GetNodeItem(XFA_NODEITEM_Parent));
      } else {
        CXFA_Node* pNode = pParentNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pNode && pNode->GetElementType() == XFA_Element::Ui) {
          pNotify->OnValueChanged(node_.Get(), eAttribute, pNode,
                                  pNode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      }
    } break;
    case XFA_Element::Comb: {
      CXFA_Node* pEditNode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      XFA_Element eUIType = pEditNode->GetElementType();
      if (pEditNode && (eUIType == XFA_Element::DateTimeEdit ||
                        eUIType == XFA_Element::NumericEdit ||
                        eUIType == XFA_Element::TextEdit)) {
        CXFA_Node* pUINode = pEditNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pUINode) {
          pNotify->OnValueChanged(node_.Get(), eAttribute, pUINode,
                                  pUINode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      }
    } break;
    case XFA_Element::Button:
    case XFA_Element::Barcode:
    case XFA_Element::ChoiceList:
    case XFA_Element::DateTimeEdit:
    case XFA_Element::NumericEdit:
    case XFA_Element::PasswordEdit:
    case XFA_Element::TextEdit: {
      CXFA_Node* pUINode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      if (pUINode) {
        pNotify->OnValueChanged(node_.Get(), eAttribute, pUINode,
                                pUINode->GetNodeItem(XFA_NODEITEM_Parent));
      }
    } break;
    case XFA_Element::CheckButton: {
      bNeedFindContainer = true;
      CXFA_Node* pUINode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      if (pUINode) {
        pNotify->OnValueChanged(node_.Get(), eAttribute, pUINode,
                                pUINode->GetNodeItem(XFA_NODEITEM_Parent));
      }
    } break;
    case XFA_Element::Keep:
    case XFA_Element::Bookend:
    case XFA_Element::Break:
    case XFA_Element::BreakAfter:
    case XFA_Element::BreakBefore:
    case XFA_Element::Overflow:
      bNeedFindContainer = true;
      break;
    case XFA_Element::Area:
    case XFA_Element::Draw:
    case XFA_Element::ExclGroup:
    case XFA_Element::Field:
    case XFA_Element::Subform:
    case XFA_Element::SubformSet:
      pLayoutPro->AddChangedContainer(node_.Get());
      pNotify->OnValueChanged(node_.Get(), eAttribute, node_.Get(),
                              node_.Get());
      break;
    case XFA_Element::Sharptext:
    case XFA_Element::Sharpxml:
    case XFA_Element::SharpxHTML: {
      CXFA_Node* pTextNode = node_->GetNodeItem(XFA_NODEITEM_Parent);
      if (!pTextNode) {
        return;
      }
      CXFA_Node* pValueNode = pTextNode->GetNodeItem(XFA_NODEITEM_Parent);
      if (!pValueNode) {
        return;
      }
      XFA_Element eType = pValueNode->GetElementType();
      if (eType == XFA_Element::Value) {
        bNeedFindContainer = true;
        CXFA_Node* pNode = pValueNode->GetNodeItem(XFA_NODEITEM_Parent);
        if (pNode && pNode->IsContainerNode()) {
          if (bScriptModify) {
            pValueNode = pNode;
          }
          pNotify->OnValueChanged(node_.Get(), eAttribute, pValueNode, pNode);
        } else {
          pNotify->OnValueChanged(node_.Get(), eAttribute, pNode,
                                  pNode->GetNodeItem(XFA_NODEITEM_Parent));
        }
      } else {
        if (eType == XFA_Element::Items) {
          CXFA_Node* pNode = pValueNode->GetNodeItem(XFA_NODEITEM_Parent);
          if (pNode && pNode->IsContainerNode()) {
            pNotify->OnValueChanged(node_.Get(), eAttribute, pValueNode, pNode);
          }
        }
      }
    } break;
    default:
      break;
  }
  if (bNeedFindContainer) {
    CXFA_Node* pParent = node_.Get();
    while (pParent) {
      if (pParent->IsContainerNode())
        break;

      pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
    }
    if (pParent) {
      pLayoutPro->AddChangedContainer(pParent);
    }
  }
}

void CJX_Node::Script_Attribute_String(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    WideString wsValue = pValue->ToWideString();
    SetAttribute(eAttribute, wsValue.AsStringView(), true);
    if (eAttribute == XFA_ATTRIBUTE_Use &&
        node_->GetElementType() == XFA_Element::Desc) {
      CXFA_Node* pTemplateNode =
          ToNode(node_->GetDocument()->GetXFAObject(XFA_HASHCODE_Template));
      CXFA_Node* pProtoRoot =
          pTemplateNode->GetFirstChildByClass(XFA_Element::Subform)
              ->GetFirstChildByClass(XFA_Element::Proto);

      WideString wsID;
      WideString wsSOM;
      if (!wsValue.IsEmpty()) {
        if (wsValue[0] == '#') {
          wsID = WideString(wsValue.c_str() + 1, wsValue.GetLength() - 1);
        } else {
          wsSOM = wsValue;
        }
      }
      CXFA_Node* pProtoNode = nullptr;
      if (!wsSOM.IsEmpty()) {
        uint32_t dwFlag = XFA_RESOLVENODE_Children |
                          XFA_RESOLVENODE_Attributes |
                          XFA_RESOLVENODE_Properties | XFA_RESOLVENODE_Parent |
                          XFA_RESOLVENODE_Siblings;
        XFA_RESOLVENODE_RS resoveNodeRS;
        int32_t iRet = node_->GetDocument()->GetScriptContext()->ResolveObjects(
            pProtoRoot, wsSOM.AsStringView(), resoveNodeRS, dwFlag);
        if (iRet > 0 && resoveNodeRS.objects.front()->IsNode()) {
          pProtoNode = resoveNodeRS.objects.front()->AsNode();
        }
      } else if (!wsID.IsEmpty()) {
        pProtoNode =
            node_->GetDocument()->GetNodeByID(pProtoRoot, wsID.AsStringView());
      }
      if (pProtoNode) {
        CXFA_Node* pHeadChild = node_->GetNodeItem(XFA_NODEITEM_FirstChild);
        while (pHeadChild) {
          CXFA_Node* pSibling =
              pHeadChild->GetNodeItem(XFA_NODEITEM_NextSibling);
          node_->RemoveChild(pHeadChild);
          pHeadChild = pSibling;
        }
        CXFA_Node* pProtoForm = pProtoNode->CloneTemplateToForm(true);
        pHeadChild = pProtoForm->GetNodeItem(XFA_NODEITEM_FirstChild);
        while (pHeadChild) {
          CXFA_Node* pSibling =
              pHeadChild->GetNodeItem(XFA_NODEITEM_NextSibling);
          pProtoForm->RemoveChild(pHeadChild);
          node_->InsertChild(pHeadChild);
          pHeadChild = pSibling;
        }
        node_->GetDocument()->RemovePurgeNode(pProtoForm);
        delete pProtoForm;
      }
    }
  } else {
    WideString wsValue;
    GetAttribute(eAttribute, wsValue);
    pValue->SetString(wsValue.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Attribute_StringRead(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  WideString wsValue;
  GetAttribute(eAttribute, wsValue);
  pValue->SetString(wsValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_WsdlConnection_Execute(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    node_->ThrowParamCountMismatchException(L"execute");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(false);
}

void CJX_Node::Script_Delta_Restore(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"restore");
}

void CJX_Node::Script_Delta_CurrentValue(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Delta_SavedValue(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Delta_Target(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Som_Message(CFXJSE_Value* pValue,
                                  bool bSetting,
                                  XFA_SOM_MESSAGETYPE iMessageType) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  bool bNew = false;
  CXFA_Validate validate = pWidgetData->GetValidate(false);
  if (!validate) {
    validate = pWidgetData->GetValidate(true);
    bNew = true;
  }
  if (bSetting) {
    switch (iMessageType) {
      case XFA_SOM_ValidationMessage:
        validate.SetScriptMessageText(pValue->ToWideString());
        break;
      case XFA_SOM_FormatMessage:
        validate.SetFormatMessageText(pValue->ToWideString());
        break;
      case XFA_SOM_MandatoryMessage:
        validate.SetNullMessageText(pValue->ToWideString());
        break;
      default:
        break;
    }
    if (!bNew) {
      CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
      if (!pNotify) {
        return;
      }
      pNotify->AddCalcValidate(node_.Get());
    }
  } else {
    WideString wsMessage;
    switch (iMessageType) {
      case XFA_SOM_ValidationMessage:
        validate.GetScriptMessageText(wsMessage);
        break;
      case XFA_SOM_FormatMessage:
        validate.GetFormatMessageText(wsMessage);
        break;
      case XFA_SOM_MandatoryMessage:
        validate.GetNullMessageText(wsMessage);
        break;
      default:
        break;
    }
    pValue->SetString(wsMessage.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_ValidationMessage(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_ValidationMessage);
}

void CJX_Node::Script_Field_Length(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pValue->SetInteger(0);
    return;
  }
  pValue->SetInteger(pWidgetData->CountChoiceListItems(true));
}

void CJX_Node::Script_Som_DefaultValue(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  XFA_Element eType = node_->GetElementType();
  if (eType == XFA_Element::Field) {
    Script_Field_DefaultValue(pValue, bSetting, eAttribute);
    return;
  }
  if (eType == XFA_Element::Draw) {
    Script_Draw_DefaultValue(pValue, bSetting, eAttribute);
    return;
  }
  if (eType == XFA_Element::Boolean) {
    Script_Boolean_Value(pValue, bSetting, eAttribute);
    return;
  }
  if (bSetting) {
    WideString wsNewValue;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      wsNewValue = pValue->ToWideString();

    WideString wsFormatValue(wsNewValue);
    CXFA_WidgetData* pContainerWidgetData = nullptr;
    if (node_->GetPacketID() == XFA_XDPPACKET_Datasets) {
      WideString wsPicture;
      for (CXFA_Node* pFormNode : node_->GetBindItems()) {
        if (!pFormNode || pFormNode->HasRemovedChildren())
          continue;
        pContainerWidgetData = pFormNode->GetContainerWidgetData();
        if (pContainerWidgetData) {
          pContainerWidgetData->GetPictureContent(wsPicture,
                                                  XFA_VALUEPICTURE_DataBind);
        }
        if (!wsPicture.IsEmpty())
          break;
        pContainerWidgetData = nullptr;
      }
    } else if (node_->GetPacketID() == XFA_XDPPACKET_Form) {
      pContainerWidgetData = node_->GetContainerWidgetData();
    }
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewValue, wsFormatValue);
    }
    SetScriptContent(wsNewValue, wsFormatValue, true, true);
  } else {
    WideString content = GetScriptContent(true);
    if (content.IsEmpty() && eType != XFA_Element::Text &&
        eType != XFA_Element::SubmitUrl) {
      pValue->SetNull();
    } else if (eType == XFA_Element::Integer) {
      pValue->SetInteger(FXSYS_wtoi(content.c_str()));
    } else if (eType == XFA_Element::Float || eType == XFA_Element::Decimal) {
      CFX_Decimal decimal(content.AsStringView());
      pValue->SetFloat((float)(double)decimal);
    } else {
      pValue->SetString(content.UTF8Encode().AsStringView());
    }
  }
}

void CJX_Node::Script_Som_DefaultValue_Read(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  WideString content = GetScriptContent(true);
  if (content.IsEmpty()) {
    pValue->SetNull();
    return;
  }
  pValue->SetString(content.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Boolean_Value(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    ByteString newValue;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      newValue = pValue->ToString();

    int32_t iValue = FXSYS_atoi(newValue.c_str());
    WideString wsNewValue(iValue == 0 ? L"0" : L"1");
    WideString wsFormatValue(wsNewValue);
    CXFA_WidgetData* pContainerWidgetData = node_->GetContainerWidgetData();
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewValue, wsFormatValue);
    }
    SetScriptContent(wsNewValue, wsFormatValue, true, true);
  } else {
    WideString wsValue = GetScriptContent(true);
    pValue->SetBoolean(wsValue == L"1");
  }
}

void CJX_Node::Script_Som_BorderColor(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  int32_t iSize = border.CountEdges();
  if (bSetting) {
    int32_t r = 0;
    int32_t g = 0;
    int32_t b = 0;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB rgb = ArgbEncode(100, r, g, b);
    for (int32_t i = 0; i < iSize; ++i) {
      CXFA_Edge edge = border.GetEdge(i);
      edge.SetColor(rgb);
    }
  } else {
    CXFA_Edge edge = border.GetEdge(0);
    FX_ARGB color = edge.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    WideString strColor;
    strColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(strColor.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_BorderWidth(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  int32_t iSize = border.CountEdges();
  WideString wsThickness;
  if (bSetting) {
    wsThickness = pValue->ToWideString();
    for (int32_t i = 0; i < iSize; ++i) {
      CXFA_Edge edge = border.GetEdge(i);
      CXFA_Measurement thickness(wsThickness.AsStringView());
      edge.SetMSThickness(thickness);
    }
  } else {
    CXFA_Edge edge = border.GetEdge(0);
    CXFA_Measurement thickness = edge.GetMSThickness();
    thickness.ToString(&wsThickness);
    pValue->SetString(wsThickness.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_FillColor(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Border border = pWidgetData->GetBorder(true);
  CXFA_Fill borderfill = border.GetFill(true);
  CXFA_Node* pNode = borderfill.GetNode();
  if (!pNode) {
    return;
  }
  if (bSetting) {
    int32_t r;
    int32_t g;
    int32_t b;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB color = ArgbEncode(0xff, r, g, b);
    borderfill.SetColor(color);
  } else {
    FX_ARGB color = borderfill.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    WideString wsColor;
    wsColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(wsColor.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_DataNode(CFXJSE_Value* pValue,
                                   bool bSetting,
                                   XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  CXFA_Node* pDataNode = node_->GetBindData();
  if (!pDataNode) {
    pValue->SetNull();
    return;
  }

  pValue->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pDataNode));
}

void CJX_Node::Script_Draw_DefaultValue(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    if (pValue && pValue->IsString()) {
      CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
      ASSERT(pWidgetData);
      XFA_Element uiType = pWidgetData->GetUIType();
      if (uiType == XFA_Element::Text) {
        WideString wsNewValue = pValue->ToWideString();
        WideString wsFormatValue(wsNewValue);
        SetScriptContent(wsNewValue, wsFormatValue, true, true);
      }
    }
  } else {
    WideString content = GetScriptContent(true);
    if (content.IsEmpty())
      pValue->SetNull();
    else
      pValue->SetString(content.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Field_DefaultValue(CFXJSE_Value* pValue,
                                         bool bSetting,
                                         XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    if (pValue && pValue->IsNull()) {
      pWidgetData->m_bPreNull = pWidgetData->m_bIsNull;
      pWidgetData->m_bIsNull = true;
    } else {
      pWidgetData->m_bPreNull = pWidgetData->m_bIsNull;
      pWidgetData->m_bIsNull = false;
    }
    WideString wsNewText;
    if (!(pValue && (pValue->IsNull() || pValue->IsUndefined())))
      wsNewText = pValue->ToWideString();

    CXFA_Node* pUIChild = pWidgetData->GetUIChild();
    if (pUIChild->GetElementType() == XFA_Element::NumericEdit) {
      int32_t iLeadDigits = 0;
      int32_t iFracDigits = 0;
      pWidgetData->GetLeadDigits(iLeadDigits);
      pWidgetData->GetFracDigits(iFracDigits);
      wsNewText =
          pWidgetData->NumericLimit(wsNewText, iLeadDigits, iFracDigits);
    }
    CXFA_WidgetData* pContainerWidgetData = node_->GetContainerWidgetData();
    WideString wsFormatText(wsNewText);
    if (pContainerWidgetData) {
      pContainerWidgetData->GetFormatDataValue(wsNewText, wsFormatText);
    }
    SetScriptContent(wsNewText, wsFormatText, true, true);
  } else {
    WideString content = GetScriptContent(true);
    if (content.IsEmpty()) {
      pValue->SetNull();
    } else {
      CXFA_Node* pUIChild = pWidgetData->GetUIChild();
      CXFA_Value defVal = pWidgetData->GetFormValue();
      CXFA_Node* pNode = defVal.GetNode()->GetNodeItem(XFA_NODEITEM_FirstChild);
      if (pNode && pNode->GetElementType() == XFA_Element::Decimal) {
        if (pUIChild->GetElementType() == XFA_Element::NumericEdit &&
            (pNode->JSNode()->GetInteger(XFA_ATTRIBUTE_FracDigits) == -1)) {
          pValue->SetString(content.UTF8Encode().AsStringView());
        } else {
          CFX_Decimal decimal(content.AsStringView());
          pValue->SetFloat((float)(double)decimal);
        }
      } else if (pNode && pNode->GetElementType() == XFA_Element::Integer) {
        pValue->SetInteger(FXSYS_wtoi(content.c_str()));
      } else if (pNode && pNode->GetElementType() == XFA_Element::Boolean) {
        pValue->SetBoolean(FXSYS_wtoi(content.c_str()) == 0 ? false : true);
      } else if (pNode && pNode->GetElementType() == XFA_Element::Float) {
        CFX_Decimal decimal(content.AsStringView());
        pValue->SetFloat((float)(double)decimal);
      } else {
        pValue->SetString(content.UTF8Encode().AsStringView());
      }
    }
  }
}

void CJX_Node::Script_Field_EditValue(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetValue(pValue->ToWideString(), XFA_VALUEPICTURE_Edit);
  } else {
    WideString wsValue;
    pWidgetData->GetValue(wsValue, XFA_VALUEPICTURE_Edit);
    pValue->SetString(wsValue.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_FontColor(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Font font = pWidgetData->GetFont(true);
  CXFA_Node* pNode = font.GetNode();
  if (!pNode) {
    return;
  }
  if (bSetting) {
    int32_t r;
    int32_t g;
    int32_t b;
    StrToRGB(pValue->ToWideString(), r, g, b);
    FX_ARGB color = ArgbEncode(0xff, r, g, b);
    font.SetColor(color);
  } else {
    FX_ARGB color = font.GetColor();
    int32_t a;
    int32_t r;
    int32_t g;
    int32_t b;
    std::tie(a, r, g, b) = ArgbDecode(color);
    WideString wsColor;
    wsColor.Format(L"%d,%d,%d", r, g, b);
    pValue->SetString(wsColor.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Field_FormatMessage(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_FormatMessage);
}

void CJX_Node::Script_Field_FormattedValue(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetValue(pValue->ToWideString(), XFA_VALUEPICTURE_Display);
  } else {
    WideString wsValue;
    pWidgetData->GetValue(wsValue, XFA_VALUEPICTURE_Display);
    pValue->SetString(wsValue.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_Mandatory(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  CXFA_Validate validate = pWidgetData->GetValidate(true);
  if (bSetting) {
    validate.SetNullTest(pValue->ToWideString());
  } else {
    int32_t iValue = validate.GetNullTest();
    const XFA_ATTRIBUTEENUMINFO* pInfo =
        GetAttributeEnumByID((XFA_ATTRIBUTEENUM)iValue);
    WideString wsValue;
    if (pInfo)
      wsValue = pInfo->pName;
    pValue->SetString(wsValue.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Som_MandatoryMessage(CFXJSE_Value* pValue,
                                           bool bSetting,
                                           XFA_ATTRIBUTE eAttribute) {
  Script_Som_Message(pValue, bSetting, XFA_SOM_MandatoryMessage);
}

void CJX_Node::Script_Field_ParentSubform(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetNull();
}

void CJX_Node::Script_Field_SelectedIndex(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    int32_t iIndex = pValue->ToInteger();
    if (iIndex == -1) {
      pWidgetData->ClearAllSelections();
      return;
    }
    pWidgetData->SetItemState(iIndex, true, true, true, true);
  } else {
    pValue->SetInteger(pWidgetData->GetSelectedItem(0));
  }
}

void CJX_Node::Script_Field_ClearItems(CFXJSE_Arguments* pArguments) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  pWidgetData->DeleteItem(-1, true, false);
}

void CJX_Node::Script_Field_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  ByteString eventString = pArguments->GetUTF8String(0);
  int32_t iRet = execSingleEventByName(
      WideString::FromUTF8(eventString.AsStringView()).AsStringView(),
      XFA_Element::Field);
  if (eventString != "validate")
    return;

  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CJX_Node::Script_Field_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Initialize, false,
                                false);
}

void CJX_Node::Script_Field_DeleteItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"deleteItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bValue = pWidgetData->DeleteItem(iIndex, true, true);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(bValue);
}

void CJX_Node::Script_Field_GetSaveItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"getSaveItem");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  if (iIndex < 0) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  WideString wsValue;
  if (!pWidgetData->GetChoiceListItem(wsValue, iIndex, true)) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->SetString(wsValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Field_BoundItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"boundItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  ByteString bsValue = pArguments->GetUTF8String(0);
  WideString wsValue = WideString::FromUTF8(bsValue.AsStringView());
  WideString wsBoundValue;
  pWidgetData->GetItemValue(wsValue.AsStringView(), wsBoundValue);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetString(wsBoundValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Field_GetItemState(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"getItemState");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bValue = pWidgetData->GetItemState(iIndex);
  CFXJSE_Value* pValue = pArguments->GetReturnValue();
  if (pValue)
    pValue->SetBoolean(bValue);
}

void CJX_Node::Script_Field_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Calculate, false, false);
}

void CJX_Node::Script_Field_SetItems(CFXJSE_Arguments* pArguments) {}

void CJX_Node::Script_Field_GetDisplayItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 1) {
    node_->ThrowParamCountMismatchException(L"getDisplayItem");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  if (iIndex < 0) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  WideString wsValue;
  if (!pWidgetData->GetChoiceListItem(wsValue, iIndex, false)) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->SetString(wsValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Field_SetItemState(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength != 2) {
    node_->ThrowParamCountMismatchException(L"setItemState");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData)
    return;

  int32_t iIndex = pArguments->GetInt32(0);
  if (pArguments->GetInt32(1) != 0) {
    pWidgetData->SetItemState(iIndex, true, true, true, true);
  } else {
    if (pWidgetData->GetItemState(iIndex))
      pWidgetData->SetItemState(iIndex, false, true, true, true);
  }
}

void CJX_Node::Script_Field_AddItem(CFXJSE_Arguments* pArguments) {
  int32_t iLength = pArguments->GetLength();
  if (iLength < 1 || iLength > 2) {
    node_->ThrowParamCountMismatchException(L"addItem");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  WideString wsLabel;
  WideString wsValue;
  if (iLength >= 1) {
    ByteString bsLabel = pArguments->GetUTF8String(0);
    wsLabel = WideString::FromUTF8(bsLabel.AsStringView());
  }
  if (iLength >= 2) {
    ByteString bsValue = pArguments->GetUTF8String(1);
    wsValue = WideString::FromUTF8(bsValue.AsStringView());
  }
  pWidgetData->InsertItem(wsLabel, wsValue, true);
}

void CJX_Node::Script_Field_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Validate,
                                               false, false);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CJX_Node::Script_ExclGroup_ErrorText(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting)
    node_->ThrowInvalidPropertyException();
}

void CJX_Node::Script_ExclGroup_DefaultAndRawValue(CFXJSE_Value* pValue,
                                                   bool bSetting,
                                                   XFA_ATTRIBUTE eAttribute) {
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    return;
  }
  if (bSetting) {
    pWidgetData->SetSelectedMemberByValue(pValue->ToWideString().AsStringView(),
                                          true, true, true);
  } else {
    WideString wsValue = GetScriptContent(true);
    XFA_VERSION curVersion = node_->GetDocument()->GetCurVersionMode();
    if (wsValue.IsEmpty() && curVersion >= XFA_VERSION_300) {
      pValue->SetNull();
    } else {
      pValue->SetString(wsValue.UTF8Encode().AsStringView());
    }
  }
}

void CJX_Node::Script_ExclGroup_Transient(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_ExclGroup_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  ByteString eventString = pArguments->GetUTF8String(0);
  execSingleEventByName(
      WideString::FromUTF8(eventString.AsStringView()).AsStringView(),
      XFA_Element::ExclGroup);
}

void CJX_Node::Script_ExclGroup_SelectedMember(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc < 0 || argc > 1) {
    node_->ThrowParamCountMismatchException(L"selectedMember");
    return;
  }

  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }

  CXFA_Node* pReturnNode = nullptr;
  if (argc == 0) {
    pReturnNode = pWidgetData->GetSelectedMember();
  } else {
    ByteString szName;
    szName = pArguments->GetUTF8String(0);
    pReturnNode = pWidgetData->SetSelectedMember(
        WideString::FromUTF8(szName.AsStringView()).AsStringView(), true);
  }
  if (!pReturnNode) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }
  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pReturnNode));
}

void CJX_Node::Script_ExclGroup_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Initialize);
}

void CJX_Node::Script_ExclGroup_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Calculate);
}

void CJX_Node::Script_ExclGroup_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CJX_Node::Script_Som_InstanceIndex(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    int32_t iTo = pValue->ToInteger();
    int32_t iFrom = node_->Subform_and_SubformSet_InstanceIndex();
    CXFA_Node* pManagerNode = nullptr;
    for (CXFA_Node* pNode = node_->GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
         pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
      if (pNode->GetElementType() == XFA_Element::InstanceManager) {
        pManagerNode = pNode;
        break;
      }
    }
    if (pManagerNode) {
      pManagerNode->InstanceManager_MoveInstance(iTo, iFrom);
      CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
      if (!pNotify) {
        return;
      }
      CXFA_Node* pToInstance = node_->GetItem(pManagerNode, iTo);
      if (pToInstance &&
          pToInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pToInstance);
      }
      CXFA_Node* pFromInstance = node_->GetItem(pManagerNode, iFrom);
      if (pFromInstance &&
          pFromInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pFromInstance);
      }
    }
  } else {
    pValue->SetInteger(node_->Subform_and_SubformSet_InstanceIndex());
  }
}

void CJX_Node::Script_Subform_InstanceManager(CFXJSE_Value* pValue,
                                              bool bSetting,
                                              XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }

  WideStringView wsName = GetCData(XFA_ATTRIBUTE_Name);
  CXFA_Node* pInstanceMgr = nullptr;
  for (CXFA_Node* pNode = node_->GetNodeItem(XFA_NODEITEM_PrevSibling); pNode;
       pNode = pNode->GetNodeItem(XFA_NODEITEM_PrevSibling)) {
    if (pNode->GetElementType() == XFA_Element::InstanceManager) {
      WideStringView wsInstMgrName =
          pNode->JSNode()->GetCData(XFA_ATTRIBUTE_Name);
      if (wsInstMgrName.GetLength() >= 1 && wsInstMgrName[0] == '_' &&
          wsInstMgrName.Right(wsInstMgrName.GetLength() - 1) == wsName) {
        pInstanceMgr = pNode;
      }
      break;
    }
  }
  if (!pInstanceMgr) {
    pValue->SetNull();
    return;
  }

  pValue->Assign(node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(
      pInstanceMgr));
}

void CJX_Node::Script_Subform_Locale(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetCData(XFA_ATTRIBUTE_Locale, pValue->ToWideString(), true, true);
  } else {
    WideString wsLocaleName;
    node_->GetLocaleName(wsLocaleName);
    pValue->SetString(wsLocaleName.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Subform_ExecEvent(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"execEvent");
    return;
  }

  ByteString eventString = pArguments->GetUTF8String(0);
  execSingleEventByName(
      WideString::FromUTF8(eventString.AsStringView()).AsStringView(),
      XFA_Element::Subform);
}

void CJX_Node::Script_Subform_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Initialize);
}

void CJX_Node::Script_Subform_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Calculate);
}

void CJX_Node::Script_Subform_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CJX_Node::Script_Subform_GetInvalidObjects(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"getInvalidObjects");
}

void CJX_Node::Script_Template_FormNodes(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"formNodes");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_Template_Remerge(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"remerge");
    return;
  }
  node_->GetDocument()->DoDataRemerge(true);
}

void CJX_Node::Script_Template_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_Template_CreateNode(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc <= 0 || argc >= 4) {
    node_->ThrowParamCountMismatchException(L"createNode");
    return;
  }

  WideString strName;
  WideString strNameSpace;
  ByteString bsTagName = pArguments->GetUTF8String(0);
  WideString strTagName = WideString::FromUTF8(bsTagName.AsStringView());
  if (argc > 1) {
    ByteString bsName = pArguments->GetUTF8String(1);
    strName = WideString::FromUTF8(bsName.AsStringView());
    if (argc == 3) {
      ByteString bsNameSpace = pArguments->GetUTF8String(2);
      strNameSpace = WideString::FromUTF8(bsNameSpace.AsStringView());
    }
  }

  XFA_Element eType = XFA_GetElementTypeForName(strTagName.AsStringView());
  CXFA_Node* pNewNode = node_->CreateSamePacketNode(eType);
  if (!pNewNode) {
    pArguments->GetReturnValue()->SetNull();
    return;
  }

  if (strName.IsEmpty()) {
    pArguments->GetReturnValue()->Assign(
        node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pNewNode));
    return;
  }

  if (!GetAttributeOfElement(eType, XFA_ATTRIBUTE_Name,
                             XFA_XDPPACKET_UNKNOWN)) {
    node_->ThrowMissingPropertyException(strTagName, L"name");
    return;
  }

  pNewNode->JSNode()->SetAttribute(XFA_ATTRIBUTE_Name, strName.AsStringView(),
                                   true);
  if (pNewNode->GetPacketID() == XFA_XDPPACKET_Datasets)
    pNewNode->CreateXMLMappingNode();

  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pNewNode));
}

void CJX_Node::Script_Template_Recalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"recalculate");
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_Template_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_Template_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execValidate");
    return;
  }
  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_Manifest_Evaluate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"evaluate");
    return;
  }

  CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
  if (!pWidgetData) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }
  pArguments->GetReturnValue()->SetBoolean(true);
}

void CJX_Node::Script_InstanceManager_Max(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  CXFA_Occur nodeOccur(node_->GetOccurNode());
  pValue->SetInteger(nodeOccur.GetMax());
}

void CJX_Node::Script_InstanceManager_Min(CFXJSE_Value* pValue,
                                          bool bSetting,
                                          XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  CXFA_Occur nodeOccur(node_->GetOccurNode());
  pValue->SetInteger(nodeOccur.GetMin());
}

void CJX_Node::Script_InstanceManager_Count(CFXJSE_Value* pValue,
                                            bool bSetting,
                                            XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    int32_t iDesired = pValue->ToInteger();
    node_->InstanceManager_SetInstances(iDesired);
  } else {
    pValue->SetInteger(node_->GetCount(node_.Get()));
  }
}

void CJX_Node::Script_InstanceManager_MoveInstance(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 2) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iFrom = pArguments->GetInt32(0);
  int32_t iTo = pArguments->GetInt32(1);
  node_->InstanceManager_MoveInstance(iTo, iFrom);
  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    return;
  }
  CXFA_Node* pToInstance = node_->GetItem(node_.Get(), iTo);
  if (pToInstance && pToInstance->GetElementType() == XFA_Element::Subform) {
    pNotify->RunSubformIndexChange(pToInstance);
  }
  CXFA_Node* pFromInstance = node_->GetItem(node_.Get(), iFrom);
  if (pFromInstance &&
      pFromInstance->GetElementType() == XFA_Element::Subform) {
    pNotify->RunSubformIndexChange(pFromInstance);
  }
}

void CJX_Node::Script_InstanceManager_RemoveInstance(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  int32_t iCount = node_->GetCount(node_.Get());
  if (iIndex < 0 || iIndex >= iCount) {
    node_->ThrowIndexOutOfBoundsException();
    return;
  }
  CXFA_Occur nodeOccur(node_->GetOccurNode());
  int32_t iMin = nodeOccur.GetMin();
  if (iCount - 1 < iMin) {
    node_->ThrowTooManyOccurancesException(L"min");
    return;
  }
  CXFA_Node* pRemoveInstance = node_->GetItem(node_.Get(), iIndex);
  node_->RemoveItem(node_.Get(), pRemoveInstance);
  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (pNotify) {
    for (int32_t i = iIndex; i < iCount - 1; i++) {
      CXFA_Node* pSubformInstance = node_->GetItem(node_.Get(), i);
      if (pSubformInstance &&
          pSubformInstance->GetElementType() == XFA_Element::Subform) {
        pNotify->RunSubformIndexChange(pSubformInstance);
      }
    }
  }
  CXFA_LayoutProcessor* pLayoutPro = node_->GetDocument()->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(node_->GetDocument()->GetXFAObject(XFA_HASHCODE_Form)));
}

void CJX_Node::Script_InstanceManager_SetInstances(
    CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    pArguments->GetReturnValue()->SetUndefined();
    return;
  }
  int32_t iDesired = pArguments->GetInt32(0);
  node_->InstanceManager_SetInstances(iDesired);
}

void CJX_Node::Script_InstanceManager_AddInstance(
    CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    node_->ThrowParamCountMismatchException(L"addInstance");
    return;
  }
  bool fFlags = true;
  if (argc == 1) {
    fFlags = pArguments->GetInt32(0) == 0 ? false : true;
  }
  int32_t iCount = node_->GetCount(node_.Get());
  CXFA_Occur nodeOccur(node_->GetOccurNode());
  int32_t iMax = nodeOccur.GetMax();
  if (iMax >= 0 && iCount >= iMax) {
    node_->ThrowTooManyOccurancesException(L"max");
    return;
  }
  CXFA_Node* pNewInstance = node_->CreateInstance(node_.Get(), fFlags);
  node_->InsertItem(node_.Get(), pNewInstance, iCount, iCount, false);
  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(
          pNewInstance));
  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    return;
  }
  pNotify->RunNodeInitialize(pNewInstance);
  CXFA_LayoutProcessor* pLayoutPro = node_->GetDocument()->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(node_->GetDocument()->GetXFAObject(XFA_HASHCODE_Form)));
}

void CJX_Node::Script_InstanceManager_InsertInstance(
    CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 1 && argc != 2) {
    node_->ThrowParamCountMismatchException(L"insertInstance");
    return;
  }
  int32_t iIndex = pArguments->GetInt32(0);
  bool bBind = false;
  if (argc == 2) {
    bBind = pArguments->GetInt32(1) == 0 ? false : true;
  }
  CXFA_Occur nodeOccur(node_->GetOccurNode());
  int32_t iCount = node_->GetCount(node_.Get());
  if (iIndex < 0 || iIndex > iCount) {
    node_->ThrowIndexOutOfBoundsException();
    return;
  }
  int32_t iMax = nodeOccur.GetMax();
  if (iMax >= 0 && iCount >= iMax) {
    node_->ThrowTooManyOccurancesException(L"max");
    return;
  }
  CXFA_Node* pNewInstance = node_->CreateInstance(node_.Get(), bBind);
  node_->InsertItem(node_.Get(), pNewInstance, iIndex, iCount, true);
  pArguments->GetReturnValue()->Assign(
      node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(
          pNewInstance));
  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    return;
  }
  pNotify->RunNodeInitialize(pNewInstance);
  CXFA_LayoutProcessor* pLayoutPro = node_->GetDocument()->GetLayoutProcessor();
  if (!pLayoutPro) {
    return;
  }
  pLayoutPro->AddChangedContainer(
      ToNode(node_->GetDocument()->GetXFAObject(XFA_HASHCODE_Form)));
}
void CJX_Node::Script_Occur_Max(CFXJSE_Value* pValue,
                                bool bSetting,
                                XFA_ATTRIBUTE eAttribute) {
  CXFA_Occur occur(node_.Get());
  if (bSetting) {
    int32_t iMax = pValue->ToInteger();
    occur.SetMax(iMax);
  } else {
    pValue->SetInteger(occur.GetMax());
  }
}

void CJX_Node::Script_Occur_Min(CFXJSE_Value* pValue,
                                bool bSetting,
                                XFA_ATTRIBUTE eAttribute) {
  CXFA_Occur occur(node_.Get());
  if (bSetting) {
    int32_t iMin = pValue->ToInteger();
    occur.SetMin(iMin);
  } else {
    pValue->SetInteger(occur.GetMin());
  }
}

void CJX_Node::Script_Desc_Metadata(CFXJSE_Arguments* pArguments) {
  int32_t argc = pArguments->GetLength();
  if (argc != 0 && argc != 1) {
    node_->ThrowParamCountMismatchException(L"metadata");
    return;
  }
  pArguments->GetReturnValue()->SetString("");
}

void CJX_Node::Script_Form_FormNodes(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"formNodes");
    return;
  }

  CXFA_Node* pDataNode = static_cast<CXFA_Node*>(pArguments->GetObject(0));
  if (!pDataNode) {
    node_->ThrowArgumentMismatchException();
    return;
  }

  std::vector<CXFA_Node*> formItems;
  CXFA_ArrayNodeList* pFormNodes = new CXFA_ArrayNodeList(node_->GetDocument());
  pFormNodes->SetArrayNodeList(formItems);
  pArguments->GetReturnValue()->SetObject(
      pFormNodes,
      node_->GetDocument()->GetScriptContext()->GetJseNormalClass());
}

void CJX_Node::Script_Form_Remerge(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"remerge");
    return;
  }

  node_->GetDocument()->DoDataRemerge(true);
}

void CJX_Node::Script_Form_ExecInitialize(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execInitialize");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Initialize);
}

void CJX_Node::Script_Form_Recalculate(CFXJSE_Arguments* pArguments) {
  CXFA_EventParam* pEventParam =
      node_->GetDocument()->GetScriptContext()->GetEventParam();
  if (pEventParam->m_eType == XFA_EVENT_Calculate ||
      pEventParam->m_eType == XFA_EVENT_InitCalculate) {
    return;
  }
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"recalculate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;
  if (pArguments->GetInt32(0) != 0)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Calculate);
  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Validate);
  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Ready, true);
}

void CJX_Node::Script_Form_ExecCalculate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execCalculate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify)
    return;

  pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Calculate);
}

void CJX_Node::Script_Form_ExecValidate(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0) {
    node_->ThrowParamCountMismatchException(L"execValidate");
    return;
  }

  CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
  if (!pNotify) {
    pArguments->GetReturnValue()->SetBoolean(false);
    return;
  }

  int32_t iRet = pNotify->ExecEventByDeepFirst(node_.Get(), XFA_EVENT_Validate);
  pArguments->GetReturnValue()->SetBoolean(
      (iRet == XFA_EVENTERROR_Error) ? false : true);
}

void CJX_Node::Script_Form_Checksum(CFXJSE_Value* pValue,
                                    bool bSetting,
                                    XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    SetAttribute(XFA_ATTRIBUTE_Checksum, pValue->ToWideString().AsStringView());
    return;
  }
  WideString wsChecksum;
  GetAttribute(XFA_ATTRIBUTE_Checksum, wsChecksum, false);
  pValue->SetString(wsChecksum.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Packet_GetAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"getAttribute");
    return;
  }
  ByteString bsAttributeName = pArguments->GetUTF8String(0);
  WideString wsAttributeValue;
  CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    wsAttributeValue = static_cast<CFX_XMLElement*>(pXMLNode)->GetString(
        WideString::FromUTF8(bsAttributeName.AsStringView()).c_str());
  }
  pArguments->GetReturnValue()->SetString(
      wsAttributeValue.UTF8Encode().AsStringView());
}

void CJX_Node::Script_Packet_SetAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 2) {
    node_->ThrowParamCountMismatchException(L"setAttribute");
    return;
  }
  ByteString bsValue = pArguments->GetUTF8String(0);
  ByteString bsName = pArguments->GetUTF8String(1);
  CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    static_cast<CFX_XMLElement*>(pXMLNode)->SetString(
        WideString::FromUTF8(bsName.AsStringView()),
        WideString::FromUTF8(bsValue.AsStringView()));
  }
  pArguments->GetReturnValue()->SetNull();
}

void CJX_Node::Script_Packet_RemoveAttribute(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 1) {
    node_->ThrowParamCountMismatchException(L"removeAttribute");
    return;
  }

  ByteString bsName = pArguments->GetUTF8String(0);
  WideString wsName = WideString::FromUTF8(bsName.AsStringView());
  CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
  if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
    CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
    if (pXMLElement->HasAttribute(wsName.c_str())) {
      pXMLElement->RemoveAttribute(wsName.c_str());
    }
  }
  pArguments->GetReturnValue()->SetNull();
}

void CJX_Node::Script_Packet_Content(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
    if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
      CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
      pXMLElement->SetTextData(pValue->ToWideString());
    }
  } else {
    WideString wsTextData;
    CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
    if (pXMLNode && pXMLNode->GetType() == FX_XMLNODE_Element) {
      CFX_XMLElement* pXMLElement = static_cast<CFX_XMLElement*>(pXMLNode);
      wsTextData = pXMLElement->GetTextData();
    }
    pValue->SetString(wsTextData.UTF8Encode().AsStringView());
  }
}

void CJX_Node::Script_Source_Next(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"next");
}

void CJX_Node::Script_Source_CancelBatch(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"cancelBatch");
}

void CJX_Node::Script_Source_First(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"first");
}

void CJX_Node::Script_Source_UpdateBatch(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"updateBatch");
}

void CJX_Node::Script_Source_Previous(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"previous");
}

void CJX_Node::Script_Source_IsBOF(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"isBOF");
}

void CJX_Node::Script_Source_IsEOF(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"isEOF");
}

void CJX_Node::Script_Source_Cancel(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"cancel");
}

void CJX_Node::Script_Source_Update(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"update");
}

void CJX_Node::Script_Source_Open(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"open");
}

void CJX_Node::Script_Source_Delete(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"delete");
}

void CJX_Node::Script_Source_AddNew(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"addNew");
}

void CJX_Node::Script_Source_Requery(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"requery");
}

void CJX_Node::Script_Source_Resync(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"resync");
}

void CJX_Node::Script_Source_Close(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"close");
}

void CJX_Node::Script_Source_Last(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"last");
}

void CJX_Node::Script_Source_HasDataChanged(CFXJSE_Arguments* pArguments) {
  if (pArguments->GetLength() != 0)
    node_->ThrowParamCountMismatchException(L"hasDataChanged");
}

void CJX_Node::Script_Source_Db(CFXJSE_Value* pValue,
                                bool bSetting,
                                XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Xfa_This(CFXJSE_Value* pValue,
                               bool bSetting,
                               XFA_ATTRIBUTE eAttribute) {
  if (!bSetting) {
    CXFA_Object* pThis =
        node_->GetDocument()->GetScriptContext()->GetThisObject();
    ASSERT(pThis);
    pValue->Assign(
        node_->GetDocument()->GetScriptContext()->GetJSValueFromMap(pThis));
  }
}

void CJX_Node::Script_Handler_Version(CFXJSE_Value* pValue,
                                      bool bSetting,
                                      XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_SubmitFormat_Mode(CFXJSE_Value* pValue,
                                        bool bSetting,
                                        XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Extras_Type(CFXJSE_Value* pValue,
                                  bool bSetting,
                                  XFA_ATTRIBUTE eAttribute) {}

void CJX_Node::Script_Script_Stateless(CFXJSE_Value* pValue,
                                       bool bSetting,
                                       XFA_ATTRIBUTE eAttribute) {
  if (bSetting) {
    node_->ThrowInvalidPropertyException();
    return;
  }
  pValue->SetString(FX_UTF8Encode(WideStringView(L"0", 1)).AsStringView());
}

void CJX_Node::Script_Encrypt_Format(CFXJSE_Value* pValue,
                                     bool bSetting,
                                     XFA_ATTRIBUTE eAttribute) {}

bool CJX_Node::TryBoolean(XFA_ATTRIBUTE eAttr, bool& bValue, bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Boolean, bUseDefault, pValue))
    return false;
  bValue = !!pValue;
  return true;
}

bool CJX_Node::TryInteger(XFA_ATTRIBUTE eAttr,
                          int32_t& iValue,
                          bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Integer, bUseDefault, pValue))
    return false;
  iValue = (int32_t)(uintptr_t)pValue;
  return true;
}

bool CJX_Node::TryEnum(XFA_ATTRIBUTE eAttr,
                       XFA_ATTRIBUTEENUM& eValue,
                       bool bUseDefault) {
  void* pValue = nullptr;
  if (!GetValue(eAttr, XFA_ATTRIBUTETYPE_Enum, bUseDefault, pValue))
    return false;
  eValue = (XFA_ATTRIBUTEENUM)(uintptr_t)pValue;
  return true;
}

bool CJX_Node::SetMeasure(XFA_ATTRIBUTE eAttr,
                          CXFA_Measurement mValue,
                          bool bNotify) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  node_->OnChanging(eAttr, bNotify);
  SetMapModuleBuffer(pKey, &mValue, sizeof(CXFA_Measurement));
  node_->OnChanged(eAttr, bNotify, false);
  return true;
}

bool CJX_Node::TryMeasure(XFA_ATTRIBUTE eAttr,
                          CXFA_Measurement& mValue,
                          bool bUseDefault) const {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  void* pValue;
  int32_t iBytes;
  if (GetMapModuleBuffer(pKey, pValue, iBytes) && iBytes == sizeof(mValue)) {
    memcpy(&mValue, pValue, sizeof(mValue));
    return true;
  }
  if (bUseDefault && XFA_GetAttributeDefaultValue(
                         pValue, node_->GetElementType(), eAttr,
                         XFA_ATTRIBUTETYPE_Measure, node_->GetPacketID())) {
    memcpy(&mValue, pValue, sizeof(mValue));
    return true;
  }
  return false;
}

CXFA_Measurement CJX_Node::GetMeasure(XFA_ATTRIBUTE eAttr) const {
  CXFA_Measurement mValue;
  return TryMeasure(eAttr, mValue, true) ? mValue : CXFA_Measurement();
}

bool CJX_Node::SetCData(XFA_ATTRIBUTE eAttr,
                        const WideString& wsValue,
                        bool bNotify,
                        bool bScriptModify) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  node_->OnChanging(eAttr, bNotify);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    WideString* pClone = new WideString(wsValue);
    SetUserData(pKey, pClone, &deleteWideStringCallBack);
  } else {
    SetMapModuleString(pKey, wsValue.AsStringView());
    if (eAttr == XFA_ATTRIBUTE_Name)
      node_->UpdateNameHash();
  }
  node_->OnChanged(eAttr, bNotify, bScriptModify);

  if (!node_->IsNeedSavingXMLNode() || eAttr == XFA_ATTRIBUTE_QualifiedName ||
      eAttr == XFA_ATTRIBUTE_BindingNode) {
    return true;
  }

  if (eAttr == XFA_ATTRIBUTE_Name &&
      (node_->GetElementType() == XFA_Element::DataValue ||
       node_->GetElementType() == XFA_Element::DataGroup)) {
    return true;
  }

  if (eAttr == XFA_ATTRIBUTE_Value) {
    FX_XMLNODETYPE eXMLType = node_->GetXMLMappingNode()->GetType();
    switch (eXMLType) {
      case FX_XMLNODE_Element:
        if (node_->IsAttributeInXML()) {
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetString(WideString(GetCData(XFA_ATTRIBUTE_QualifiedName)),
                          wsValue);
        } else {
          bool bDeleteChildren = true;
          if (node_->GetPacketID() == XFA_XDPPACKET_Datasets) {
            for (CXFA_Node* pChildDataNode =
                     node_->GetNodeItem(XFA_NODEITEM_FirstChild);
                 pChildDataNode; pChildDataNode = pChildDataNode->GetNodeItem(
                                     XFA_NODEITEM_NextSibling)) {
              if (!pChildDataNode->GetBindItems().empty()) {
                bDeleteChildren = false;
                break;
              }
            }
          }
          if (bDeleteChildren) {
            static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
                ->DeleteChildren();
          }
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetTextData(wsValue);
        }
        break;
      case FX_XMLNODE_Text:
        static_cast<CFX_XMLText*>(node_->GetXMLMappingNode())->SetText(wsValue);
        break;
      default:
        ASSERT(0);
    }
    return true;
  }

  const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttr);
  if (pInfo) {
    ASSERT(node_->GetXMLMappingNode()->GetType() == FX_XMLNODE_Element);
    WideString wsAttrName = pInfo->pName;
    if (pInfo->eName == XFA_ATTRIBUTE_ContentType) {
      wsAttrName = L"xfa:" + wsAttrName;
    }
    static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
        ->SetString(wsAttrName, wsValue);
  }
  return true;
}

bool CJX_Node::SetAttributeValue(const WideString& wsValue,
                                 const WideString& wsXMLValue,
                                 bool bNotify,
                                 bool bScriptModify) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), XFA_ATTRIBUTE_Value);
  node_->OnChanging(XFA_ATTRIBUTE_Value, bNotify);
  WideString* pClone = new WideString(wsValue);
  SetUserData(pKey, pClone, &deleteWideStringCallBack);
  node_->OnChanged(XFA_ATTRIBUTE_Value, bNotify, bScriptModify);
  if (node_->IsNeedSavingXMLNode()) {
    FX_XMLNODETYPE eXMLType = node_->GetXMLMappingNode()->GetType();
    switch (eXMLType) {
      case FX_XMLNODE_Element:
        if (node_->IsAttributeInXML()) {
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetString(WideString(GetCData(XFA_ATTRIBUTE_QualifiedName)),
                          wsXMLValue);
        } else {
          bool bDeleteChildren = true;
          if (node_->GetPacketID() == XFA_XDPPACKET_Datasets) {
            for (CXFA_Node* pChildDataNode =
                     node_->GetNodeItem(XFA_NODEITEM_FirstChild);
                 pChildDataNode; pChildDataNode = pChildDataNode->GetNodeItem(
                                     XFA_NODEITEM_NextSibling)) {
              if (!pChildDataNode->GetBindItems().empty()) {
                bDeleteChildren = false;
                break;
              }
            }
          }
          if (bDeleteChildren) {
            static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
                ->DeleteChildren();
          }
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetTextData(wsXMLValue);
        }
        break;
      case FX_XMLNODE_Text:
        static_cast<CFX_XMLText*>(node_->GetXMLMappingNode())
            ->SetText(wsXMLValue);
        break;
      default:
        ASSERT(0);
    }
  }
  return true;
}

bool CJX_Node::TryCData(XFA_ATTRIBUTE eAttr,
                        WideString& wsValue,
                        bool bUseDefault,
                        bool bProto) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    WideString* pStr = (WideString*)GetUserData(pKey, bProto);
    if (pStr) {
      wsValue = *pStr;
      return true;
    }
  } else {
    WideStringView wsValueC;
    if (GetMapModuleString(pKey, wsValueC)) {
      wsValue = wsValueC;
      return true;
    }
  }
  if (!bUseDefault) {
    return false;
  }
  void* pValue = nullptr;
  if (XFA_GetAttributeDefaultValue(pValue, node_->GetElementType(), eAttr,
                                   XFA_ATTRIBUTETYPE_Cdata,
                                   node_->GetPacketID())) {
    wsValue = (const wchar_t*)pValue;
    return true;
  }
  return false;
}

bool CJX_Node::TryCData(XFA_ATTRIBUTE eAttr,
                        WideStringView& wsValue,
                        bool bUseDefault,
                        bool bProto) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  if (eAttr == XFA_ATTRIBUTE_Value) {
    WideString* pStr = (WideString*)GetUserData(pKey, bProto);
    if (pStr) {
      wsValue = pStr->AsStringView();
      return true;
    }
  } else {
    if (GetMapModuleString(pKey, wsValue)) {
      return true;
    }
  }
  if (!bUseDefault) {
    return false;
  }
  void* pValue = nullptr;
  if (XFA_GetAttributeDefaultValue(pValue, node_->GetElementType(), eAttr,
                                   XFA_ATTRIBUTETYPE_Cdata,
                                   node_->GetPacketID())) {
    wsValue = (WideStringView)(const wchar_t*)pValue;
    return true;
  }
  return false;
}

bool CJX_Node::SetObject(XFA_ATTRIBUTE eAttr,
                         void* pData,
                         XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  return SetUserData(pKey, pData, pCallbackInfo);
}

bool CJX_Node::TryObject(XFA_ATTRIBUTE eAttr, void*& pData) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  pData = GetUserData(pKey);
  return !!pData;
}

bool CJX_Node::SetValue(XFA_ATTRIBUTE eAttr,
                        XFA_ATTRIBUTETYPE eType,
                        void* pValue,
                        bool bNotify) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  node_->OnChanging(eAttr, bNotify);
  SetMapModuleValue(pKey, pValue);
  node_->OnChanged(eAttr, bNotify, false);
  if (node_->IsNeedSavingXMLNode()) {
    ASSERT(node_->GetXMLMappingNode()->GetType() == FX_XMLNODE_Element);
    const XFA_ATTRIBUTEINFO* pInfo = XFA_GetAttributeByID(eAttr);
    if (pInfo) {
      switch (eType) {
        case XFA_ATTRIBUTETYPE_Enum:
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetString(
                  pInfo->pName,
                  GetAttributeEnumByID((XFA_ATTRIBUTEENUM)(uintptr_t)pValue)
                      ->pName);
          break;
        case XFA_ATTRIBUTETYPE_Boolean:
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetString(pInfo->pName, pValue ? L"1" : L"0");
          break;
        case XFA_ATTRIBUTETYPE_Integer: {
          WideString wsValue;
          wsValue.Format(
              L"%d", static_cast<int32_t>(reinterpret_cast<uintptr_t>(pValue)));
          static_cast<CFX_XMLElement*>(node_->GetXMLMappingNode())
              ->SetString(pInfo->pName, wsValue);
          break;
        }
        default:
          ASSERT(0);
      }
    }
  }
  return true;
}

bool CJX_Node::GetValue(XFA_ATTRIBUTE eAttr,
                        XFA_ATTRIBUTETYPE eType,
                        bool bUseDefault,
                        void*& pValue) {
  void* pKey = GetMapKey_Element(node_->GetElementType(), eAttr);
  if (GetMapModuleValue(pKey, pValue)) {
    return true;
  }
  if (!bUseDefault) {
    return false;
  }
  return XFA_GetAttributeDefaultValue(pValue, node_->GetElementType(), eAttr,
                                      eType, node_->GetPacketID());
}

bool CJX_Node::SetUserData(void* pKey,
                           void* pData,
                           XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  SetMapModuleBuffer(pKey, &pData, sizeof(void*),
                     pCallbackInfo ? pCallbackInfo : &gs_XFADefaultFreeData);
  return true;
}

bool CJX_Node::TryUserData(void* pKey, void*& pData, bool bProtoAlso) {
  int32_t iBytes = 0;
  if (!GetMapModuleBuffer(pKey, pData, iBytes, bProtoAlso)) {
    return false;
  }
  return iBytes == sizeof(void*) && memcpy(&pData, pData, iBytes);
}

bool CJX_Node::SetScriptContent(const WideString& wsContent,
                                const WideString& wsXMLValue,
                                bool bNotify,
                                bool bScriptModify,
                                bool bSyncData) {
  CXFA_Node* pNode = nullptr;
  CXFA_Node* pBindNode = nullptr;
  switch (node_->GetObjectType()) {
    case XFA_ObjectType::ContainerNode: {
      if (XFA_FieldIsMultiListBox(node_.Get())) {
        CXFA_Node* pValue = GetProperty(0, XFA_Element::Value);
        if (!pValue)
          break;

        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        ASSERT(pChildValue);
        pChildValue->JSNode()->SetCData(XFA_ATTRIBUTE_ContentType, L"text/xml");
        pChildValue->JSNode()->SetScriptContent(wsContent, wsContent, bNotify,
                                                bScriptModify, false);
        CXFA_Node* pBind = node_->GetBindData();
        if (bSyncData && pBind) {
          std::vector<WideString> wsSaveTextArray;
          size_t iSize = 0;
          if (!wsContent.IsEmpty()) {
            size_t iStart = 0;
            size_t iLength = wsContent.GetLength();
            auto iEnd = wsContent.Find(L'\n', iStart);
            iEnd = !iEnd.has_value() ? iLength : iEnd;
            while (iEnd.value() >= iStart) {
              wsSaveTextArray.push_back(
                  wsContent.Mid(iStart, iEnd.value() - iStart));
              iStart = iEnd.value() + 1;
              if (iStart >= iLength) {
                break;
              }
              iEnd = wsContent.Find(L'\n', iStart);
              if (!iEnd.has_value()) {
                wsSaveTextArray.push_back(
                    wsContent.Mid(iStart, iLength - iStart));
              }
            }
            iSize = wsSaveTextArray.size();
          }
          if (iSize == 0) {
            while (CXFA_Node* pChildNode =
                       pBind->GetNodeItem(XFA_NODEITEM_FirstChild)) {
              pBind->RemoveChild(pChildNode);
            }
          } else {
            std::vector<CXFA_Node*> valueNodes = pBind->GetNodeList(
                XFA_NODEFILTER_Children, XFA_Element::DataValue);
            size_t iDatas = valueNodes.size();
            if (iDatas < iSize) {
              size_t iAddNodes = iSize - iDatas;
              CXFA_Node* pValueNodes = nullptr;
              while (iAddNodes-- > 0) {
                pValueNodes =
                    pBind->CreateSamePacketNode(XFA_Element::DataValue);
                pValueNodes->JSNode()->SetCData(XFA_ATTRIBUTE_Name, L"value");
                pValueNodes->CreateXMLMappingNode();
                pBind->InsertChild(pValueNodes);
              }
              pValueNodes = nullptr;
            } else if (iDatas > iSize) {
              size_t iDelNodes = iDatas - iSize;
              while (iDelNodes-- > 0) {
                pBind->RemoveChild(pBind->GetNodeItem(XFA_NODEITEM_FirstChild));
              }
            }
            int32_t i = 0;
            for (CXFA_Node* pValueNode =
                     pBind->GetNodeItem(XFA_NODEITEM_FirstChild);
                 pValueNode; pValueNode = pValueNode->GetNodeItem(
                                 XFA_NODEITEM_NextSibling)) {
              pValueNode->JSNode()->SetAttributeValue(
                  wsSaveTextArray[i], wsSaveTextArray[i], false);
              i++;
            }
          }
          for (CXFA_Node* pArrayNode : pBind->GetBindItems()) {
            if (pArrayNode != node_.Get()) {
              pArrayNode->JSNode()->SetScriptContent(
                  wsContent, wsContent, bNotify, bScriptModify, false);
            }
          }
        }
        break;
      }
      if (node_->GetElementType() == XFA_Element::ExclGroup) {
        pNode = node_.Get();
      } else {
        CXFA_Node* pValue = GetProperty(0, XFA_Element::Value);
        if (!pValue)
          break;

        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        ASSERT(pChildValue);
        pChildValue->JSNode()->SetScriptContent(wsContent, wsContent, bNotify,
                                                bScriptModify, false);
      }
      pBindNode = node_->GetBindData();
      if (pBindNode && bSyncData) {
        pBindNode->JSNode()->SetScriptContent(wsContent, wsXMLValue, bNotify,
                                              bScriptModify, false);
        for (CXFA_Node* pArrayNode : pBindNode->GetBindItems()) {
          if (pArrayNode != node_.Get()) {
            pArrayNode->JSNode()->SetScriptContent(wsContent, wsContent,
                                                   bNotify, true, false);
          }
        }
      }
      pBindNode = nullptr;
      break;
    }
    case XFA_ObjectType::ContentNode: {
      WideString wsContentType;
      if (node_->GetElementType() == XFA_Element::ExData) {
        GetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType, false);
        if (wsContentType == L"text/html") {
          wsContentType = L"";
          SetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType.AsStringView());
        }
      }
      CXFA_Node* pContentRawDataNode =
          node_->GetNodeItem(XFA_NODEITEM_FirstChild);
      if (!pContentRawDataNode) {
        pContentRawDataNode = node_->CreateSamePacketNode(
            (wsContentType == L"text/xml") ? XFA_Element::Sharpxml
                                           : XFA_Element::Sharptext);
        node_->InsertChild(pContentRawDataNode);
      }
      return pContentRawDataNode->JSNode()->SetScriptContent(
          wsContent, wsXMLValue, bNotify, bScriptModify, bSyncData);
    }
    case XFA_ObjectType::NodeC:
    case XFA_ObjectType::TextNode:
      pNode = node_.Get();
      break;
    case XFA_ObjectType::NodeV:
      pNode = node_.Get();
      if (bSyncData && node_->GetPacketID() == XFA_XDPPACKET_Form) {
        CXFA_Node* pParent = node_->GetNodeItem(XFA_NODEITEM_Parent);
        if (pParent) {
          pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
        }
        if (pParent && pParent->GetElementType() == XFA_Element::Value) {
          pParent = pParent->GetNodeItem(XFA_NODEITEM_Parent);
          if (pParent && pParent->IsContainerNode()) {
            pBindNode = pParent->GetBindData();
            if (pBindNode) {
              pBindNode->JSNode()->SetScriptContent(
                  wsContent, wsXMLValue, bNotify, bScriptModify, false);
            }
          }
        }
      }
      break;
    default:
      if (node_->GetElementType() == XFA_Element::DataValue) {
        pNode = node_.Get();
        pBindNode = node_.Get();
      }
      break;
  }
  if (!pNode)
    return false;

  SetAttributeValue(wsContent, wsXMLValue, bNotify, bScriptModify);
  if (pBindNode && bSyncData) {
    for (CXFA_Node* pArrayNode : pBindNode->GetBindItems()) {
      pArrayNode->JSNode()->SetScriptContent(wsContent, wsContent, bNotify,
                                             bScriptModify, false);
    }
  }
  return true;
}

bool CJX_Node::SetContent(const WideString& wsContent,
                          const WideString& wsXMLValue,
                          bool bNotify,
                          bool bScriptModify,
                          bool bSyncData) {
  return SetScriptContent(wsContent, wsXMLValue, bNotify, bScriptModify,
                          bSyncData);
}

WideString CJX_Node::GetScriptContent(bool bScriptModify) {
  WideString wsContent;
  return TryContent(wsContent, bScriptModify) ? wsContent : WideString();
}

WideString CJX_Node::GetContent() {
  return GetScriptContent();
}

bool CJX_Node::TryContent(WideString& wsContent,
                          bool bScriptModify,
                          bool bProto) {
  CXFA_Node* pNode = nullptr;
  switch (node_->GetObjectType()) {
    case XFA_ObjectType::ContainerNode:
      if (node_->GetElementType() == XFA_Element::ExclGroup) {
        pNode = node_.Get();
      } else {
        CXFA_Node* pValue = node_->GetChild(0, XFA_Element::Value);
        if (!pValue) {
          return false;
        }
        CXFA_Node* pChildValue = pValue->GetNodeItem(XFA_NODEITEM_FirstChild);
        if (pChildValue && XFA_FieldIsMultiListBox(node_.Get())) {
          pChildValue->JSNode()->SetAttribute(XFA_ATTRIBUTE_ContentType,
                                              L"text/xml");
        }
        return pChildValue ? pChildValue->JSNode()->TryContent(
                                 wsContent, bScriptModify, bProto)
                           : false;
      }
      break;
    case XFA_ObjectType::ContentNode: {
      CXFA_Node* pContentRawDataNode =
          node_->GetNodeItem(XFA_NODEITEM_FirstChild);
      if (!pContentRawDataNode) {
        XFA_Element element = XFA_Element::Sharptext;
        if (node_->GetElementType() == XFA_Element::ExData) {
          WideString wsContentType;
          GetAttribute(XFA_ATTRIBUTE_ContentType, wsContentType, false);
          if (wsContentType == L"text/html") {
            element = XFA_Element::SharpxHTML;
          } else if (wsContentType == L"text/xml") {
            element = XFA_Element::Sharpxml;
          }
        }
        pContentRawDataNode = node_->CreateSamePacketNode(element);
        node_->InsertChild(pContentRawDataNode);
      }
      return pContentRawDataNode->JSNode()->TryContent(wsContent, bScriptModify,
                                                       bProto);
    }
    case XFA_ObjectType::NodeC:
    case XFA_ObjectType::NodeV:
    case XFA_ObjectType::TextNode:
      pNode = node_.Get();
    default:
      if (node_->GetElementType() == XFA_Element::DataValue) {
        pNode = node_.Get();
      }
      break;
  }
  if (pNode) {
    if (bScriptModify) {
      CFXJSE_Engine* pScriptContext = node_->GetDocument()->GetScriptContext();
      if (pScriptContext) {
        node_->GetDocument()->GetScriptContext()->AddNodesOfRunScript(
            node_.Get());
      }
    }
    return TryCData(XFA_ATTRIBUTE_Value, wsContent, false, bProto);
  }
  return false;
}

bool CJX_Node::TryNamespace(WideString& wsNamespace) {
  wsNamespace.clear();
  if (node_->IsModelNode() || node_->GetElementType() == XFA_Element::Packet) {
    CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
    if (!pXMLNode || pXMLNode->GetType() != FX_XMLNODE_Element)
      return false;

    wsNamespace = static_cast<CFX_XMLElement*>(pXMLNode)->GetNamespaceURI();
    return true;
  }

  if (node_->GetPacketID() != XFA_XDPPACKET_Datasets)
    return node_->GetModelNode()->JSNode()->TryNamespace(wsNamespace);

  CFX_XMLNode* pXMLNode = node_->GetXMLMappingNode();
  if (!pXMLNode)
    return false;
  if (pXMLNode->GetType() != FX_XMLNODE_Element)
    return true;

  if (node_->GetElementType() == XFA_Element::DataValue &&
      GetEnum(XFA_ATTRIBUTE_Contains) == XFA_ATTRIBUTEENUM_MetaData) {
    return XFA_FDEExtension_ResolveNamespaceQualifier(
        static_cast<CFX_XMLElement*>(pXMLNode),
        GetCData(XFA_ATTRIBUTE_QualifiedName), &wsNamespace);
  }
  wsNamespace = static_cast<CFX_XMLElement*>(pXMLNode)->GetNamespaceURI();
  return true;
}

CXFA_Node* CJX_Node::GetProperty(int32_t index,
                                 XFA_Element eProperty,
                                 bool bCreateProperty) {
  XFA_Element eType = node_->GetElementType();
  uint32_t dwPacket = node_->GetPacketID();
  const XFA_PROPERTY* pProperty =
      XFA_GetPropertyOfElement(eType, eProperty, dwPacket);
  if (!pProperty || index >= pProperty->uOccur)
    return nullptr;

  CXFA_Node* pNode = node_->GetChildNode();
  int32_t iCount = 0;
  for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pNode->GetElementType() == eProperty) {
      iCount++;
      if (iCount > index) {
        return pNode;
      }
    }
  }
  if (!bCreateProperty)
    return nullptr;

  if (pProperty->uFlags & XFA_PROPERTYFLAG_OneOf) {
    pNode = node_->GetChildNode();
    for (; pNode; pNode = pNode->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      const XFA_PROPERTY* pExistProperty =
          XFA_GetPropertyOfElement(eType, pNode->GetElementType(), dwPacket);
      if (pExistProperty && (pExistProperty->uFlags & XFA_PROPERTYFLAG_OneOf))
        return nullptr;
    }
  }

  const XFA_PACKETINFO* pPacket = XFA_GetPacketByID(dwPacket);
  CXFA_Node* pNewNode = nullptr;
  for (; iCount <= index; iCount++) {
    pNewNode = node_->GetDocument()->CreateNode(pPacket, eProperty);
    if (!pNewNode)
      return nullptr;
    node_->InsertChild(pNewNode, nullptr);
    pNewNode->SetFlag(XFA_NodeFlag_Initialized, true);
  }
  return pNewNode;
}

XFA_MAPMODULEDATA* CJX_Node::CreateMapModuleData() {
  if (!map_module_data_)
    map_module_data_ = new XFA_MAPMODULEDATA;
  return map_module_data_;
}

XFA_MAPMODULEDATA* CJX_Node::GetMapModuleData() const {
  return map_module_data_;
}

void CJX_Node::SetMapModuleValue(void* pKey, void* pValue) {
  XFA_MAPMODULEDATA* pModule = CreateMapModuleData();
  pModule->m_ValueMap[pKey] = pValue;
}

bool CJX_Node::GetMapModuleValue(void* pKey, void*& pValue) {
  for (CXFA_Node* pNode = node_.Get(); pNode;
       pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->JSNode()->GetMapModuleData();
    if (pModule) {
      auto it = pModule->m_ValueMap.find(pKey);
      if (it != pModule->m_ValueMap.end()) {
        pValue = it->second;
        return true;
      }
    }
    if (pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  return false;
}

void CJX_Node::SetMapModuleString(void* pKey, const WideStringView& wsValue) {
  SetMapModuleBuffer(pKey, (void*)wsValue.unterminated_c_str(),
                     wsValue.GetLength() * sizeof(wchar_t));
}

bool CJX_Node::GetMapModuleString(void* pKey, WideStringView& wsValue) {
  void* pValue;
  int32_t iBytes;
  if (!GetMapModuleBuffer(pKey, pValue, iBytes))
    return false;
  // Defensive measure: no out-of-bounds pointers even if zero length.
  int32_t iChars = iBytes / sizeof(wchar_t);
  wsValue = WideStringView(iChars ? (const wchar_t*)pValue : nullptr, iChars);
  return true;
}

void CJX_Node::SetMapModuleBuffer(void* pKey,
                                  void* pValue,
                                  int32_t iBytes,
                                  XFA_MAPDATABLOCKCALLBACKINFO* pCallbackInfo) {
  XFA_MAPMODULEDATA* pModule = CreateMapModuleData();
  XFA_MAPDATABLOCK*& pBuffer = pModule->m_BufferMap[pKey];
  if (!pBuffer) {
    pBuffer =
        (XFA_MAPDATABLOCK*)FX_Alloc(uint8_t, sizeof(XFA_MAPDATABLOCK) + iBytes);
  } else if (pBuffer->iBytes != iBytes) {
    if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree) {
      pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
    }
    pBuffer = (XFA_MAPDATABLOCK*)FX_Realloc(uint8_t, pBuffer,
                                            sizeof(XFA_MAPDATABLOCK) + iBytes);
  } else if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree) {
    pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
  }
  if (!pBuffer)
    return;

  pBuffer->pCallbackInfo = pCallbackInfo;
  pBuffer->iBytes = iBytes;
  memcpy(pBuffer->GetData(), pValue, iBytes);
}

bool CJX_Node::GetMapModuleBuffer(void* pKey,
                                  void*& pValue,
                                  int32_t& iBytes,
                                  bool bProtoAlso) const {
  XFA_MAPDATABLOCK* pBuffer = nullptr;
  for (const CXFA_Node* pNode = node_.Get(); pNode;
       pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->JSNode()->GetMapModuleData();
    if (pModule) {
      auto it = pModule->m_BufferMap.find(pKey);
      if (it != pModule->m_BufferMap.end()) {
        pBuffer = it->second;
        break;
      }
    }
    if (!bProtoAlso || pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  if (!pBuffer)
    return false;

  pValue = pBuffer->GetData();
  iBytes = pBuffer->iBytes;
  return true;
}

bool CJX_Node::HasMapModuleKey(void* pKey, bool bProtoAlso) {
  for (CXFA_Node* pNode = node_.Get(); pNode;
       pNode = pNode->GetTemplateNode()) {
    XFA_MAPMODULEDATA* pModule = pNode->JSNode()->GetMapModuleData();
    if (pModule) {
      auto it1 = pModule->m_ValueMap.find(pKey);
      if (it1 != pModule->m_ValueMap.end())
        return true;

      auto it2 = pModule->m_BufferMap.find(pKey);
      if (it2 != pModule->m_BufferMap.end())
        return true;
    }
    if (!bProtoAlso || pNode->GetPacketID() == XFA_XDPPACKET_Datasets)
      break;
  }
  return false;
}

void CJX_Node::RemoveMapModuleKey(void* pKey) {
  XFA_MAPMODULEDATA* pModule = GetMapModuleData();
  if (!pModule)
    return;

  if (pKey) {
    auto it = pModule->m_BufferMap.find(pKey);
    if (it != pModule->m_BufferMap.end()) {
      XFA_MAPDATABLOCK* pBuffer = it->second;
      if (pBuffer) {
        if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree)
          pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
        FX_Free(pBuffer);
      }
      pModule->m_BufferMap.erase(it);
    }
    pModule->m_ValueMap.erase(pKey);
    return;
  }

  for (auto& pair : pModule->m_BufferMap) {
    XFA_MAPDATABLOCK* pBuffer = pair.second;
    if (pBuffer) {
      if (pBuffer->pCallbackInfo && pBuffer->pCallbackInfo->pFree)
        pBuffer->pCallbackInfo->pFree(*(void**)pBuffer->GetData());
      FX_Free(pBuffer);
    }
  }
  pModule->m_BufferMap.clear();
  pModule->m_ValueMap.clear();
  delete pModule;
}

void CJX_Node::MergeAllData(void* pDstModule) {
  XFA_MAPMODULEDATA* pDstModuleData =
      static_cast<CXFA_Node*>(pDstModule)->JSNode()->CreateMapModuleData();
  XFA_MAPMODULEDATA* pSrcModuleData = GetMapModuleData();
  if (!pSrcModuleData)
    return;

  for (const auto& pair : pSrcModuleData->m_ValueMap)
    pDstModuleData->m_ValueMap[pair.first] = pair.second;

  for (const auto& pair : pSrcModuleData->m_BufferMap) {
    XFA_MAPDATABLOCK* pSrcBuffer = pair.second;
    XFA_MAPDATABLOCK*& pDstBuffer = pDstModuleData->m_BufferMap[pair.first];
    if (pSrcBuffer->pCallbackInfo && pSrcBuffer->pCallbackInfo->pFree &&
        !pSrcBuffer->pCallbackInfo->pCopy) {
      if (pDstBuffer) {
        pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
        pDstModuleData->m_BufferMap.erase(pair.first);
      }
      continue;
    }
    if (!pDstBuffer) {
      pDstBuffer = (XFA_MAPDATABLOCK*)FX_Alloc(
          uint8_t, sizeof(XFA_MAPDATABLOCK) + pSrcBuffer->iBytes);
    } else if (pDstBuffer->iBytes != pSrcBuffer->iBytes) {
      if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pFree) {
        pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
      }
      pDstBuffer = (XFA_MAPDATABLOCK*)FX_Realloc(
          uint8_t, pDstBuffer, sizeof(XFA_MAPDATABLOCK) + pSrcBuffer->iBytes);
    } else if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pFree) {
      pDstBuffer->pCallbackInfo->pFree(*(void**)pDstBuffer->GetData());
    }
    if (!pDstBuffer) {
      continue;
    }
    pDstBuffer->pCallbackInfo = pSrcBuffer->pCallbackInfo;
    pDstBuffer->iBytes = pSrcBuffer->iBytes;
    memcpy(pDstBuffer->GetData(), pSrcBuffer->GetData(), pSrcBuffer->iBytes);
    if (pDstBuffer->pCallbackInfo && pDstBuffer->pCallbackInfo->pCopy) {
      pDstBuffer->pCallbackInfo->pCopy(*(void**)pDstBuffer->GetData());
    }
  }
}

void CJX_Node::MoveBufferMapData(CXFA_Node* pDstModule, void* pKey) {
  if (!pDstModule) {
    return;
  }
  bool bNeedMove = true;
  if (!pKey) {
    bNeedMove = false;
  }
  if (pDstModule->GetElementType() != node_->GetElementType()) {
    bNeedMove = false;
  }
  XFA_MAPMODULEDATA* pSrcModuleData = nullptr;
  XFA_MAPMODULEDATA* pDstModuleData = nullptr;
  if (bNeedMove) {
    pSrcModuleData = GetMapModuleData();
    if (!pSrcModuleData) {
      bNeedMove = false;
    }
    pDstModuleData = pDstModule->JSNode()->CreateMapModuleData();
  }
  if (bNeedMove) {
    auto it = pSrcModuleData->m_BufferMap.find(pKey);
    if (it != pSrcModuleData->m_BufferMap.end()) {
      XFA_MAPDATABLOCK* pBufferBlockData = it->second;
      if (pBufferBlockData) {
        pSrcModuleData->m_BufferMap.erase(pKey);
        pDstModuleData->m_BufferMap[pKey] = pBufferBlockData;
      }
    }
  }
  if (pDstModule->IsNodeV()) {
    WideString wsValue = pDstModule->JSNode()->GetScriptContent(false);
    WideString wsFormatValue(wsValue);
    CXFA_WidgetData* pWidgetData = pDstModule->GetContainerWidgetData();
    if (pWidgetData) {
      pWidgetData->GetFormatDataValue(wsValue, wsFormatValue);
    }
    pDstModule->JSNode()->SetScriptContent(wsValue, wsFormatValue, true, true);
  }
}

void CJX_Node::MoveBufferMapData(CXFA_Node* pSrcModule,
                                 CXFA_Node* pDstModule,
                                 void* pKey,
                                 bool bRecursive) {
  if (!pSrcModule || !pDstModule || !pKey) {
    return;
  }
  if (bRecursive) {
    CXFA_Node* pSrcChild = pSrcModule->GetNodeItem(XFA_NODEITEM_FirstChild);
    CXFA_Node* pDstChild = pDstModule->GetNodeItem(XFA_NODEITEM_FirstChild);
    for (; pSrcChild && pDstChild;
         pSrcChild = pSrcChild->GetNodeItem(XFA_NODEITEM_NextSibling),
         pDstChild = pDstChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
      MoveBufferMapData(pSrcChild, pDstChild, pKey, true);
    }
  }
  pSrcModule->JSNode()->MoveBufferMapData(pDstModule, pKey);
}

int32_t CJX_Node::execSingleEventByName(const WideStringView& wsEventName,
                                        XFA_Element eType) {
  int32_t iRet = XFA_EVENTERROR_NotExist;
  const XFA_ExecEventParaInfo* eventParaInfo =
      GetEventParaInfoByName(wsEventName);
  if (eventParaInfo) {
    uint32_t validFlags = eventParaInfo->m_validFlags;
    CXFA_FFNotify* pNotify = node_->GetDocument()->GetNotify();
    if (!pNotify) {
      return iRet;
    }
    if (validFlags == 1) {
      iRet = pNotify->ExecEventByDeepFirst(node_.Get(),
                                           eventParaInfo->m_eventType);
    } else if (validFlags == 2) {
      iRet = pNotify->ExecEventByDeepFirst(
          node_.Get(), eventParaInfo->m_eventType, false, false);
    } else if (validFlags == 3) {
      if (eType == XFA_Element::Subform) {
        iRet = pNotify->ExecEventByDeepFirst(
            node_.Get(), eventParaInfo->m_eventType, false, false);
      }
    } else if (validFlags == 4) {
      if (eType == XFA_Element::ExclGroup || eType == XFA_Element::Field) {
        CXFA_Node* pParentNode = node_->GetNodeItem(XFA_NODEITEM_Parent);
        if (pParentNode &&
            pParentNode->GetElementType() == XFA_Element::ExclGroup) {
          iRet = pNotify->ExecEventByDeepFirst(
              node_.Get(), eventParaInfo->m_eventType, false, false);
        }
        iRet = pNotify->ExecEventByDeepFirst(
            node_.Get(), eventParaInfo->m_eventType, false, false);
      }
    } else if (validFlags == 5) {
      if (eType == XFA_Element::Field) {
        iRet = pNotify->ExecEventByDeepFirst(
            node_.Get(), eventParaInfo->m_eventType, false, false);
      }
    } else if (validFlags == 6) {
      CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
      if (pWidgetData) {
        CXFA_Node* pUINode = pWidgetData->GetUIChild();
        if (pUINode->GetElementType() == XFA_Element::Signature) {
          iRet = pNotify->ExecEventByDeepFirst(
              node_.Get(), eventParaInfo->m_eventType, false, false);
        }
      }
    } else if (validFlags == 7) {
      CXFA_WidgetData* pWidgetData = node_->GetWidgetData();
      if (pWidgetData) {
        CXFA_Node* pUINode = pWidgetData->GetUIChild();
        if ((pUINode->GetElementType() == XFA_Element::ChoiceList) &&
            (!pWidgetData->IsListBox())) {
          iRet = pNotify->ExecEventByDeepFirst(
              node_.Get(), eventParaInfo->m_eventType, false, false);
        }
      }
    }
  }
  return iRet;
}
