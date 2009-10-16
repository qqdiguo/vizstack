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

#include "vsdomparser.hpp"
#include <xercesc/framework/MemBufInputSource.hpp>
#include <iostream>

using namespace std;

VSDOMParserErrorHandler::VSDOMParserErrorHandler()
{
  resetErrors();
}

void VSDOMParserErrorHandler::resetErrors()
{
  m_messages.clear();
  m_nErrors=0;
  m_nWarnings=0;
}

bool VSDOMParserErrorHandler::haveMessages() const
{
  return (m_messages.size()>0);
}

bool VSDOMParserErrorHandler::haveErrors() const
{
  return (m_nErrors!=0);
}
// ---------------------------------------------------------------------------
//  DOMCountHandlers: Overrides of the DOM ErrorHandler interface
// ---------------------------------------------------------------------------
bool VSDOMParserErrorHandler::handleError(const DOMError& domError)
{
  char num[20];
  string message;
  message=transcode2string(domError.getLocation()->getURI ());
  message+=":";
  sprintf(num, "%u",(unsigned int)domError.getLocation ()->getLineNumber());
  message+=num;
  message+=":column ";
  sprintf(num, "%u",(unsigned int)domError.getLocation ()->getColumnNumber());
  message+=num;
  message+=":";
  if (domError.getSeverity () == DOMError::DOM_SEVERITY_WARNING)
  {
     m_nWarnings++;
     message += "warning";
  }
  else if (domError.getSeverity () == DOMError::DOM_SEVERITY_ERROR)
  {
     m_nErrors++;
     message += "error";
  }
  else
  {
     m_nErrors++;
     message += "fatal error";
  }

  message += ":";

  message += transcode2string(domError.getMessage());
  m_messages.push_back(message);
  return true;
}

std::string transcode2string(const XMLCh *str)
{
	char *ret = XMLString::transcode(str);
	std::string retStr = ret;
	XMLString::release(&ret);
	return retStr;
}

void VSDOMParserErrorHandler::getMessages(std::vector<std::string>& msg) const
{
	msg=m_messages;
}

VSDOMParserErrorHandler::~VSDOMParserErrorHandler()
{
}

bool VSDOMParser::Initialize()
{
	bool                       recognizeNEL = false;
  char localeStr[64];
  memset (localeStr, 0, sizeof localeStr);
  // Initialize Xerces Parsing
  try
  {
    if (strlen (localeStr))
      {
	XMLPlatformUtils::Initialize (localeStr);
      }
    else
      {
	XMLPlatformUtils::Initialize ();
      }

    if (recognizeNEL)
      {
	XMLPlatformUtils::recognizeNEL (recognizeNEL);
      }
  }
  catch (const XMLException & toCatch)
  {
    XERCES_STD_QUALIFIER cerr << "Error during initialization! :\n"
      << transcode2string (toCatch.getMessage ()) << XERCES_STD_QUALIFIER
      endl;
    return false;
  }

  return true;

}


void VSDOMParser::Finalize()
{
  // And call the termination method
  XMLPlatformUtils::Terminate ();
}

DOMDocument* VSDOMParser::Parse(const char* source, bool sourceIsFile, VSDOMParserErrorHandler& errorHandler)
{
	// Instantiate the DOM parser.
	static const XMLCh gLS[] = { chLatin_L, chLatin_S, chNull };
	DOMImplementation *impl = DOMImplementationRegistry::getDOMImplementation(gLS);
	m_parser = ((DOMImplementationLS*)impl)->createDOMBuilder(DOMImplementationLS::MODE_SYNCHRONOUS, 0);

	// Using Auto ensures that validation is done if the schema references are present in the document
	AbstractDOMParser::ValSchemes valScheme = AbstractDOMParser::Val_Auto; 

	bool                       doNamespaces       = true;
	bool                       doSchema           = true;
	bool                       schemaFullChecking = true;

	m_parser->setFeature(XMLUni::fgDOMNamespaces, doNamespaces);
	m_parser->setFeature(XMLUni::fgXercesSchema, doSchema);
	m_parser->setFeature(XMLUni::fgXercesSchemaFullChecking, schemaFullChecking);

	if (valScheme == AbstractDOMParser::Val_Auto)
	{
		m_parser->setFeature(XMLUni::fgDOMValidateIfSchema, true);
	}
	else if (valScheme == AbstractDOMParser::Val_Never)
	{
		m_parser->setFeature(XMLUni::fgDOMValidation, false);
	}
	else if (valScheme == AbstractDOMParser::Val_Always)
	{
		m_parser->setFeature(XMLUni::fgDOMValidation, true);
	}

	// enable datatype normalization - default is off
	m_parser->setFeature(XMLUni::fgDOMDatatypeNormalization, true);

	// And install our error handler
	m_parser->setErrorHandler(&errorHandler);

	//reset error count first
	errorHandler.resetErrors();

	// Now parse the document
	XERCES_CPP_NAMESPACE_QUALIFIER DOMDocument *doc = 0;

	try
	{
		// reset document pool
		m_parser->resetDocumentPool();

		if(sourceIsFile)
			doc = m_parser->parseURI(source);
		else
		{
			 MemBufInputSource* memBufIS = new MemBufInputSource(
				(const XMLByte*) source,
				strlen(source),
				"XML",
				false);
			Wrapper4InputSource wrapperIS(memBufIS);
			doc = m_parser->parse(wrapperIS);
			// FIXME: where will the memory for memBufIS get cleaned !?!
		}
	}
	catch (const XMLException& toCatch)
	{
		XERCES_STD_QUALIFIER cerr << "\nError during parsing: '" << ((sourceIsFile)? source : "memory-message") << "'\n"
			<< "Exception message is:  \n"
			<< transcode2string(toCatch.getMessage()) << "\n" << XERCES_STD_QUALIFIER endl;
	}
	catch (const DOMException& toCatch)
	{
		const unsigned int maxChars = 2047;
		XMLCh errText[maxChars + 1];

		XERCES_STD_QUALIFIER cerr << "\nDOM Error during parsing: '" << ((sourceIsFile)? source : "memory-message") << "'\n"
			<< "DOMException code is:  " << toCatch.code << XERCES_STD_QUALIFIER endl;

		if (DOMImplementation::loadDOMExceptionMsg(toCatch.code, errText, maxChars))
			XERCES_STD_QUALIFIER cerr << "Message is: " << transcode2string(errText) << XERCES_STD_QUALIFIER endl;

	}
	catch (...)
	{
		XERCES_STD_QUALIFIER cerr << "\nUnexpected exception during parsing: '" << ((sourceIsFile)? source : "memory-message")<< "'\n";
	}

	if(errorHandler.haveErrors())
	{
		return 0;
	}

	return doc;
}

VSDOMParser::VSDOMParser()
{
	m_parser = 0;
}

VSDOMParser::~VSDOMParser()
{
	if(m_parser)
	{
		m_parser->release();
	}
	m_parser = 0;	
}

// Get a child node matching nodeName.
// Use this only if you have a single child node matching this name
DOMNode * getChildNode(DOMNode* node, std::string nodeName)
{
	DOMNode *node2process;
	for(node2process=node->getFirstChild(); node2process!=0; node2process=node2process->getNextSibling())
	{
		// skip comment and non-element nodes
		if(node2process->getNodeType()==DOMNode::COMMENT_NODE)
			continue;
		if(node2process->getNodeType()!=DOMNode::ELEMENT_NODE)
			continue;
		// return the node that matches the given name
		if(transcode2string(node2process->getNodeName())==nodeName)
		{
			return node2process;
		}
	}
	return 0;
}

std::vector<DOMNode *>getChildNodes(DOMNode* node, std::string nodeName)
{
	std::vector<DOMNode *> ret;
	DOMNode *node2process;
	for(node2process=node->getFirstChild(); node2process!=0; node2process=node2process->getNextSibling())
	{
		// skip comment and non-element nodes
		if(node2process->getNodeType()==DOMNode::COMMENT_NODE)
			continue;
		if(node2process->getNodeType()!=DOMNode::ELEMENT_NODE)
			continue;
		// process nodes that specify framebuffers
		if(transcode2string(node2process->getNodeName())==nodeName)
		{
			ret.push_back(node2process);
		}
	}
	return ret;
}

string getValueAsString(DOMNode *node)
{
	DOMNode *contentNode = node->getFirstChild();
	return transcode2string(contentNode->getNodeValue());
}

unsigned int getValueAsInt(DOMNode *node)
{
	DOMNode *contentNode = node->getFirstChild();
	string val=transcode2string(contentNode->getNodeValue());
	return atoi(val.c_str());
}

float getValueAsFloat(DOMNode *node)
{
	DOMNode *contentNode = node->getFirstChild();
	string val=transcode2string(contentNode->getNodeValue());
	return atof(val.c_str());
}

