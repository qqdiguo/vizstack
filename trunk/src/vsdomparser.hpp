/*
* VizStack - A Framework to manage visualization resources
* Copyright (C) 2009  name of Shreekumar <shreekumar/at/users.sourceforge.net>
* Copyright (C) 2009  name of Manjunath Sripadarao <manjunaths/at/users.sourceforge.net>
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __VS_DOM_PARSER_INCLUDED__
#define __VS_DOM_PARSER_INCLUDED__

// Xerces includes
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/parsers/AbstractDOMParser.hpp>
#include <xercesc/framework/Wrapper4InputSource.hpp>
#include <xercesc/dom/DOMImplementation.hpp>
#include <xercesc/dom/DOMImplementationLS.hpp>
#include <xercesc/dom/DOMImplementationRegistry.hpp>
#include <xercesc/dom/DOMBuilder.hpp>
#include <xercesc/dom/DOMException.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMNodeList.hpp>
#include <xercesc/dom/DOMError.hpp>
#include <xercesc/dom/DOMLocator.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>
#include <xercesc/dom/DOMAttr.hpp>
#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/util/XMLString.hpp>

XERCES_CPP_NAMESPACE_USE
#include <vector>
#include <string>

class VSDOMParserErrorHandler : public DOMErrorHandler
{
public:
  VSDOMParserErrorHandler ();
  ~VSDOMParserErrorHandler ();

  bool haveMessages () const;
  bool haveErrors () const;
  void getMessages(std::vector<std::string>& msg) const;
  // -----------------------------------------------------------------------
  //  Implementation of the DOM ErrorHandler interface
  // -----------------------------------------------------------------------
  bool handleError (const DOMError & domError);
  void resetErrors ();

private:
	std::vector<std::string> m_messages;
	unsigned int m_nErrors;
	unsigned int m_nWarnings;
};

class VSDOMParser
{
public:
  static bool Initialize ();
  static void Finalize ();

  VSDOMParser();
  ~VSDOMParser();

  DOMDocument* Parse (const char* source, bool sourceIsFile, VSDOMParserErrorHandler& errorHandler);
private:
  DOMBuilder *m_parser;
};

std::string transcode2string(const XMLCh *str);
DOMNode * getChildNode(DOMNode* node, std::string nodeName);
std::vector<DOMNode *>getChildNodes(DOMNode* node, std::string nodeName);
std::string getValueAsString(DOMNode *node);
unsigned int getValueAsInt(DOMNode *node);
float getValueAsFloat(DOMNode *node);

#endif
