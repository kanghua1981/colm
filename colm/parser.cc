/*
 *  Copyright 2006-2012 Adrian Thurston <thurston@complang.org>
 */

/*  This file is part of Colm.
 *
 *  Colm is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  Colm is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with Colm; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <iostream>
#include <errno.h>

#include "parser.h"
#include "config.h"
#include "lmparse.h"
#include "global.h"
#include "input.h"

using std::cout;
using std::cerr;
using std::endl;

void BaseParser::init()
{
	/* Set up the root namespace. */
	Namespace *rootNamespace = createNamespace(
			internal, String("___ROOT_NAMESPACE") );
	pd->rootNamespace = rootNamespace;

	TokenRegion *rootRegion = createRegion( internal );

	RegionSet *rootRegionSet = new RegionSet( rootRegion, 0, 0, 0 );
	pd->regionSetList.append( rootRegionSet );
	regionStack.push( rootRegionSet );

	pd->rootRegion = rootRegion;

	/* Set up the global object. */
	String global = "global";
	pd->globalObjectDef = ObjectDef::cons( ObjectDef::UserType,
			global, pd->nextObjectId++ ); 
	
	/* The eofTokenRegion defaults to the root region. */
	pd->eofTokenRegion = rootRegion;

	/* Initialize the dictionary of graphs. This is our symbol table. The
	 * initialization needs to be done on construction which happens at the
	 * beginning of a machine spec so any assignment operators can reference
	 * the builtins. */
	pd->initGraphDict();

	pd->rootLocalFrame = ObjectDef::cons( ObjectDef::FrameType, 
				"local", pd->nextObjectId++ );
	pd->curLocalFrame = pd->rootLocalFrame;
}

void BaseParser::addRegularDef( const InputLoc &loc, Namespace *nspace,
		const String &name, LexJoin *join )
{
	GraphDictEl *newEl = nspace->rlMap.insert( name );
	if ( newEl != 0 ) {
		/* New element in the dict, all good. */
		newEl->value = new LexDefinition( name, join );
		newEl->isInstance = false;
		newEl->loc = loc;
	}
	else {
		// Recover by ignoring the duplicate.
		error(loc) << "regular definition \"" << name << "\" already exists" << endl;
	}
}

TokenRegion *BaseParser::createRegion( const InputLoc &loc )
{
	TokenRegion *tokenRegion = new TokenRegion( loc,
			pd->regionList.length() );

	pd->regionList.append( tokenRegion );

	/* New element in the dict, all good. */
	RegionDef *regionDef = new RegionDef( tokenRegion, loc );
	pd->regionDefList.append( regionDef );

	return tokenRegion;
}

void BaseParser::pushRegionSet( const InputLoc &loc )
{
	TokenRegion *tokenIgnore = createRegion( loc );
	TokenRegion *tokenOnly = createRegion( loc );
	TokenRegion *ignoreOnly = createRegion( loc );
	TokenRegion *collectIgnore = createRegion( loc );

	RegionSet *regionSet = new RegionSet( tokenIgnore,
			tokenOnly, ignoreOnly, collectIgnore );

	collectIgnore->isCiOnly = true;
	collectIgnore->ignoreOnly = ignoreOnly;

	pd->regionSetList.append( regionSet );
	regionStack.push( regionSet );
}

void BaseParser::popRegionSet()
{
	regionStack.pop();
}

Namespace *BaseParser::createNamespace( const InputLoc &loc, const String &name )
{
	Namespace *parentNamespace = namespaceStack.length() > 0 ?
			namespaceStack.top() : 0;

	/* Make the new namespace. */
	Namespace *nspace = new Namespace( loc, name,
			pd->namespaceList.length(), parentNamespace );

	if ( parentNamespace != 0 )
		parentNamespace->childNamespaces.append( nspace );

	pd->namespaceList.append( nspace );
	namespaceStack.push( nspace );

	return nspace;
}

LexJoin *BaseParser::literalJoin( const InputLoc &loc, const String &data )
{
	Literal *literal = Literal::cons( loc, data, Literal::LitString );
	LexFactor *factor = LexFactor::cons( literal );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( loc, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( loc, factorNeg );
	LexFactorAug *factorAug = LexFactorAug::cons( factorRep );
	LexTerm *term = LexTerm::cons( factorAug );
	LexExpression *expr = LexExpression::cons( term );
	LexJoin *join = LexJoin::cons( expr );
	return join;
}

void BaseParser::tokenDef( const InputLoc &loc, String name, LexJoin *join, ObjectDef *objectDef,
		CodeBlock *transBlock, bool ignore, bool noPreIgnore, bool noPostIgnore )
{
	/* Check the region if this is for an ignore. */
	if ( ignore && !pd->insideRegion )
		error(loc) << "ignore tokens can only appear inside scanners" << endp;

	/* Check the name if this is a token. */
	if ( !ignore && name == 0 )
		error(loc) << "tokens must have a name" << endp;

	/* Give a default name to ignores. */ 
	if ( name == 0 )
		name.setAs( 32, "_ignore_%.4x", pd->nextTokenId );

	Namespace *nspace = namespaceStack.top();
	RegionSet *regionSet = regionStack.top();

	TokenDef *tokenDef = TokenDef::cons( name, String(), false, ignore, join, 
			transBlock, loc, 0, nspace, regionSet, objectDef, contextStack.top() );

	regionSet->tokenDefList.append( tokenDef );
	nspace->tokenDefList.append( tokenDef );

	tokenDef->noPreIgnore = noPreIgnore;
	tokenDef->noPostIgnore = noPostIgnore;

	TokenInstance *tokenInstance = TokenInstance::cons( tokenDef,
			join, loc, pd->nextTokenId++, nspace, 
			regionSet->tokenIgnore );

	regionSet->tokenIgnore->tokenInstanceList.append( tokenInstance );

	tokenDef->noPreIgnore = noPreIgnore;
	tokenDef->noPostIgnore = noPostIgnore;

	if ( ignore ) {
		/* The instance for the ignore-only. */
		TokenInstance *tokenInstanceIgn = TokenInstance::cons( tokenDef,
				join, loc, pd->nextTokenId++, nspace, regionSet->ignoreOnly );

		tokenInstanceIgn->dupOf = tokenInstance;

		regionSet->ignoreOnly->tokenInstanceList.append( tokenInstanceIgn );
	}
	else {
		/* The instance for the token-only. */
		TokenInstance *tokenInstanceTok = TokenInstance::cons( tokenDef,
				join, loc, pd->nextTokenId++, nspace, regionSet->tokenOnly );

		tokenInstanceTok->dupOf = tokenInstance;

		regionSet->tokenOnly->tokenInstanceList.append( tokenInstanceTok );
	}

	/* This is created and pushed in the name. */
	if ( !pd->insideRegion )
		popRegionSet();

	if ( join != 0 ) {
		/* Create a regular language definition so the token can be used to
		 * make other tokens */
		addRegularDef( loc, namespaceStack.top(), name, join );
	}
}

void BaseParser::zeroDef( const InputLoc &loc, const String &data,
		bool noPreIgnore, bool noPostIgnore )
{
	/* Create a name for the literal. */
	String name( 32, "_literal_%.4x", pd->nextTokenId );

	bool insideRegion = regionStack.length() > 1;
	if ( !insideRegion )
		error(loc) << "zero token should be inside token" << endp;

	String interp("");;

	/* Look for the production's associated region. */
	Namespace *nspace = namespaceStack.top();
	RegionSet *regionSet = regionStack.top();

	LiteralDictEl *ldel = nspace->literalDict.find( interp );
	if ( ldel != 0 )
		error( loc ) << "literal already defined in this namespace" << endp;

	LexJoin *join = literalJoin( loc, data );

	TokenDef *tokenDef = TokenDef::cons( name, data, true, false, join, 
			0, loc, 0, nspace, regionSet, 0, 0 );

	tokenDef->isZero = true;

	regionSet->tokenDefList.append( tokenDef );
	nspace->tokenDefList.append( tokenDef );

	TokenInstance *tokenInstance = TokenInstance::cons( tokenDef, join, 
			loc, pd->nextTokenId++, nspace, regionSet->tokenIgnore );

	/* Doesn't go into instance list. */

	ldel = nspace->literalDict.insert( interp, tokenInstance );
}

void BaseParser::literalDef( const InputLoc &loc, const String &data,
		bool noPreIgnore, bool noPostIgnore )
{
	/* Create a name for the literal. */
	String name( 32, "_literal_%.4x", pd->nextTokenId );

	bool insideRegion = regionStack.length() > 1;
	if ( !insideRegion )
		pushRegionSet( loc );

	bool unusedCI;
	String interp;
	prepareLitString( interp, unusedCI, data, loc );

	/* Look for the production's associated region. */
	Namespace *nspace = namespaceStack.top();
	RegionSet *regionSet = regionStack.top();

	LiteralDictEl *ldel = nspace->literalDict.find( interp );
	if ( ldel != 0 )
		error( loc ) << "literal already defined in this namespace" << endp;

	LexJoin *join = literalJoin( loc, data );

	/* The token definition. */
	TokenDef *tokenDef = TokenDef::cons( name, data, true, false, join, 
			0, loc, 0, nspace, regionSet, 0, 0 );

	regionSet->tokenDefList.append( tokenDef );
	nspace->tokenDefList.append( tokenDef );

	/* The instance for the token/ignore region. */
	TokenInstance *tokenInstance = TokenInstance::cons( tokenDef, join, 
			loc, pd->nextTokenId++, nspace, regionSet->tokenIgnore );

	regionSet->tokenIgnore->tokenInstanceList.append( tokenInstance );

	ldel = nspace->literalDict.insert( interp, tokenInstance );

	/* Make the duplicate for the token-only region. */
	tokenDef->noPreIgnore = noPreIgnore;
	tokenDef->noPostIgnore = noPostIgnore;

	/* The instance for the token-only region. */
	TokenInstance *tokenInstanceTok = TokenInstance::cons( tokenDef,
			join, loc, pd->nextTokenId++, nspace,
			regionSet->tokenOnly );

	tokenInstanceTok->dupOf = tokenInstance;

	regionSet->tokenOnly->tokenInstanceList.append( tokenInstanceTok );

	if ( !insideRegion )
		popRegionSet();
}

void BaseParser::addArgvList()
{
	TypeRef *typeRef = TypeRef::cons( internal, pd->uniqueTypeStr );
	pd->argvTypeRef = TypeRef::cons( internal, TypeRef::List, 0, typeRef, 0 );
}

ObjectDef *BaseParser::blockOpen()
{
	/* Init the object representing the local frame. */
	ObjectDef *frame = ObjectDef::cons( ObjectDef::FrameType, 
			"local", pd->nextObjectId++ );

	pd->curLocalFrame = frame;
	return frame;
}

void BaseParser::blockClose()
{
	pd->curLocalFrame = pd->rootLocalFrame;
}

void BaseParser::functionDef( StmtList *stmtList, ObjectDef *localFrame,
	ParameterList *paramList, TypeRef *typeRef, const String &name )
{
	CodeBlock *codeBlock = CodeBlock::cons( stmtList, localFrame );
	Function *newFunction = Function::cons( typeRef, name, 
			paramList, codeBlock, pd->nextFuncId++, false );
	pd->functionList.append( newFunction );
	newFunction->inContext = contextStack.top();
}

void BaseParser::iterDef( StmtList *stmtList, ObjectDef *localFrame,
	ParameterList *paramList, const String &name )
{
	CodeBlock *codeBlock = CodeBlock::cons( stmtList, localFrame );
	Function *newFunction = Function::cons( 0, name, 
			paramList, codeBlock, pd->nextFuncId++, true );
	pd->functionList.append( newFunction );
}

LangStmt *BaseParser::globalDef( ObjectField *objField, LangExpr *expr, 
		LangStmt::Type assignType )
{
	LangStmt *stmt = 0;

	ObjectDef *object;
	if ( contextStack.length() == 0 )
		object = pd->globalObjectDef;
	else {
		Context *context = contextStack.top();
		objField->context = context;
		object = context->contextObjDef;
	}

	if ( object->checkRedecl( objField->name ) != 0 )
		error(objField->loc) << "object field renamed" << endp;

	object->insertField( objField->name, objField );

	if ( expr != 0 ) {
		LangVarRef *varRef = LangVarRef::cons( objField->loc, objField->name );

		stmt = LangStmt::cons( objField->loc, 
				assignType, varRef, expr );
	}

	return stmt;
}

void BaseParser::cflDef( NtDef *ntDef, ObjectDef *objectDef, LelDefList *defList )
{
	Namespace *nspace = namespaceStack.top();

	ntDef->objectDef = objectDef;
	ntDef->defList = defList;

	nspace->ntDefList.append( ntDef );

	/* Declare the captures in the object. */
	for ( LelDefList::Iter prod = *defList; prod.lte(); prod++ ) {
		for ( ProdElList::Iter pel = *prod->prodElList; pel.lte(); pel++ ) {
			/* If there is a capture, create the field. */
			if ( pel->captureField != 0 ) {
				/* Might already exist. */
				ObjectField *newOf = objectDef->checkRedecl( pel->captureField->name );
				if ( newOf != 0 ) {
					/* FIXME: check the types are the same. */
				}
				else {
					newOf = pel->captureField;
					newOf->typeRef = pel->typeRef;
					objectDef->insertField( newOf->name, newOf );
				}

				newOf->isRhsGet = true;
				newOf->rhsVal.append( RhsVal( pel ) );
			}
		}
	}
}

ReOrBlock *BaseParser::lexRegularExprData( ReOrBlock *reOrBlock, ReOrItem *reOrItem )
{
	ReOrBlock *ret;

	/* An optimization to lessen the tree size. If an or char is directly under
	 * the left side on the right and the right side is another or char then
	 * paste them together and return the left side. Otherwise just put the two
	 * under a new or data node. */
	if ( reOrItem->type == ReOrItem::Data &&
			reOrBlock->type == ReOrBlock::RecurseItem &&
			reOrBlock->item->type == ReOrItem::Data )
	{
		/* Append the right side to right side of the left and toss the
		 * right side. */
		reOrBlock->item->data += reOrItem->data;
		delete reOrItem;
		ret = reOrBlock;
	}
	else {
		/* Can't optimize, put the left and right under a new node. */
		ret = ReOrBlock::cons( reOrBlock, reOrItem );
	}
	return ret;
}

LexFactor *BaseParser::lexRlFactorName( const String &data, const InputLoc &loc )
{
	LexFactor *factor = 0;
	/* Find the named graph. */
	Namespace *nspace = namespaceStack.top();

	while ( nspace != 0 ) {
		GraphDictEl *gdNode = nspace->rlMap.find( data );
		if ( gdNode != 0 ) {
			if ( gdNode->isInstance ) {
				/* Recover by retuning null as the factor node. */
				error(loc) << "references to graph instantiations not allowed "
						"in expressions" << endl;
				factor = 0;
			}
			else {
				/* Create a factor node that is a lookup of an expression. */
				factor = LexFactor::cons( loc, gdNode->value );
			}
			break;
		}

		nspace = nspace->parentNamespace;
	}

	if ( nspace == 0 ) {
		/* Recover by returning null as the factor node. */
		error(loc) << "graph lookup of \"" << data << "\" failed" << endl;
		factor = 0;
	}

	return factor;
}

int BaseParser::lexFactorRepNum( const InputLoc &loc, const String &data )
{
	/* Convert the priority number to a long. Check for overflow. */
	errno = 0;
	int rep = strtol( data, 0, 10 );
	if ( errno == ERANGE && rep == LONG_MAX ) {
		/* Repetition too large. Recover by returing repetition 1. */
		error(loc) << "repetition number " << data << " overflows" << endl;
		rep = 1;
	}
	return rep;
}

LexFactorAug *BaseParser::lexFactorLabel( const InputLoc &loc, const String &data, LexFactorAug *factorAug )
{
	/* Create the object field. */
	TypeRef *typeRef = TypeRef::cons( loc, pd->uniqueTypeStr );
	ObjectField *objField = ObjectField::cons( loc, typeRef, data );

	/* Create the enter and leaving actions that will mark the substring. */
	Action *enter = Action::cons( MarkMark, pd->nextMatchEndNum++ );
	Action *leave = Action::cons( MarkMark, pd->nextMatchEndNum++ );
	pd->actionList.append( enter );
	pd->actionList.append( leave );
	
	/* Add entering and leaving actions. */
	factorAug->actions.append( ParserAction( loc, at_start, 0, enter ) );
	factorAug->actions.append( ParserAction( loc, at_leave, 0, leave ) );

	factorAug->reCaptureVect.append( ReCapture( enter, leave, objField ) );

	return factorAug;
}

LexJoin *BaseParser::lexOptJoin( LexJoin *join, LexJoin *context )
{
	if ( context != 0 ) {
		/* Create the enter and leaving actions that will mark the substring. */
		Action *mark = Action::cons( MarkMark, pd->nextMatchEndNum++ );
		pd->actionList.append( mark );

		join->context = context;
		join->mark = mark;
	}

	return join;
}

LangExpr *BaseParser::send( const InputLoc &loc, LangVarRef *varRef, ConsItemList *list )
{
	Namespace *nspace = namespaceStack.top();
	ParserText *parserText = ParserText::cons( loc, nspace, list );
	pd->parserTextList.append( parserText );

	return LangExpr::cons( LangTerm::cons( InputLoc(),
			LangTerm::SendType, varRef, parserText ) );
}

LangExpr *BaseParser::parseCmd( const InputLoc &loc, bool stop, ObjectField *objField,
		TypeRef *typeRef, FieldInitVect *fieldInitVect, ConsItemList *list )
{
	LangExpr *expr = 0;

	Namespace *nspace = namespaceStack.top();

	/* We are constructing a parser, sending it items, then returning it.
	 * Thisis the constructor for the parser. */
	ConsItemList *emptyConsItemList = new ConsItemList;

	Constructor *constructor = Constructor::cons( loc, nspace,
			emptyConsItemList, pd->nextPatConsId++ );
	pd->replList.append( constructor );
	
	/* The parser may be referenced. */
	LangVarRef *varRef = 0;
	if ( objField != 0 )
		varRef = LangVarRef::cons( objField->loc, objField->name );

	/* The typeref for the parser. */
	TypeRef *parserTypeRef = TypeRef::cons( loc,
			TypeRef::Parser, 0, typeRef, 0 );

	ParserText *parserText = ParserText::cons( loc, nspace, list );
	pd->parserTextList.append( parserText );

	expr = LangExpr::cons( LangTerm::cons( loc, 
			stop ? LangTerm::ParseStopType : LangTerm::ParseType,
			varRef, objField, parserTypeRef, fieldInitVect, constructor, parserText ) );

	/* Check for redeclaration. */
	if ( objField != 0 ) {
		if ( pd->curLocalFrame->checkRedecl( objField->name ) != 0 ) {
			error( objField->loc ) << "variable " << objField->name <<
					" redeclared" << endp;
		}

		/* Insert it into the field map. */
		objField->typeRef = parserTypeRef;
		pd->curLocalFrame->insertField( objField->name, objField );
	}

	return expr;
}

PatternItemList *BaseParser::patternEl( LangVarRef *varRef, PatternItemList *list )
{
	/* Store the variable reference in the pattern itemm. */
	list->head->varRef = varRef;

	if ( varRef != 0 ) {
		if ( pd->curLocalFrame->checkRedecl( varRef->name ) != 0 ) {
			error( varRef->loc ) << "variable " << varRef->name << 
					" redeclared" << endp;
		}

		TypeRef *typeRef = list->head->factor->typeRef;
		ObjectField *objField = ObjectField::cons( InputLoc(), typeRef, varRef->name );

		/* Insert it into the field map. */
		pd->curLocalFrame->insertField( varRef->name, objField );
	}

	return list;
}

PatternItemList *BaseParser::patternElNamed( const InputLoc &loc,
		NamespaceQual *nspaceQual, const String &data, RepeatType repeatType )
{
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, data, repeatType );
	ProdEl *prodEl = new ProdEl( ProdEl::ReferenceType, loc, 0, false, typeRef, 0 );
	PatternItem *patternItem = PatternItem::cons( loc, prodEl, PatternItem::FactorType );
	return PatternItemList::cons( patternItem );
}

PatternItemList *BaseParser::patternElType( const InputLoc &loc,
		NamespaceQual *nspaceQual, const String &data, RepeatType repeatType )
{
	PdaLiteral *literal = new PdaLiteral( loc, data );
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, literal, repeatType );

	ProdEl *prodEl = new ProdEl( ProdEl::ReferenceType, loc, 0, false, typeRef, 0 );
	PatternItem *patternItem = PatternItem::cons( loc, prodEl, PatternItem::FactorType );
	return PatternItemList::cons( patternItem );
}

PatternItemList *BaseParser::patListConcat( PatternItemList *list1,
		PatternItemList *list2 )
{
	list1->append( *list2 );
	delete list2;
	return list1;
}

ConsItemList *BaseParser::consListConcat( ConsItemList *list1,
		ConsItemList *list2 )
{
	list1->append( *list2 );
	delete list2;
	return list1;
}

LangStmt *BaseParser::forScope( const InputLoc &loc, const String &data,
		TypeRef *typeRef, LangTerm *langTerm, StmtList *stmtList )
{
	/* Check for redeclaration. */
	if ( pd->curLocalFrame->checkRedecl( data ) != 0 )
		error( loc ) << "variable " << data << " redeclared" << endp;

	/* Note that we pass in a null type reference. This type is dependent
	 * on the result of the iter_call lookup since it must contain a reference
	 * to the iterator that is called. This lookup is done at compile time. */
	ObjectField *iterField = ObjectField::cons( loc, (TypeRef*)0, data );
	pd->curLocalFrame->insertField( data, iterField );

	LangStmt *stmt = LangStmt::cons( loc, LangStmt::ForIterType, 
			iterField, typeRef, langTerm, stmtList );

	return stmt;
}

void BaseParser::preEof( const InputLoc &loc, StmtList *stmtList, ObjectDef *localFrame )
{
	bool insideRegion = regionStack.length() > 1;
	if ( !insideRegion )
		error(loc) << "preeof must be used inside an existing region" << endl;

	CodeBlock *codeBlock = CodeBlock::cons( stmtList, localFrame );
	codeBlock->context = contextStack.length() == 0 ? 0 : contextStack.top();

	RegionSet *regionSet = regionStack.top();
	regionSet->tokenIgnore->preEofBlock = codeBlock;
}

ProdEl *BaseParser::prodElName( const InputLoc &loc, const String &data,
		NamespaceQual *nspaceQual, ObjectField *objField,
		RepeatType repeatType, bool commit )
{
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, data, repeatType );
	ProdEl *prodEl = new ProdEl( ProdEl::ReferenceType, loc, objField, commit, typeRef, 0 );
	return prodEl;
}

ProdEl *BaseParser::prodElLiteral( const InputLoc &loc, const String &data,
		NamespaceQual *nspaceQual, ObjectField *objField, RepeatType repeatType,
		bool commit )
{
	/* Create a new prodEl node going to a concat literal. */
	PdaLiteral *literal = new PdaLiteral( loc, data );
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, literal, repeatType );
	ProdEl *prodEl = new ProdEl( ProdEl::LiteralType, loc, objField, commit, typeRef, 0 );
	return prodEl;
}

ConsItemList *BaseParser::consElLiteral( const InputLoc &loc,
		const String &data, NamespaceQual *nspaceQual )
{
	PdaLiteral *literal = new PdaLiteral( loc, data );
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, literal );
	ProdEl *prodEl = new ProdEl( ProdEl::LiteralType, loc, 0, false, typeRef, 0 );
	ConsItem *consItem = ConsItem::cons( loc, ConsItem::FactorType, prodEl );
	ConsItemList *list = ConsItemList::cons( consItem );
	return list;
}

Production *BaseParser::production( const InputLoc &loc, ProdElList *prodElList,
		bool commit, CodeBlock *codeBlock, LangEl *predOf )
{
	Production *prod = Production::cons( loc, 0, prodElList,
			commit, codeBlock, pd->prodList.length(), 0 );
	prod->predOf = predOf;

	/* Link the production elements back to the production. */
	for ( ProdEl *prodEl = prodElList->head; prodEl != 0; prodEl = prodEl->next )
		prodEl->production = prod;

	pd->prodList.append( prod );

	return prod;
}

void BaseParser::objVarDef( ObjectDef *objectDef, ObjectField *objField )
{
	if ( objectDef->checkRedecl( objField->name ) != 0 )
		error() << "object field renamed" << endp;

	objectDef->insertField( objField->name, objField );
}

LelDefList *BaseParser::prodAppend( LelDefList *defList, Production *definition )
{
	definition->prodNum = defList->length();
	defList->append( definition );
	return defList;
}

LangExpr *BaseParser::construct( const InputLoc &loc, ObjectField *objField,
		ConsItemList *list, TypeRef *typeRef, FieldInitVect *fieldInitVect )
{
	Namespace *nspace = namespaceStack.top();
	Constructor *constructor = Constructor::cons( loc, nspace,
			list, pd->nextPatConsId++ );
	pd->replList.append( constructor );
	
	LangVarRef *varRef = 0;
	if ( objField != 0 )
		varRef = LangVarRef::cons( objField->loc, objField->name );

	LangExpr *expr = LangExpr::cons( LangTerm::cons( loc, LangTerm::ConstructType,
			varRef, objField, typeRef, fieldInitVect, constructor ) );

	/* Check for redeclaration. */
	if ( objField != 0 ) {
		if ( pd->curLocalFrame->checkRedecl( objField->name ) != 0 ) {
			error( objField->loc ) << "variable " << objField->name <<
					" redeclared" << endp;
		}

		/* Insert it into the field map. */
		objField->typeRef = typeRef;
		pd->curLocalFrame->insertField( objField->name, objField );
	}

	return expr;
}

LangExpr *BaseParser::match( const InputLoc &loc, LangVarRef *varRef,
		PatternItemList *list )
{
	Namespace *nspace = namespaceStack.top();

	Pattern *pattern = Pattern::cons( loc, nspace,
			list, pd->nextPatConsId++ );
	pd->patternList.append( pattern );

	LangExpr *expr = LangExpr::cons( LangTerm::cons( 
			InputLoc(), LangTerm::MatchType, varRef, pattern ) );

	return expr;
}

LangStmt *BaseParser::varDef( ObjectField *objField,
		LangExpr *expr, LangStmt::Type assignType )
{
	LangStmt *stmt = 0;

	/* Check for redeclaration. */
	if ( pd->curLocalFrame->checkRedecl( objField->name ) != 0 ) {
		error( objField->loc ) << "variable " << objField->name <<
				" redeclared" << endp;
	}

	/* Insert it into the field map. */
	pd->curLocalFrame->insertField( objField->name, objField );

	//cout << "var def " << $1->objField->name << endl;

	if ( expr != 0 ) {
		LangVarRef *varRef = LangVarRef::cons( objField->loc, 
				objField->name );

		stmt = LangStmt::cons( objField->loc, assignType, varRef, expr );
	}

	return stmt;
}

LangStmt *BaseParser::exportStmt( ObjectField *objField, LangStmt::Type assignType, LangExpr *expr )
{
	LangStmt *stmt = 0;

	if ( contextStack.length() != 0 )
		error(objField->loc) << "cannot export parser context variables" << endp;

	ObjectDef *object = pd->globalObjectDef;

	if ( object->checkRedecl( objField->name ) != 0 )
		error(objField->loc) << "object field renamed" << endp;

	object->insertField( objField->name, objField );
	objField->isExport = true;

	if ( expr != 0 ) {
		LangVarRef *varRef = LangVarRef::cons( objField->loc, 
				objField->name );

		stmt = LangStmt::cons( objField->loc, assignType, varRef, expr );
	}

	return stmt;
}

LangExpr *BaseParser::require( const InputLoc &loc,
		LangVarRef *varRef, PatternItemList *list )
{
	Namespace *nspace = namespaceStack.top();

	Pattern *pattern = Pattern::cons( loc, nspace,
			list, pd->nextPatConsId++ );
	pd->patternList.append( pattern );

	LangExpr *expr = LangExpr::cons( LangTerm::cons(
			InputLoc(), LangTerm::MatchType, varRef, pattern ) );
	return expr;
}

void BaseParser::contextVarDef( const InputLoc &loc, ObjectField *objField )
{
	ObjectDef *object;
	if ( contextStack.length() == 0 )
		error(loc) << "internal error: no context stack items found" << endp;

	Context *context = contextStack.top();
	objField->context = context;
	object = context->contextObjDef;

	if ( object->checkRedecl( objField->name ) != 0 )
		error(objField->loc) << "object field renamed" << endp;

	object->insertField( objField->name, objField );
}

void BaseParser::contextHead( const InputLoc &loc, const String &data )
{
	/* Make the new namespace. */
	Namespace *nspace = createNamespace( loc, data );

	Context *context = new Context( loc, 0 );
	contextStack.push( context );

	ContextDef *contextDef = new ContextDef( data, context, nspace );
	nspace->contextDefList.append( contextDef );

	context->contextObjDef = ObjectDef::cons( ObjectDef::UserType,
			data, pd->nextObjectId++ ); 
}

void BaseParser::guaranteeRegion()
{
	pd->insideRegion = regionStack.length() > 1;

	if ( !pd->insideRegion )
		pushRegionSet( internal );
}

StmtList *BaseParser::appendStatement( StmtList *stmtList, LangStmt *stmt )
{
	if ( stmt != 0 )
		stmtList->append( stmt );
	return stmtList;
}

ParameterList *BaseParser::appendParam( ParameterList *paramList, ObjectField *objField )
{
	paramList->append( objField );
	return paramList;
}

ObjectField *BaseParser::addParam( const InputLoc &loc, TypeRef *typeRef,
		const String &name )
{
	ObjectField *objField = ObjectField::cons( loc, typeRef, name );
	objField->isParam = true;
	return objField;
}

PredDecl *BaseParser::predTokenName( const InputLoc &loc, NamespaceQual *qual,
		const String &data )
{
	TypeRef *typeRef = TypeRef::cons( loc, qual, data );
	PredDecl *predDecl = new PredDecl( typeRef, pd->predValue );
	return predDecl;
}

PredDecl *BaseParser::predTokenLit( const InputLoc &loc, const String &data,
		NamespaceQual *nspaceQual )
{
	PdaLiteral *literal = new PdaLiteral( loc, data );
	TypeRef *typeRef = TypeRef::cons( loc, nspaceQual, literal );
	PredDecl *predDecl = new PredDecl( typeRef, pd->predValue );
	return predDecl;
}

void BaseParser::alias( const InputLoc &loc, const String &data, TypeRef *typeRef )
{
	Namespace *nspace = namespaceStack.top();
	TypeAlias *typeAlias = new TypeAlias( loc, nspace, data, typeRef );
	nspace->typeAliasList.append( typeAlias );
}

void BaseParser::precedenceStmt( PredType predType, PredDeclList *predDeclList )
{
	while ( predDeclList->length() > 0 ) {
		PredDecl *predDecl = predDeclList->detachFirst();
		predDecl->predType = predType;
		pd->predDeclList.append( predDecl );
	}
	pd->predValue++;
}


void BaseParser::parseInput( StmtList *stmtList )
{
	NamespaceQual *nspaceQual = NamespaceQual::cons( namespaceStack.top() );
	TypeRef *typeRef = TypeRef::cons( internal, nspaceQual, String("start"), RepeatNone );

	LangVarRef *varRef = LangVarRef::cons( internal, new QualItemVect, String("stdin") );
	LangExpr *expr = LangExpr::cons( LangTerm::cons( internal, LangTerm::VarRefType, varRef ) );

	ConsItem *consItem = ConsItem::cons( internal, ConsItem::ExprType, expr );
	ConsItemList *list = ConsItemList::cons( consItem );

	ObjectField *objField = ObjectField::cons( internal, 0, String("P") );

	expr = parseCmd( internal, false, objField, typeRef, 0, list );
	LangStmt *stmt = LangStmt::cons( internal, LangStmt::ExprType, expr );
	stmtList->append( stmt );
}

void BaseParser::printParseTree( StmtList *stmtList )
{
	QualItemVect *qual = new QualItemVect;
	qual->append( QualItem( internal, String( "P" ), QualItem::Dot ) );
	LangVarRef *varRef = LangVarRef::cons( internal, qual, String("tree") );
	LangExpr *expr = LangExpr::cons( LangTerm::cons( internal, LangTerm::VarRefType, varRef ) );

	ExprVect *exprVect = new ExprVect;
	exprVect->append( expr );
	LangStmt *stmt = LangStmt::cons( internal, LangStmt::PrintType, exprVect );
	stmtList->append( stmt );
}

/* */

LexTerm *rangeTerm( const char *low, const char *high )
{
	Literal *lowLit = Literal::cons( internal, String( low ), Literal::LitString );
	Literal *highLit = Literal::cons( internal, String( high ), Literal::LitString );
	Range *range = Range::cons( lowLit, highLit );
	LexFactor *factor = LexFactor::cons( range );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorAug *factorAug = LexFactorAug::cons( factorRep );
	LexTerm *term = LexTerm::cons( factorAug );
	return term;
}

LexTerm *litTerm( const char *str )
{
	Literal *lit = Literal::cons( internal, String( str ), Literal::LitString );
	LexFactor *factor = LexFactor::cons( lit );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorAug *factorAug = LexFactorAug::cons( factorRep );
	LexTerm *term = LexTerm::cons( factorAug );
	return term;
}

LexExpression *orExpr( LexTerm *term1, LexTerm *term2 )
{
	LexExpression *expr1 = LexExpression::cons( term1 );
	return LexExpression::cons( expr1, term2, LexExpression::OrType );
}

LexExpression *orExpr( LexTerm *term1, LexTerm *term2, LexTerm *term3 )
{
	LexExpression *expr1 = LexExpression::cons( term1 );
	LexExpression *expr2 = LexExpression::cons( expr1, term2, LexExpression::OrType );
	LexExpression *expr3 = LexExpression::cons( expr2, term3, LexExpression::OrType );
	return expr3;
}

LexExpression *orExpr( LexTerm *term1, LexTerm *term2, LexTerm *term3, LexTerm *term4 )
{
	LexExpression *expr1 = LexExpression::cons( term1 );
	LexExpression *expr2 = LexExpression::cons( expr1, term2, LexExpression::OrType );
	LexExpression *expr3 = LexExpression::cons( expr2, term3, LexExpression::OrType );
	LexExpression *expr4 = LexExpression::cons( expr3, term4, LexExpression::OrType );
	return expr4;
}

LexExpression *orExpr( LexTerm *term1, LexTerm *term2, LexTerm *term3,
		LexTerm *term4, LexTerm *term5, LexTerm *term6 )
{
	LexExpression *expr1 = LexExpression::cons( term1 );
	LexExpression *expr2 = LexExpression::cons( expr1, term2, LexExpression::OrType );
	LexExpression *expr3 = LexExpression::cons( expr2, term3, LexExpression::OrType );
	LexExpression *expr4 = LexExpression::cons( expr3, term4, LexExpression::OrType );
	return expr4;
}

LexFactorAug *starFactorAug( LexExpression *expr )
{
	LexJoin *join = LexJoin::cons( expr );
	LexFactor *factor = LexFactor::cons( join );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorRep *staredRep = LexFactorRep::cons( internal, factorRep, 0, 0, LexFactorRep::StarType );
	LexFactorAug *factorAug = LexFactorAug::cons( staredRep );
	return factorAug;
}

LexFactorAug *plusFactorAug( LexExpression *expr )
{
	LexJoin *join = LexJoin::cons( expr );
	LexFactor *factor = LexFactor::cons( join );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorRep *staredRep = LexFactorRep::cons( internal, factorRep, 0, 0, LexFactorRep::PlusType );
	LexFactorAug *factorAug = LexFactorAug::cons( staredRep );
	return factorAug;
}

LexTerm *concatTerm( LexFactorAug *fa1, LexFactorAug *fa2 )
{
	LexTerm *term1 = LexTerm::cons( fa1 );
	LexTerm *term2 = LexTerm::cons( term1, fa2, LexTerm::ConcatType );
	return term2;
}

LexFactorAug *parensFactorAug( LexExpression *expr )
{
	LexJoin *join = LexJoin::cons( expr );
	LexFactor *factor = LexFactor::cons( join );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorAug *factorAug = LexFactorAug::cons( factorRep );
	return factorAug;
}

LexFactorAug *parensFactorAug( LexTerm *term )
{
	LexExpression *expr = LexExpression::cons( term );
	LexJoin *join = LexJoin::cons( expr );
	LexFactor *factor = LexFactor::cons( join );
	LexFactorNeg *factorNeg = LexFactorNeg::cons( internal, factor );
	LexFactorRep *factorRep = LexFactorRep::cons( internal, factorNeg );
	LexFactorAug *factorAug = LexFactorAug::cons( factorRep );
	return factorAug;
}

void BaseParser::idToken()
{
	String hello( "id" );

	ObjectDef *objectDef = ObjectDef::cons( ObjectDef::UserType, hello, pd->nextObjectId++ ); 

	LexTerm *r1 = rangeTerm( "'a'", "'z'" );
	LexTerm *r2 = rangeTerm( "'A'", "'Z'" );
	LexTerm *r3 = litTerm( "'_'" );
	LexFactorAug *first = parensFactorAug( orExpr( r1, r2, r3 ) ); 

	LexTerm *r4 = rangeTerm( "'a'", "'z'" );
	LexTerm *r5 = rangeTerm( "'A'", "'Z'" );
	LexTerm *r6 = litTerm( "'_'" );
	LexTerm *r7 = rangeTerm( "'0'", "'9'" );
	LexExpression *second = orExpr( r4, r5, r6, r7 );
	LexFactorAug *secondStar = starFactorAug( second );

	LexTerm *concat = concatTerm( first, secondStar );

	LexExpression *expr = LexExpression::cons( concat );
	LexJoin *join = LexJoin::cons( expr );

	tokenDef( internal, hello, join, objectDef, 0, false, false, false );
}

void BaseParser::wsIgnore()
{
	ObjectDef *objectDef = ObjectDef::cons( ObjectDef::UserType, String(), pd->nextObjectId++ ); 

	LexTerm *r1 = litTerm( "' '" );
	LexTerm *r2 = litTerm( "'\t'" );
	LexTerm *r3 = litTerm( "'\v'" );
	LexTerm *r4 = litTerm( "'\n'" );
	LexTerm *r5 = litTerm( "'\r'" );
	LexTerm *r6 = litTerm( "'\f'" );

	LexExpression *whitespace = orExpr( r1, r2, r3, r4, r5, r6 );
	LexFactorAug *whitespaceRep = plusFactorAug( whitespace );

	LexTerm *term = LexTerm::cons( whitespaceRep );
	LexExpression *expr = LexExpression::cons( term );
	LexJoin *join = LexJoin::cons( expr );

	tokenDef( internal, String(), join, objectDef, 0, true, false, false );
}

void BaseParser::startProd()
{
	String start( "start" );
	ObjectDef *objectDef = ObjectDef::cons( ObjectDef::UserType, start, pd->nextObjectId++ ); 

	NtDef *ntDef = NtDef::cons( start, namespaceStack.top(),
				contextStack.top(), false );

	LelDefList *defList = new LelDefList;
		
	/* Production 1. */
	ProdElList *prodElList = new ProdElList;

	ProdEl *prodEl = prodElName( internal, String( "id" ),
			NamespaceQual::cons(namespaceStack.top()), 0, RepeatNone, false );
	prodElList->append( prodEl );

	Production *def = production( internal, prodElList, false, 0, 0 );
	prodAppend( defList, def );

	/* Make. */
	cflDef( ntDef, objectDef, defList );
}

void BaseParser::go()
{
	StmtList *stmtList = new StmtList;

	/* The token region */
	pushRegionSet( internal );
	pd->insideRegion = true;

	idToken();
	wsIgnore();

	pd->insideRegion = false;
	popRegionSet();

	startProd();

	parseInput( stmtList );

	printParseTree( stmtList );

	pd->rootCodeBlock = CodeBlock::cons( stmtList, 0 );
}