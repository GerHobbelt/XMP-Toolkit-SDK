// =================================================================================================
// Copyright 2003 Adobe
// All Rights Reserved.
//
// NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it. 
//
// Adobe patent application tracking #P435, entitled 'Unique markers to simplify embedding data of
// one format in a file with a different format', inventors: Sean Parent, Greg Gilley.
// =================================================================================================

#include "public/include/XMP_Environment.h"	// ! This must be the first include!

#include <algorithm>	// For sort and stable_sort.
#include <cstdio>		// For snprintf.

#if XMP_DebugBuild
	#include <iostream>
#endif

#include "XMPCore/source/XMPCore_Impl.hpp"

#include "XMPCore/source/XMPMeta.hpp"
#include "XMPCore/source/XMPIterator.hpp"
#include "XMPCore/source/XMPUtils.hpp"
#include "public/include/XMP_Version.h"
#include "source/UnicodeInlines.incl_cpp"
#include "source/UnicodeConversions.hpp"


#include "XMPCore/XMPCoreDefines.h"
#if ENABLE_CPP_DOM_MODEL
	#include "XMPCommon/XMPCommon_I.h"
	#include "XMPCore/Interfaces/ICoreConfigurationManager_I.h"
	#include "XMPCommon/Interfaces/IMemoryAllocator.h"
	#include "XMPCore/Interfaces/INameSpacePrefixMap_I.h"
	#include "XMPCommon/ImplHeaders/SharedObjectImpl.h"
	#include "XMPCore/Interfaces/ICoreObjectFactory_I.h"
	#include "XMPCore/Interfaces/IDOMImplementationRegistry_I.h"
#endif

using namespace std;

#if XMP_WinBuild
	#pragma warning ( disable : 4533 )	// initialization of '...' is skipped by 'goto ...'
	#pragma warning ( disable : 4702 )	// unreachable code
	#pragma warning ( disable : 4800 )	// forcing value to bool 'true' or 'false' (performance warning)
	#pragma warning ( disable : 4996 )	// '...' was declared deprecated
#endif


	#if ENABLE_CPP_DOM_MODEL
		#if !XMP_StaticBuild

		#if XMP_WinBuild
			#pragma warning( push )
			#pragma warning( disable : 4250 )
		#endif
		class InternalClientAllocator
			: public AdobeXMPCommon::IMemoryAllocator
		{
		public:
			virtual void * APICALL allocate( AdobeXMPCommon::sizet size ) __NOTHROW__ {
				return sXMP_MemAlloc( size );
			}

			virtual void APICALL deallocate( void * ptr ) __NOTHROW__ {
				sXMP_MemFree( ptr );
			}

			virtual void * APICALL reallocate( void * ptr, AdobeXMPCommon::sizet size ) __NOTHROW__ {
				return NULL;
			}
			
			virtual ~InternalClientAllocator(){}

		};
		static InternalClientAllocator * sInternalClientAllocator( NULL );
		#if XMP_WinBuild
			#pragma warning( pop )
		#endif

		#endif  // !XMP_StaticBuild
	#endif  // ENABLE_CPP_DOM_MODEL


// *** Use the XMP_PropIsXyz (Schema, Simple, Struct, Array, ...) macros
// *** Add debug codegen checks, e.g. that typical masking operations really work
// *** Change all uses of strcmp and strncmp to XMP_LitMatch and XMP_LitNMatch


// =================================================================================================
// Local Types and Constants
// =========================


// =================================================================================================
// Static Variables
// ================

XMP_VarString * xdefaultName = 0;	// Needed in XMPMeta-Parse.cpp, MoveExplicitAliases.

static XMPMeta::ErrorCallbackInfo sDefaultErrorCallback;

// These are embedded version strings.

const char * kXMPCore_EmbeddedVersion   = kXMPCore_VersionMessage;
const char * kXMPCore_EmbeddedCopyright = kXMPCoreName " " kXMP_CopyrightStr;

// =================================================================================================
// Local Utilities
// ===============


// -------------------------------------------------------------------------------------------------
// DumpNodeOptions
// ---------------

void
DumpNodeOptions	( XMP_OptionBits	 options,
				  XMP_TextOutputProc outProc,
				  void *			 refCon )
{
	char		buffer [32];	// Decimal of a 64 bit int is at most about 20 digits.
	memset(buffer, 0, 32);

	static const char * optNames[] = { " schema",		// 0x8000_0000
									   " ?30",
									   " ?29",
									   " -COMMAS-",
									   " ?27",			// 0x0800_0000
									   " ?26",
									   " ?25",
									   " ?24",
									   " ?23",			// 0x0080_0000
									   " isStale",
									   " isDerived",
									   " isStable",
									   " ?19",			// 0x0008_0000
									   " isInternal",
									   " hasAliases",
									   " isAlias",
									   " -AFTER-",		// 0x0000_8000
									   " -BEFORE-",
									   " isCompact",
									   " isLangAlt",
									   " isAlt",		// 0x0000_0800
									   " isOrdered",
									   " isArray",
									   " isStruct",
									   " hasType",		// 0x0000_0080
									   " hasLang",
									   " isQual",
									   " hasQual",
									   " ?3",			// 0x0000_0008
									   " ?2",
									   " URI",
									   " ?0" };

	if ( options == 0 ) {

		OutProcNChars ( "(0x0)", 5 );

	} else {

		OutProcNChars ( "(0x", 3 );
		OutProcHexInt ( options );
		OutProcNChars ( " :", 2 );

		XMP_OptionBits mask = 0x80000000;
		for ( int b = 0; b < 32; ++b ) {
			if ( options & mask ) OutProcLiteral ( optNames[b] );
			mask = mask >> 1;
		}
		OutProcNChars ( ")", 1 );

	}

}	// DumpNodeOptions


// -------------------------------------------------------------------------------------------------
// DumpPropertyTree
// ----------------

// *** Extract the validation code into a separate routine to call on exit in debug builds.

static void
DumpPropertyTree ( const XMP_Node *	  currNode,
				   int				  indent,
				   size_t			  itemIndex,
				   XMP_TextOutputProc outProc,
				   void *			  refCon )
{
	char buffer [32];	// Decimal of a 64 bit int is at most about 20 digits.

	OutProcIndent ( (size_t)indent );
	if ( itemIndex == 0 ) {
		if ( currNode->options & kXMP_PropIsQualifier ) OutProcNChars ( "? ", 2 );
		DumpClearString ( currNode->name, outProc, refCon );
	} else {
		OutProcNChars ( "[", 1 );
		OutProcDecInt ( itemIndex );
		OutProcNChars ( "]", 1 );
	}

	if ( ! (currNode->options & kXMP_PropCompositeMask) ) {
		OutProcNChars ( " = \"", 4 );
		DumpClearString ( currNode->value, outProc, refCon );
		OutProcNChars ( "\"", 1 );
	}

	if ( currNode->options != 0 ) {
		OutProcNChars ( "  ", 2 );
		DumpNodeOptions ( currNode->options, outProc, refCon );
	}

	if ( currNode->options & kXMP_PropHasLang ) {
		if ( currNode->qualifiers.empty() || (currNode->qualifiers[0]->name != "xml:lang") ) {
			OutProcLiteral ( "  ** bad lang flag **" );
		}
	}
	// *** Check rdf:type also.

	if ( ! (currNode->options & kXMP_PropCompositeMask) ) {
		if ( ! currNode->children.empty() ) OutProcLiteral ( "  ** bad children **" );
	} else if ( currNode->options & kXMP_PropValueIsArray ) {
		if ( currNode->options & kXMP_PropValueIsStruct ) OutProcLiteral ( "  ** bad comp flags **" );
	} else if ( (currNode->options & kXMP_PropCompositeMask) != kXMP_PropValueIsStruct ) {
		OutProcLiteral ( "  ** bad comp flags **" );
	}

	#if 0	// *** XMP_DebugBuild
		if ( (currNode->_namePtr != currNode->name.c_str()) ||
			 (currNode->_valuePtr != currNode->value.c_str()) ) OutProcLiteral ( "  ** bad debug string **" );
	#endif

	OutProcNewline();

	for ( size_t qualNum = 0, qualLim = currNode->qualifiers.size(); qualNum < qualLim; ++qualNum ) {

		const XMP_Node * currQual = currNode->qualifiers[qualNum];

		if ( currQual->parent != currNode ) OutProcLiteral ( "** bad parent link => " );
		if ( currQual->name == kXMP_ArrayItemName ) OutProcLiteral ( "** bad qual name => " );
		if ( ! (currQual->options & kXMP_PropIsQualifier) ) OutProcLiteral ( "** bad qual flag => " );
		if ( currQual->name == "xml:lang" ) {
			if ( (qualNum != 0) || (! (currNode->options & kXMP_PropHasLang)) ) OutProcLiteral ( "** bad lang qual => " );
		}

		DumpPropertyTree ( currQual, indent+2, 0, outProc, refCon );

	}

	for ( size_t childNum = 0, childLim = currNode->children.size(); childNum < childLim; ++childNum ) {

		const XMP_Node * currChild = currNode->children[childNum];

		if ( currChild->parent != currNode ) OutProcLiteral ( "** bad parent link => " );
		if ( currChild->options & kXMP_PropIsQualifier ) OutProcLiteral ( "** bad qual flag => " );

		if ( currNode->options & kXMP_PropValueIsArray ) {
			itemIndex = childNum+1;
			if ( currChild->name != kXMP_ArrayItemName ) OutProcLiteral ( "** bad item name => " );
		} else {
			itemIndex = 0;
			if ( currChild->name == kXMP_ArrayItemName ) OutProcLiteral ( "** bad field name => " );
		}

		DumpPropertyTree ( currChild, indent+1, itemIndex, outProc, refCon );

	}

}	// DumpPropertyTree


// -------------------------------------------------------------------------------------------------
// DumpXMLTree
// -----------

#if DumpXMLParseTree

static inline void PutHexByte ( FILE * log, unsigned char ch )
{

	fprintf ( log, "\\x" );
	if ( ch < 0x10 ) {
		fprintf ( log, "%c", kHexDigits[ch] );
	} else {
		fprintf ( log, "%c%c", kHexDigits[ch>>4], kHexDigits[ch&0xF] );
	}

}	// PutHexByte

// -------------------------------------------------------------------------------------------------

static void PutClearString ( FILE * log, const std::string & str )
{

	for ( size_t i = 0; i != str.size(); ++i ) {
		unsigned char ch = str[i];
		if ( (0x20 <= ch) && (ch <= 0x7F) ) {
			fprintf ( log, "%c", ch );
		} else {
			PutHexByte ( log, ch );
		}
	}

}	// PutClearString

// -------------------------------------------------------------------------------------------------

static void DumpXMLTree ( FILE * log, const XML_Node & node, int indent )
{
	size_t i;

	#if 0	// *** XMP_DebugBuild
		if ( (node._namePtr != node.name.c_str()) ||
			 (node._valuePtr != node.value.c_str()) ) fprintf ( log, "*** bad debug string ***\n" );
	#endif

	for ( i = 0; i != (size_t)indent; ++i ) fprintf ( log, "  " );

	switch ( node.kind ) {

		case kRootNode :
			fprintf ( log, "\nStart of XML tree dump\n\n" );
			if ( (indent != 0) || (! node.attrs.empty()) ||
				 (! node.ns.empty()) || (! node.name.empty()) || (!node.value.empty()) ) fprintf ( log, " ** invalid root ** \n" );
			for ( i = 0; i < node.children.size(); ++i ) {
				XMP_Uns8 kind = node.children[i]->kind;
				if ( (kind == kRootNode) || (kind == kAttrNode) ) fprintf ( log, " ** invalid child ** \n" );
				DumpXMLTree ( log, *node.children[i], indent+1 );
			}
			fprintf ( log, "\nEnd of XML tree dump\n" );
			break;

		case kElemNode :
			fprintf ( log, "Elem %s", node.name.c_str() );
			if ( indent == 0 ) fprintf ( log, " ** invalid elem ** " );
			if ( ! node.ns.empty() ) fprintf ( log, " @ %s", node.ns.c_str() );
			fprintf ( log, "\n" );
			for ( i = 0; i < node.attrs.size(); ++i ) {
				XMP_Uns8 kind = node.attrs[i]->kind;
				if ( kind != kAttrNode ) fprintf ( log, " ** invalid attr ** \n" );
				DumpXMLTree ( log, *node.attrs[i], indent+2 );
			}
			for ( i = 0; i < node.children.size(); ++i ) {
				XMP_Uns8 kind = node.children[i]->kind;
				if ( (kind == kRootNode) || (kind == kAttrNode) ) fprintf ( log, " ** invalid child ** \n" );
				DumpXMLTree ( log, *node.children[i], indent+1 );
			}
			break;

		case kAttrNode :
			fprintf ( log, "Attr %s", node.name.c_str() );
			if ( (indent == 0) || node.name.empty() || (! node.attrs.empty()) || (! node.children.empty()) ) fprintf ( log, " ** invalid attr ** " );
			fprintf ( log, " = \"" );
			PutClearString ( log, node.value );
			fprintf ( log, "\"" );
			if ( ! node.ns.empty() ) fprintf ( log, " @ %s", node.ns.c_str() );
			fprintf ( log, "\n" );
			break;

		case kCDataNode :
			if ( (indent == 0) || (! node.ns.empty()) || (! node.name.empty()) ||
				 (! node.attrs.empty()) || (! node.children.empty()) ) fprintf ( log, " ** invalid cdata ** \n" );
			fprintf ( log, "\"" );
			PutClearString ( log, node.value );
			fprintf ( log, "\"\n" );
			break;

		case kPINode :
			fprintf ( log, "PI %s", node.name.c_str() );
			if ( (indent == 0) || node.name.empty() || (! node.children.empty()) ) fprintf ( log, " ** invalid pi ** \n" );
			if ( ! node.value.empty() ) {
				fprintf ( log, " <? " );
				PutClearString ( log, node.value );
				fprintf ( log, " ?>" );
			}
			fprintf ( log, "\n" );
			break;

	}

}	// DumpXMLTree

#endif	// DumpXMLParseTree


// -------------------------------------------------------------------------------------------------
// CompareNodeNames
// ----------------
//
// Comparison routine for sorting XMP nodes by name. The name "xml:lang" is less than anything else,
// and "rdf:type" is less than anything except "xml:lang". This preserves special rules for qualifiers.

static bool
CompareNodeNames ( XMP_Node * left, XMP_Node * right )
{

	if ( left->name  == "xml:lang" ) return true;
	if ( right->name == "xml:lang" ) return false;

	if ( left->name  == "rdf:type" ) return true;
	if ( right->name == "rdf:type" ) return false;

	return ( left->name < right->name );

}	// CompareNodeNames


// -------------------------------------------------------------------------------------------------
// CompareNodeValues
// -----------------
//
// Comparison routine for sorting XMP nodes by value.

static bool
CompareNodeValues ( XMP_Node * left, XMP_Node * right )
{

	if ( XMP_PropIsSimple ( left->options ) && XMP_PropIsSimple ( right->options ) ) {
		return ( left->value < right->value );
	}

	XMP_OptionBits leftForm  = left->options & kXMP_PropCompositeMask;
	XMP_OptionBits rightForm = right->options & kXMP_PropCompositeMask;

	return ( leftForm < rightForm );

}	// CompareNodeValues


// -------------------------------------------------------------------------------------------------
// CompareNodeLangs
// ----------------
//
// Comparison routine for sorting XMP nodes by xml:lang qualifier. An "x-default" value is less than
// any other language.

static bool
CompareNodeLangs ( XMP_Node * left, XMP_Node * right )
{

	if ( left->qualifiers.empty()  || (left->qualifiers[0]->name  != "xml:lang") ) return false;
	if ( right->qualifiers.empty() || (right->qualifiers[0]->name != "xml:lang") ) return false;

	if ( left->qualifiers[0]->value  == "x-default" ) return true;
	if ( right->qualifiers[0]->value == "x-default" ) return false;

	return ( left->qualifiers[0]->value < right->qualifiers[0]->value );

}	// CompareNodeLangs


// -------------------------------------------------------------------------------------------------
// SortWithinOffspring
// -------------------
//
// Sort one level down, within the elements of a node vector. This sorts the qualifiers of each
// node. If the node is a struct it sorts the fields by names. If the node is an unordered array it
// sorts the elements by value. If the node is an AltText array it sorts the elements by language.

static void
SortWithinOffspring ( XMP_NodeOffspring & nodeVec )
{

	for ( size_t i = 0, limit = nodeVec.size(); i < limit; ++i ) {

		XMP_Node * currPos = nodeVec[i];

		if ( ! currPos->qualifiers.empty() ) {
			sort ( currPos->qualifiers.begin(), currPos->qualifiers.end(), CompareNodeNames );
			SortWithinOffspring ( currPos->qualifiers );
		}

		if ( ! currPos->children.empty() ) {

			if ( XMP_PropIsStruct ( currPos->options ) || XMP_NodeIsSchema ( currPos->options ) ) {
				sort ( currPos->children.begin(), currPos->children.end(), CompareNodeNames );
			} else if ( XMP_PropIsArray ( currPos->options ) ) {
				if ( XMP_ArrayIsUnordered ( currPos->options ) ) {
					stable_sort ( currPos->children.begin(), currPos->children.end(), CompareNodeValues );
				} else if ( XMP_ArrayIsAltText ( currPos->options ) ) {
					sort ( currPos->children.begin(), currPos->children.end(), CompareNodeLangs );
				}
			}

			SortWithinOffspring ( currPos->children );

		}

	}

}	// SortWithinOffspring


// -------------------------------------------------------------------------------------------------
// RegisterAlias
// -------------
//
// Allow 3 kinds of alias:
//	TopProp => TopProp
//	TopProp => TopArray[1]
//	TopProp => TopArray[@xml:lang='x-default']
//
// A new alias can be made to something that is already aliased, as long as the net result is one of
// the legitimate forms. The new alias can already have aliases to it, also as long as result of
// adjusting all of the exiting aliases leaves them legal.
//
// ! The caller assumes all risk that new aliases do not invalidate existing XMPMeta objects. Any
// ! conflicts will result in later references throwing bad XPath exceptions.

static void
RegisterAlias ( XMP_StringPtr  aliasNS,
				XMP_StringPtr  aliasProp,
				XMP_StringPtr  actualNS,
				XMP_StringPtr  actualProp,
				XMP_OptionBits arrayForm )
{
	XMP_ExpandedXPath	expAlias, expActual;
	XMP_AliasMapPos		mapPos;
	XMP_ExpandedXPath *	regActual = 0;

	XMP_Assert ( (aliasNS != 0) && (aliasProp != 0) && (actualNS != 0) && (actualProp != 0) );	// Enforced by wrapper.

	// Expand the alias and actual names, make sure they are one of the basic 3 forms. When counting
	// the expanded XPath size remember that the schema URI is the first component. We don't have to
	// compare the schema URIs though, the (unique) prefix is part of the top property name.

	ExpandXPath ( aliasNS, aliasProp, &expAlias );
	ExpandXPath ( actualNS, actualProp, &expActual );
	if ( (expAlias.size() != 2) || (expActual.size() != 2) ) {
		XMP_Throw ( "Alias and actual property names must be simple", kXMPErr_BadXPath );
	}

	arrayForm = VerifySetOptions ( arrayForm, 0 );
	if ( arrayForm != 0 ) {
		if ( (arrayForm & ~kXMP_PropArrayFormMask) != 0 ) XMP_Throw ( "Only array form flags are allowed", kXMPErr_BadOptions );
		expActual[1].options |= arrayForm;	// Set the array form for the top level step.
		if ( ! (arrayForm & kXMP_PropArrayIsAltText) ) {
			expActual.push_back ( XPathStepInfo ( "[1]", kXMP_ArrayIndexStep ) );
		} else {
			expActual.push_back ( XPathStepInfo ( "[?xml:lang=\"x-default\"]", kXMP_QualSelectorStep ) );
		}
	}

	// See if there are any conflicts with existing aliases. A couple of the checks are easy. If the
	// alias is already aliased it is only OK to reregister an identical alias. If the actual is
	// already aliased to something else and the new chain is legal, just swap in the old base.

	mapPos = sRegisteredAliasMap->find ( expAlias[kRootPropStep].step );
	if ( mapPos != sRegisteredAliasMap->end() ) {

		// This alias is already registered to something, make sure it is the same something.

		regActual = &mapPos->second;
		if ( arrayForm != (mapPos->second[1].options & kXMP_PropArrayFormMask) ) {
			XMP_Throw ( "Mismatch with existing alias array form", kXMPErr_BadParam );
		}
		if ( expActual.size() != regActual->size() ) {
			XMP_Throw ( "Mismatch with existing actual path", kXMPErr_BadParam );
		}
		if ( expActual[kRootPropStep].step != (*regActual)[kRootPropStep].step ) {
			XMP_Throw ( "Mismatch with existing actual name", kXMPErr_BadParam );
		}
		if ( (expActual.size() == 3) && (expActual[kAliasIndexStep].step != (*regActual)[kAliasIndexStep].step) ) {
			XMP_Throw ( "Mismatch with existing actual array item", kXMPErr_BadParam );
		}
		return;

	}

	mapPos = sRegisteredAliasMap->find ( expActual[kRootPropStep].step );
	if ( mapPos != sRegisteredAliasMap->end() ) {

		// The actual is already aliased to something else.

		regActual = &mapPos->second;
		if ( expActual.size() == 2 ) {
			expActual = *regActual;	// TopProp => TopProp => anything : substitute the entire old base.
		} else if ( regActual->size() != 2 ) {
			XMP_Throw ( "Can't alias an array item to an array item", kXMPErr_BadParam );	// TopProp => TopArray[] => TopArray[] : nope.
		} else {
			expActual[kSchemaStep].step = (*regActual)[kSchemaStep].step;		// TopProp => TopArray[] => TopProp :
			expActual[kRootPropStep].step   = (*regActual)[kRootPropStep].step;	//	substitute the old base name.
		}

	}

	// Checking for existing aliases to this one is touchier. This involves updating the alias map,
	// which must not be done unless all of the changes are legal. So we need 2 loops, one to verify
	// that everything is OK, and one to make the changes. The bad case is:
	//	TopProp => TopArray[] => TopArray[]
	// In the valid cases we back substitute the new base.

	for ( mapPos = sRegisteredAliasMap->begin(); mapPos != sRegisteredAliasMap->end(); ++mapPos ) {
		regActual = &mapPos->second;
		if ( expAlias[kRootPropStep].step == (*regActual)[kRootPropStep].step ) {
			if ( (regActual->size() == 2) && (expAlias.size() == 2) ) {
				XMP_Throw ( "Can't alias an array item to an array item", kXMPErr_BadParam );
			}
		}
	}

	for ( mapPos = sRegisteredAliasMap->begin(); mapPos != sRegisteredAliasMap->end(); ++mapPos ) {
		regActual = &mapPos->second;
		if ( expAlias[kRootPropStep].step == (*regActual)[kRootPropStep].step ) {

			if ( regActual->size() == 1 ) {
				*regActual = expActual;	// TopProp => TopProp => anything : substitute the entire new base.
			} else {
				(*regActual)[kSchemaStep].step = expActual[kSchemaStep].step;	// TopProp => TopArray[] => TopProp :
				(*regActual)[kRootPropStep].step   = expActual[kRootPropStep].step;		//	substitute the new base name.
			}

		}
	}

	// Finally, all is OK to register the new alias.

	(void) sRegisteredAliasMap->insert ( XMP_AliasMap::value_type ( expAlias[kRootPropStep].step, expActual ) );

}	// RegisterAlias


// -------------------------------------------------------------------------------------------------
// RegisterStandardAliases
// -----------------------

static void
RegisterStandardAliases()
{

	// Aliases from XMP to DC.
	RegisterAlias ( kXMP_NS_XMP, "Author",       kXMP_NS_DC, "creator", kXMP_PropArrayIsOrdered );
	RegisterAlias ( kXMP_NS_XMP, "Authors",      kXMP_NS_DC, "creator", 0 );
	RegisterAlias ( kXMP_NS_XMP, "Description",  kXMP_NS_DC, "description", 0 );
	RegisterAlias ( kXMP_NS_XMP, "Format",       kXMP_NS_DC, "format", 0 );
	RegisterAlias ( kXMP_NS_XMP, "Keywords",     kXMP_NS_DC, "subject", 0 );
	RegisterAlias ( kXMP_NS_XMP, "Locale",       kXMP_NS_DC, "language", 0 );
	RegisterAlias ( kXMP_NS_XMP, "Title",        kXMP_NS_DC, "title", 0 );
	RegisterAlias ( kXMP_NS_XMP_Rights, "Copyright", kXMP_NS_DC, "rights", 0 );

	// Aliases from PDF to DC and XMP.
	RegisterAlias ( kXMP_NS_PDF, "Author",       kXMP_NS_DC, "creator", kXMP_PropArrayIsOrdered );
	RegisterAlias ( kXMP_NS_PDF, "BaseURL",      kXMP_NS_XMP, "BaseURL", 0 );
	RegisterAlias ( kXMP_NS_PDF, "CreationDate", kXMP_NS_XMP, "CreateDate", 0 );
	RegisterAlias ( kXMP_NS_PDF, "Creator",      kXMP_NS_XMP, "CreatorTool", 0 );
	RegisterAlias ( kXMP_NS_PDF, "ModDate",      kXMP_NS_XMP, "ModifyDate", 0 );
	RegisterAlias ( kXMP_NS_PDF, "Subject",      kXMP_NS_DC,  "description", kXMP_PropArrayIsAltText );
	RegisterAlias ( kXMP_NS_PDF, "Title",        kXMP_NS_DC,  "title", kXMP_PropArrayIsAltText );

	// Aliases from Photoshop to DC and XMP.
	RegisterAlias ( kXMP_NS_Photoshop, "Author",       kXMP_NS_DC, "creator", kXMP_PropArrayIsOrdered );
	RegisterAlias ( kXMP_NS_Photoshop, "Caption",      kXMP_NS_DC, "description", kXMP_PropArrayIsAltText );
	RegisterAlias ( kXMP_NS_Photoshop, "Copyright",    kXMP_NS_DC, "rights", kXMP_PropArrayIsAltText );
	RegisterAlias ( kXMP_NS_Photoshop, "Keywords",     kXMP_NS_DC, "subject", 0 );
	RegisterAlias ( kXMP_NS_Photoshop, "Marked",       kXMP_NS_XMP_Rights, "Marked", 0 );
	RegisterAlias ( kXMP_NS_Photoshop, "Title",        kXMP_NS_DC, "title", kXMP_PropArrayIsAltText );
	RegisterAlias ( kXMP_NS_Photoshop, "WebStatement", kXMP_NS_XMP_Rights, "WebStatement", 0 );

	// Aliases from TIFF and EXIF to DC and XMP.
	RegisterAlias ( kXMP_NS_TIFF, "Artist",            kXMP_NS_DC,  "creator", kXMP_PropArrayIsOrdered);
	RegisterAlias ( kXMP_NS_TIFF, "Copyright",         kXMP_NS_DC,  "rights", 0 );
	RegisterAlias ( kXMP_NS_TIFF, "DateTime",          kXMP_NS_XMP, "ModifyDate", 0 );
	RegisterAlias ( kXMP_NS_EXIF, "DateTimeDigitized", kXMP_NS_XMP, "CreateDate", 0 );
	RegisterAlias ( kXMP_NS_TIFF, "ImageDescription",  kXMP_NS_DC,  "description", kXMP_PropArrayIsAltText );
	RegisterAlias ( kXMP_NS_TIFF, "Software",          kXMP_NS_XMP, "CreatorTool", 0 );

	// Aliases from PNG to DC and XMP.
	RegisterAlias ( kXMP_NS_PNG, "Author",           kXMP_NS_DC,  "creator", kXMP_PropArrayIsOrdered);
	RegisterAlias ( kXMP_NS_PNG, "Copyright",        kXMP_NS_DC,  "rights", kXMP_PropArrayIsAltText);
	RegisterAlias ( kXMP_NS_PNG, "CreationTime",     kXMP_NS_XMP, "CreateDate", 0 );
	RegisterAlias ( kXMP_NS_PNG, "Description",      kXMP_NS_DC,  "description", kXMP_PropArrayIsAltText);
	RegisterAlias ( kXMP_NS_PNG, "ModificationTime", kXMP_NS_XMP, "ModifyDate", 0 );
	RegisterAlias ( kXMP_NS_PNG, "Software",         kXMP_NS_XMP, "CreatorTool", 0 );
	RegisterAlias ( kXMP_NS_PNG, "Title",            kXMP_NS_DC,  "title", kXMP_PropArrayIsAltText);
	

}	// RegisterStandardAliases


// =================================================================================================
// Constructors
// ============


XMPMeta::XMPMeta() : tree(XMP_Node(0,"",0)), clientRefs(0), xmlParser(0)
{
	#if XMP_TraceCTorDTor
		printf ( "Default construct XMPMeta @ %.8X\n", this );
	#endif

	if ( sDefaultErrorCallback.clientProc != 0 ) {
		this->errorCallback.wrapperProc = sDefaultErrorCallback.wrapperProc;
		this->errorCallback.clientProc = sDefaultErrorCallback.clientProc;
		this->errorCallback.context = sDefaultErrorCallback.context;
		this->errorCallback.limit = sDefaultErrorCallback.limit;
	}

}	// XMPMeta

// -------------------------------------------------------------------------------------------------

XMPMeta::~XMPMeta() RELEASE_NO_THROW
{
	#if XMP_TraceCTorDTor
		printf ( "Destruct XMPMeta @ %.8X\n", this );
	#endif

	XMP_Assert ( this->clientRefs <= 0 );
	if ( xmlParser != 0 ) delete ( xmlParser );
	xmlParser = 0;

}	// ~XMPMeta


// =================================================================================================
// Class Static Functions
// ======================
//
//
// =================================================================================================

// -------------------------------------------------------------------------------------------------
// GetVersionInfo
// --------------

/* class-static */ void
XMPMeta::GetVersionInfo ( XMP_VersionInfo * info )
{

	memset ( info, 0, sizeof(*info) );	// AUDIT: Safe, using sizeof the destination.
	XMP_Assert ( sizeof(*info) == sizeof(XMP_VersionInfo) );

	info->major   = XMPCORE_API_VERSION_MAJOR;
	info->minor   = XMPCORE_API_VERSION_MINOR;
	info->micro   = 0; //no longer used
	info->isDebug = kXMPCore_DebugFlag;
	info->flags   = 0;	// ! None defined yet.
	info->message = kXMPCore_VersionMessage;

}	// GetVersionInfo

// -------------------------------------------------------------------------------------------------
// Initialize
// ----------

#if XMP_TraceCoreCalls
	FILE * xmpCoreLog = stderr;
#endif

#if UseGlobalLibraryLock
	XMP_BasicMutex sLibraryLock;
#endif

/* class-static */ bool
XMPMeta::Initialize()
{
	// Allocate and initialize static objects.

	++sXMP_InitCount;
	if ( sXMP_InitCount > 1 ) return true;

	#if XMP_TraceCoreCallsToFile
		xmpCoreLog = fopen ( "XMPCoreLog.txt", "w" );
		if ( xmpCoreLog == 0 ) xmpCoreLog = stderr;
	#endif

	#if UseGlobalLibraryLock
		InitializeBasicMutex ( sLibraryLock );
	#endif

	if ( ! Initialize_LibUtils() ) return false;

	#if ENABLE_CPP_DOM_MODEL
		try {
			AdobeXMPCore_Int::InitializeXMPCommonFramework();
	
		} catch ( ... ) {
			return false;
		}
		AdobeXMPCore_Int::INameSpacePrefixMap_I::CreateDefaultNameSpacePrefixMap();
		sDefaultNamespacePrefixMapLock = new XMP_ReadWriteLock;

		// Explicitly setting sUseNewCoreAPIs as false (default value)
		sUseNewCoreAPIs = false;
	#endif
	
	xdefaultName = new XMP_VarString ( "x-default" );

	sRegisteredNamespaces = new XMP_NamespaceTable;
	sRegisteredAliasMap   = new XMP_AliasMap;
	InitializeUnicodeConversions();


	// Register standard namespaces and aliases.

	XMP_StringPtr	voidPtr;
	XMP_StringLen	voidLen;

	(void) RegisterNamespace ( kXMP_NS_XML, "xml", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_RDF, "rdf", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_DC, "dc", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_XMP, "xmp", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDF, "pdf", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_Photoshop, "photoshop", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PSAlbum, "album", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_EXIF, "exif", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_EXIF_Aux, "aux", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_ExifEX, "exifEX", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_TIFF, "tiff", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PNG, "png", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_JPEG, "jpeg", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_JP2K, "jp2k", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_CameraRaw, "crs", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_ASF, "asf", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_WAV, "wav", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_AdobeStockPhoto, "bmsp", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_CreatorAtom, "creatorAtom", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_XMP_Rights, "xmpRights", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_MM, "xmpMM", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_BJ, "xmpBJ", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_Note, "xmpNote", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_DM, "xmpDM", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_Script, "xmpScript", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_BWF, "bext", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_AEScart, "AEScart", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_RIFFINFO, "riffinfo", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_Text, "xmpT", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_PagedFile, "xmpTPg", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_Graphics, "xmpG", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_Image, "xmpGImg", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_XMP_Font, "stFnt", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_Dimensions, "stDim", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_ResourceEvent, "stEvt", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_ResourceRef, "stRef", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_ST_Version, "stVer", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_ST_Job, "stJob", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_XMP_ManifestItem, "stMfs", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_XMP_IdentifierQual, "xmpidq", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_IPTCCore, "Iptc4xmpCore", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_IPTCExt, "Iptc4xmpExt", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_DICOM, "DICOM", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PLUS, "plus", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_PDFA_Schema, "pdfaSchema", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFA_Property, "pdfaProperty", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFA_Type, "pdfaType", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFA_Field, "pdfaField", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFA_ID, "pdfaid", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFA_Extension, "pdfaExtension", &voidPtr, &voidLen );

	(void) RegisterNamespace ( kXMP_NS_PDFX, "pdfx", &voidPtr, &voidLen );
	(void) RegisterNamespace ( kXMP_NS_PDFX_ID, "pdfxid", &voidPtr, &voidLen );

	(void) RegisterNamespace ( "adobe:ns:meta/", "x", &voidPtr, &voidLen );
	(void) RegisterNamespace ( "http://ns.adobe.com/iX/1.0/", "iX", &voidPtr, &voidLen );

	(void) RegisterNamespace( kXMP_NS_iXML, "iXML", &voidPtr, &voidLen );

	(void)RegisterNamespace(kXMP_NS_MicrosoftPhoto, "MicrosoftPhoto", &voidPtr, &voidLen);

	RegisterStandardAliases();

	// Initialize the other core classes.

	if ( ! XMPIterator::Initialize() ) XMP_Throw ( "Failure from XMPIterator::Initialize", kXMPErr_InternalFailure );
	if ( ! XMPUtils::Initialize() ) XMP_Throw ( "Failure from XMPUtils::Initialize", kXMPErr_InternalFailure );
	// Do miscelaneous semantic checks of types and arithmetic.

	XMP_Assert ( sizeof(XMP_Int8) == 1 );
	XMP_Assert ( sizeof(XMP_Int16) == 2 );
	XMP_Assert ( sizeof(XMP_Int32) == 4 );
	XMP_Assert ( sizeof(XMP_Int64) == 8 );
	XMP_Assert ( sizeof(XMP_Uns8) == 1 );
	XMP_Assert ( sizeof(XMP_Uns16) == 2 );
	XMP_Assert ( sizeof(XMP_Uns32) == 4 );
	XMP_Assert ( sizeof(XMP_Uns64) == 8 );
	XMP_Assert ( sizeof(XMP_Bool) == 1 );

	XMP_Assert ( sizeof(XMP_OptionBits) == 4 );	// Check that option masking work on all 32 bits.
	XMP_OptionBits flag = (XMP_OptionBits) (~0UL);
	XMP_Assert ( flag == (XMP_OptionBits)(-1L) );
	XMP_Assert ( (flag ^ kXMP_PropHasLang) == 0xFFFFFFBFUL );
	XMP_Assert ( (flag & ~kXMP_PropHasLang) == 0xFFFFFFBFUL );

	XMP_OptionBits opt1 = 0;	// Check the general option bit macros.
	XMP_OptionBits opt2 = (XMP_OptionBits)~0UL;
	XMP_SetOption ( opt1, kXMP_PropValueIsArray );
	XMP_ClearOption ( opt2, kXMP_PropValueIsArray );
	XMP_Assert ( opt1 == ~opt2 );
	XMP_Assert ( XMP_TestOption ( opt1, kXMP_PropValueIsArray ) );
	XMP_Assert ( ! XMP_TestOption ( opt2, kXMP_PropValueIsArray ) );

	XMP_Assert ( XMP_PropIsSimple ( ~kXMP_PropCompositeMask ) );	// Check the special option bit macros.
	XMP_Assert ( ! XMP_PropIsSimple ( kXMP_PropValueIsStruct ) );
	XMP_Assert ( ! XMP_PropIsSimple ( kXMP_PropValueIsArray ) );

	XMP_Assert ( XMP_PropIsStruct ( kXMP_PropValueIsStruct ) );
	XMP_Assert ( XMP_PropIsArray  ( kXMP_PropValueIsArray ) );
	XMP_Assert ( ! XMP_PropIsStruct ( ~kXMP_PropValueIsStruct ) );
	XMP_Assert ( ! XMP_PropIsArray  ( ~kXMP_PropValueIsArray ) );

	XMP_Assert ( XMP_ArrayIsUnordered ( ~kXMP_PropArrayIsOrdered ) );
	XMP_Assert ( XMP_ArrayIsOrdered   ( kXMP_PropArrayIsOrdered ) );
	XMP_Assert ( XMP_ArrayIsAlternate ( kXMP_PropArrayIsAlternate ) );
	XMP_Assert ( XMP_ArrayIsAltText   ( kXMP_PropArrayIsAltText ) );
	XMP_Assert ( ! XMP_ArrayIsUnordered ( kXMP_PropArrayIsOrdered ) );
	XMP_Assert ( ! XMP_ArrayIsOrdered   ( ~kXMP_PropArrayIsOrdered ) );
	XMP_Assert ( ! XMP_ArrayIsAlternate ( ~kXMP_PropArrayIsAlternate ) );
	XMP_Assert ( ! XMP_ArrayIsAltText   ( ~kXMP_PropArrayIsAltText ) );

	XMP_Assert ( XMP_PropHasQualifiers ( kXMP_PropHasQualifiers ) );
	XMP_Assert ( XMP_PropIsQualifier   ( kXMP_PropIsQualifier ) );
	XMP_Assert ( XMP_PropHasLang       ( kXMP_PropHasLang ) );
	XMP_Assert ( ! XMP_PropHasQualifiers ( ~kXMP_PropHasQualifiers ) );
	XMP_Assert ( ! XMP_PropIsQualifier   ( ~kXMP_PropIsQualifier ) );
	XMP_Assert ( ! XMP_PropHasLang       ( ~kXMP_PropHasLang ) );

	XMP_Assert ( XMP_NodeIsSchema ( kXMP_SchemaNode ) );
	XMP_Assert ( XMP_PropIsAlias  ( kXMP_PropIsAlias ) );
	XMP_Assert ( ! XMP_NodeIsSchema ( ~kXMP_SchemaNode ) );
	XMP_Assert ( ! XMP_PropIsAlias  ( ~kXMP_PropIsAlias ) );

	#if 0	// Generally off, enable to hand check generated code.
		extern XMP_OptionBits opt3, opt4;
		if ( XMP_TestOption ( opt3, kXMP_PropValueIsArray ) ) opt4 = opt3;
		if ( ! XMP_TestOption ( opt3, kXMP_PropValueIsStruct ) ) opt4 = opt3;
		static bool ok1 = XMP_TestOption ( opt4, kXMP_PropValueIsArray );
		static bool ok2 = ! XMP_TestOption ( opt4, kXMP_PropValueIsStruct );
	#endif

	// Make sure the embedded info strings are referenced and kept.
	if ( (kXMPCore_EmbeddedVersion[0] == 0) || (kXMPCore_EmbeddedCopyright[0] == 0) ) return false;
	return true;

}	// Initialize


// -------------------------------------------------------------------------------------------------
// Terminate
// ---------

/* class-static */ void
XMPMeta::Terminate() RELEASE_NO_THROW
{
	--sXMP_InitCount;
	if ( sXMP_InitCount != 0 ) return;	// Not ready to terminate, or already terminated.

	XMPIterator::Terminate();
	XMPUtils::Terminate();
#if ENABLE_CPP_DOM_MODEL
		AdobeXMPCore_Int::INameSpacePrefixMap_I::DestroyDefaultNameSapcePrefixMap();
		AdobeXMPCore_Int::IDOMImplementationRegistry_I::DestoryDOMImplementationRegistry();
		AdobeXMPCore_Int::ICoreObjectFactory_I::DestroyCoreObjectFactory();
		AdobeXMPCore_Int::ICoreConfigurationManager_I::DestroyCoreConfigurationManager();
		AdobeXMPCore_Int::TerminateXMPCommonFramework();
		EliminateGlobal( sDefaultNamespacePrefixMapLock );
		// Explicitly setting sUseNewCoreAPIs as false (default value)
		sUseNewCoreAPIs = false;
		#if !XMP_StaticBuild
			EliminateGlobal( sInternalClientAllocator );
		#endif
#endif


	EliminateGlobal ( sRegisteredNamespaces );
	EliminateGlobal ( sRegisteredAliasMap );

	EliminateGlobal ( xdefaultName );

	Terminate_LibUtils();

	#if UseGlobalLibraryLock
		TerminateBasicMutex ( sLibraryLock );
	#endif

	#if XMP_TraceCoreCallsToFile
		if ( xmpCoreLog != stderr ) fclose ( xmpCoreLog );
		xmpCoreLog = stderr;
	#endif

	// reset static variables
	sDefaultErrorCallback.Clear();
}	// Terminate


// -------------------------------------------------------------------------------------------------
// DumpNamespaces
// --------------
//
// Dump the prefix to URI map (easier to read) and verify that both are consistent and legit.

// *** Should put checks in a separate routine for regular calling in debug builds.

/* class-static */ XMP_Status
XMPMeta::DumpNamespaces ( XMP_TextOutputProc outProc,
                          void *             refCon )
{

	sRegisteredNamespaces->Dump ( outProc, refCon );
	return 0;

}	// DumpNamespaces


// -------------------------------------------------------------------------------------------------
// GetGlobalOptions
// ----------------

/* class-static */ XMP_OptionBits
XMPMeta::GetGlobalOptions()
{
	XMP_OptionBits	options = 0;

	return options;

}	// GetGlobalOptions


// -------------------------------------------------------------------------------------------------
// SetGlobalOptions
// ----------------

/* class-static */ void
XMPMeta::SetGlobalOptions ( XMP_OptionBits options )
{

	void * p; p = &options;	// Avoid unused param warnings.
	XMP_Throw("Unimplemented method XMPMeta::SetGlobalOptions", kXMPErr_Unimplemented);

}	// SetGlobalOptions


// -------------------------------------------------------------------------------------------------
// RegisterNamespace
// -----------------

/* class-static */ bool
XMPMeta::RegisterNamespace ( XMP_StringPtr	 namespaceURI,
							 XMP_StringPtr	 suggestedPrefix,
							 XMP_StringPtr * registeredPrefix,
							 XMP_StringLen * prefixSize )
{

	bool returnValue = sRegisteredNamespaces->Define ( namespaceURI, suggestedPrefix, registeredPrefix, prefixSize );
#if ENABLE_CPP_DOM_MODEL
	const char * prefix = NULL;
	XMP_StringLen len = 0;
	sRegisteredNamespaces->GetPrefix( namespaceURI, &prefix, &len );
	XMP_VarString prefixWithoutColon( prefix, len - 1 );
	{
		XMP_AutoLock aLock( sDefaultNamespacePrefixMapLock, true );
		AdobeXMPCore_Int::INameSpacePrefixMap_I::InsertInDefaultNameSpacePrefixMap( prefixWithoutColon.c_str(), prefixWithoutColon.size(), namespaceURI, AdobeXMPCommon::npos );
	}
#endif
	return returnValue;
}	// RegisterNamespace


// -------------------------------------------------------------------------------------------------
// GetNamespacePrefix
// ------------------

/* class-static */ bool
XMPMeta::GetNamespacePrefix	( XMP_StringPtr   namespaceURI,
							  XMP_StringPtr * namespacePrefix,
							  XMP_StringLen * prefixSize )
{

	return sRegisteredNamespaces->GetPrefix ( namespaceURI, namespacePrefix, prefixSize );

}	// GetNamespacePrefix


// -------------------------------------------------------------------------------------------------
// GetNamespaceURI
// ---------------

/* class-static */ bool
XMPMeta::GetNamespaceURI ( XMP_StringPtr   namespacePrefix,
						   XMP_StringPtr * namespaceURI,
						   XMP_StringLen * uriSize )
{

	return sRegisteredNamespaces->GetURI ( namespacePrefix, namespaceURI, uriSize );

}	// GetNamespaceURI


// -------------------------------------------------------------------------------------------------
// DeleteNamespace
// ---------------

// *** Don't allow standard namespaces to be deleted.
// *** We would be better off not having this. Instead, have local namespaces from parsing be
// *** restricted to the object that introduced them.

/* class-static */ void
XMPMeta::DeleteNamespace ( XMP_StringPtr namespaceURI )
{

	XMP_Throw ( "Unimplemented method XMPMeta::DeleteNamespace", kXMPErr_Unimplemented );

}	// DeleteNamespace


// =================================================================================================
// Class Methods
// =============
//
//
// =================================================================================================


// -------------------------------------------------------------------------------------------------
// DumpObject
// ----------

void
XMPMeta::DumpObject ( XMP_TextOutputProc outProc,
                      void *             refCon ) const
{
	XMP_Assert ( outProc != 0 );	// ! Enforced by wrapper.

	OutProcLiteral ( "Dumping XMPMeta object \"" );
	DumpClearString ( tree.name, outProc, refCon );
	OutProcNChars ( "\"  ", 3 );
	DumpNodeOptions ( tree.options, outProc, refCon );
	#if 0	// *** XMP_DebugBuild
		if ( (tree._namePtr != tree.name.c_str()) ||
			 (tree._valuePtr != tree.value.c_str()) ) OutProcLiteral ( "  ** bad debug string **" );
	#endif
	OutProcNewline();

	if ( ! tree.value.empty() ) {
		OutProcLiteral ( "** bad root value **  \"" );
		DumpClearString ( tree.value, outProc, refCon );
		OutProcNChars ( "\"", 1 );
		OutProcNewline();
	}

	if ( ! tree.qualifiers.empty() ) {
		OutProcLiteral ( "** bad root qualifiers **" );
		OutProcNewline();
		for ( size_t qualNum = 0, qualLim = tree.qualifiers.size(); qualNum < qualLim; ++qualNum ) {
			DumpPropertyTree ( tree.qualifiers[qualNum], 3, 0, outProc, refCon );
		}
	}

	if ( ! tree.children.empty() ) {

		for ( size_t childNum = 0, childLim = tree.children.size(); childNum < childLim; ++childNum ) {

			const XMP_Node * currSchema = tree.children[childNum];

			OutProcNewline();
			OutProcIndent ( 1 );
			DumpClearString ( currSchema->value, outProc, refCon );
			OutProcNChars ( "  ", 2 );
			DumpClearString ( currSchema->name, outProc, refCon );
			OutProcNChars ( "  ", 2 );
			DumpNodeOptions ( currSchema->options, outProc, refCon );
			#if 0	// *** XMP_DebugBuild
				if ( (currSchema->_namePtr != currSchema->name.c_str()) ||
					 (currSchema->_valuePtr != currSchema->value.c_str()) ) OutProcLiteral ( "  ** bad debug string **" );
			#endif
			OutProcNewline();

			if ( ! (currSchema->options & kXMP_SchemaNode) ) {
				OutProcLiteral ( "** bad schema options **" );
				OutProcNewline();
			}

			if ( ! currSchema->qualifiers.empty() ) {
				OutProcLiteral ( "** bad schema qualifiers **" );
				OutProcNewline();
				for ( size_t qualNum = 0, qualLim = currSchema->qualifiers.size(); qualNum < qualLim; ++qualNum ) {
					DumpPropertyTree ( currSchema->qualifiers[qualNum], 3, 0, outProc, refCon );
				}
			}

			for ( size_t numChild = 0, childLimit = currSchema->children.size(); numChild < childLimit; ++numChild ) {
				DumpPropertyTree ( currSchema->children[numChild], 2, 0, outProc, refCon );
			}

		}

	}

}	// DumpObject


// -------------------------------------------------------------------------------------------------
// CountArrayItems
// ---------------

XMP_Index
XMPMeta::CountArrayItems ( XMP_StringPtr schemaNS,
						   XMP_StringPtr arrayName ) const
{
	XMP_Assert ( (schemaNS != 0) && (arrayName != 0) );	// Enforced by wrapper.

	XMP_ExpandedXPath	expPath;
	ExpandXPath ( schemaNS, arrayName, &expPath );

	const XMP_Node * arrayNode = FindConstNode ( &tree, expPath );

	if ( arrayNode == 0 ) return 0;
	if ( ! (arrayNode->options & kXMP_PropValueIsArray) ) XMP_Throw ( "The named property is not an array", kXMPErr_BadXPath );
	return static_cast<XMP_Index>( arrayNode->children.size() );

}	// CountArrayItems


// -------------------------------------------------------------------------------------------------
// GetObjectName
// -------------

void
XMPMeta::GetObjectName ( XMP_StringPtr * namePtr,
						 XMP_StringLen * nameLen ) const
{

	*namePtr = tree.name.c_str();
	*nameLen = static_cast<XMP_Index>( tree.name.size() );

}	// GetObjectName


// -------------------------------------------------------------------------------------------------
// SetObjectName
// -------------

void
XMPMeta::SetObjectName ( XMP_StringPtr name )
{
	VerifyUTF8 ( name );	// Throws if the string is not legit UTF-8.
	tree.name = name;

}	// SetObjectName


// -------------------------------------------------------------------------------------------------
// GetObjectOptions
// ----------------

XMP_OptionBits
XMPMeta::GetObjectOptions() const
{
	XMP_OptionBits	options	= 0;

	return options;

}	// GetObjectOptions


// -------------------------------------------------------------------------------------------------
// SetObjectOptions
// ----------------

void
XMPMeta::SetObjectOptions ( XMP_OptionBits options )
{
	void * p; p = &options;	// Avoid unused param warnings.
	XMP_Throw ( "Unimplemented method XMPMeta::SetObjectOptions", kXMPErr_Unimplemented );
		

}	// SetObjectOptions


// -------------------------------------------------------------------------------------------------
// Sort
// ----
//
// At the top level the namespaces are sorted by their prefixes. Within a namespace, the top level
// properties are sorted by name. Within a struct, the fields are sorted by their qualified name,
// i.e. their XML prefix:local form. Unordered arrays of simple items are sorted by value. Language
// Alternative arrays are sorted by the xml:lang qualifiers, with the "x-default" item placed first.

void
XMPMeta::Sort()
{

	if ( ! this->tree.qualifiers.empty() ) {
		sort ( this->tree.qualifiers.begin(), this->tree.qualifiers.end(), CompareNodeNames );
		SortWithinOffspring ( this->tree.qualifiers );
	}

	if ( ! this->tree.children.empty() ) {
		// The schema prefixes are the node's value, the name is the URI, so we sort schemas by value.
		sort ( this->tree.children.begin(), this->tree.children.end(), CompareNodeValues );
		SortWithinOffspring ( this->tree.children );
	}

}	// Sort


// -------------------------------------------------------------------------------------------------
// Erase
// -----
//
// Clear everything except for clientRefs.

void
XMPMeta::Erase()
{

	if ( this->xmlParser != 0 ) {
		delete ( this->xmlParser );
		this->xmlParser = 0;
	}
	this->tree.ClearNode();

}	// Erase


// -------------------------------------------------------------------------------------------------
// Clone
// -----

void
XMPMeta::Clone ( XMPMeta * clone, XMP_OptionBits options ) const
{
	if ( clone == 0 ) XMP_Throw ( "Null clone pointer", kXMPErr_BadParam );
	if ( options != 0 ) XMP_Throw ( "No options are defined yet", kXMPErr_BadOptions );
	XMP_Assert ( this->tree.parent == 0 );

	clone->tree.ClearNode();

	clone->tree.options = this->tree.options;
	clone->tree.name    = this->tree.name;
	clone->tree.value   = this->tree.value;
	clone->errorCallback = this->errorCallback;

	#if 0	// *** XMP_DebugBuild
		clone->tree._namePtr = clone->tree.name.c_str();
		clone->tree._valuePtr = clone->tree.value.c_str();
	#endif

	CloneOffspring ( &this->tree, &clone->tree );

}	// Clone

// =================================================================================================
// XMP_Node::GetLocalURI
// =====================
//
// This has to be someplace where XMPMeta::GetNamespaceURI is visible.

void XMP_Node::GetLocalURI ( XMP_StringPtr * uriStr, XMP_StringLen * uriSize ) const
{

	if ( uriStr != 0 ) *uriStr = "";	// Set up empty defaults.
	if ( uriSize != 0 ) *uriSize = 0;

	if ( this->name.empty() ) return;

	if ( XMP_NodeIsSchema ( this->options ) ) {

		if ( uriStr != 0 ) *uriStr = this->name.c_str();
		if ( uriSize != 0 ) *uriSize = static_cast<XMP_StringLen>( this->name.size() );

	} else {

		size_t colonPos = this->name.find_first_of(':');
		if ( colonPos == XMP_VarString::npos ) return;	// ! Name of array items is "[]".

		XMP_VarString prefix ( this->name, 0, colonPos );
		XMPMeta::GetNamespaceURI ( prefix.c_str(), uriStr, uriSize );

	}

}

void XMP_Node::GetFullQualifiedName( XMP_StringPtr * uriStr, XMP_StringLen * uriSize, XMP_StringPtr * nameStr, XMP_StringLen * nameSize ) const
{
	if ( uriStr != 0 ) *uriStr = "";	// Set up empty defaults.
	if ( uriSize != 0 ) *uriSize = 0;
	if ( nameStr != 0 ) *nameStr = "";
	if ( nameSize != 0 ) *nameSize = 0;

	if ( this->name.empty() ) return;

	if ( XMP_NodeIsSchema ( this->options ) ) {

		if ( uriStr != 0 ) *uriStr = this->name.c_str();
		if ( uriSize != 0 ) *uriSize = static_cast<XMP_StringLen>( this->name.size() );
		if ( nameStr != 0 ) *nameStr = this->value.c_str();
		if ( nameSize != 0 ) *nameSize = static_cast<XMP_StringLen>( this->value.size() );

	} else {

		size_t colonPos = this->name.find_first_of(':');
		if ( colonPos == XMP_VarString::npos ) return;	// ! Name of array items is "[]".

		XMP_VarString prefix ( this->name, 0, colonPos );
		XMPMeta::GetNamespaceURI ( prefix.c_str(), uriStr, uriSize );
		*nameStr = this->name.c_str() + colonPos + 1;
		*nameSize = static_cast<XMP_StringLen>( this->name.size() - colonPos - 1 );
	}
}

// =================================================================================================
// Error notifications
// ===================

// -------------------------------------------------------------------------------------------------
// SetDefaultErrorCallback
// -----------------------

/* class-static */ void
XMPMeta::SetDefaultErrorCallback ( XMPMeta_ErrorCallbackWrapper wrapperProc,
								   XMPMeta_ErrorCallbackProc    clientProc,
								   void *    context,
								   XMP_Uns32 limit )
{
	XMP_Assert ( wrapperProc != 0 );	// Must always be set by the glue;

	sDefaultErrorCallback.wrapperProc = wrapperProc;
	sDefaultErrorCallback.clientProc = clientProc;
	sDefaultErrorCallback.context = context;
	sDefaultErrorCallback.limit = limit;

}	// SetDefaultErrorCallback

// -------------------------------------------------------------------------------------------------
// SetErrorCallback
// ----------------

void
XMPMeta::SetErrorCallback ( XMPMeta_ErrorCallbackWrapper wrapperProc,
							XMPMeta_ErrorCallbackProc    clientProc,
							void *    context,
							XMP_Uns32 limit )
{
	XMP_Assert ( wrapperProc != 0 );	// Must always be set by the glue;
	
	this->errorCallback.Clear();
	this->errorCallback.wrapperProc = wrapperProc;
	this->errorCallback.clientProc = clientProc;
	this->errorCallback.context = context;
	this->errorCallback.limit = limit;
	
}	// SetErrorCallback

// -------------------------------------------------------------------------------------------------
// ResetErrorCallbackLimit
// -----------------------

void
XMPMeta::ResetErrorCallbackLimit ( XMP_Uns32 limit )
{

	this->errorCallback.limit = limit;
	this->errorCallback.notifications = 0;
	this->errorCallback.topSeverity = kXMPErrSev_Recoverable;

}	// ResetErrorCallbackLimit

// -------------------------------------------------------------------------------------------------
// ErrorCallbackInfo::CanNotify
// -------------------------------
//
// This is const just to be usable from const XMPMeta functions.

bool XMPMeta::ErrorCallbackInfo::CanNotify() const
{
	XMP_Assert ( (this->clientProc == 0) || (this->wrapperProc != 0) );
	return ( this->clientProc != 0);
}

// -------------------------------------------------------------------------------------------------
// ErrorCallbackInfo::ClientCallbackWrapper
// -------------------------------
//
// This is const just to be usable from const XMPMeta functions.

bool XMPMeta::ErrorCallbackInfo::ClientCallbackWrapper ( XMP_StringPtr filePath, XMP_ErrorSeverity severity, XMP_Int32 cause, XMP_StringPtr messsage ) const
{
	XMP_Bool retValue = (*this->wrapperProc) ( this->clientProc, this->context, severity, cause, messsage );
	return ConvertXMP_BoolToBool(retValue);
}

// =================================================================================================
