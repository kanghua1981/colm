/*
 *  Copyright 2009-2012 Adrian Thurston <thurston@complang.org>
 */

#include "bytecode.h"
#include "compiler.h"
#include <iostream>
#include <assert.h>

using std::cout;
using std::cerr;
using std::endl;

Namespace *TypeRef::resolveNspace( Compiler *pd )
{
	if ( parsedVarRef != 0 && !nspaceQual->thisOnly() ) {
		UniqueType *ut = parsedVarRef->lookup( pd );
		return ut->langEl->nspace;
	}
	else if ( parsedTypeRef != 0 && !nspaceQual->thisOnly() ) {
		UniqueType *ut = parsedTypeRef->resolveType( pd );
		return ut->langEl->nspace;
	}
	else {
		/* Lookup up the qualifiction and then the name. */
		return nspaceQual->getQual( pd );
	}
}

UniqueType *TypeRef::resolveTypeName( Compiler *pd )
{
	nspace = resolveNspace( pd );

	if ( nspace == 0 )
		error(loc) << "do not have region for resolving reference" << endp;

	while ( nspace != 0 ) {
		/* Search for the token in the region by typeName. */
		TypeMapEl *inDict = nspace->typeMap.find( typeName );

		if ( inDict != 0 ) {
			switch ( inDict->type ) {
				/* Defer to the typeRef we are an alias of. We need to guard
				 * against loops here. */
				case TypeMapEl::AliasType: {
					return inDict->typeRef->resolveType( pd );
				}

				case TypeMapEl::StructType: {
					UniqueType *structUt = pd->findUniqueType( TYPE_TREE, inDict->value );
					return pd->findUniqueType( TYPE_PTR, structUt->langEl );
				}

				case TypeMapEl::LangElType: {
					UniqueType *ut = pd->findUniqueType( TYPE_TREE, inDict->value );
					if ( ut == pd->uniqueTypeStream )
						return pd->findUniqueType( TYPE_PTR, ut->langEl );
					return ut;
				}
			}
		}

		if ( nspaceQual->thisOnly() )
			break;

		nspace = nspace->parentNamespace;
	}

	error(loc) << "unknown type in typeof expression" << endp;
	return 0;
}

UniqueType *TypeRef::resolveTypeLiteral( Compiler *pd )
{
	/* Lookup up the qualifiction and then the name. */
	nspace = resolveNspace( pd );

	if ( nspace == 0 )
		error(loc) << "do not have region for resolving reference" << endp;

	/* Interpret escape sequences and remove quotes. */
	bool unusedCI;
	String interp;
	prepareLitString( interp, unusedCI, pdaLiteral->data,
			pdaLiteral->loc );

	while ( nspace != 0 ) {
		LiteralDictEl *ldel = nspace->literalDict.find( interp );

		if ( ldel != 0 )
			return pd->findUniqueType( TYPE_TREE, ldel->value->tokenDef->tdLangEl );

		if ( nspaceQual->thisOnly() )
			break;

		nspace = nspace->parentNamespace;
	}

	error(loc) << "unknown type in typeof expression" << endp;
	return 0;
}

UniqueType *TypeRef::resolveTypeMapObj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utKey = typeRef1->resolveType( pd );	
	UniqueType *utValue = typeRef2->resolveType( pd );	

	UniqueMap searchKey( utKey, utValue );
	UniqueMap *inMap = pd->uniqueMapMap.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueMap( utKey, utValue );
		pd->uniqueMapMap.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int mapId = 0;
		String name( 36, "__map%d", mapId++ );

		GenericType *generic = new GenericType( name, GEN_MAP,
				pd->nextGenericId++, 0/*langEl*/, typeRef2 );
		generic->keyTypeArg = typeRef1;

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeMap( Compiler *pd )
{
	UniqueType *mapUt = resolveTypeMapObj( pd );
	return pd->findUniqueType( TYPE_PTR, mapUt->langEl );
}

UniqueType *TypeRef::resolveTypeListObj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utValue = typeRef1->resolveType( pd );	

	UniqueList searchKey( utValue );
	UniqueList *inMap = pd->uniqueListMap.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueList( utValue );
		pd->uniqueListMap.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int listId = 0;
		String name( 36, "__list%d", listId++ );

		GenericType *generic = new GenericType( name, GEN_LIST,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeList( Compiler *pd )
{
	UniqueType *listUt = resolveTypeListObj( pd );
	return pd->findUniqueType( TYPE_PTR, listUt->langEl );
}

UniqueType *TypeRef::resolveTypeParserObj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utParse = typeRef1->resolveType( pd );	

	UniqueParser searchKey( utParse );
	UniqueParser *inMap = pd->uniqueParserMap.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueParser( utParse );
		pd->uniqueParserMap.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int accumId = 0;
		String name( 36, "__accum%d", accumId++ );

		GenericType *generic = new GenericType( name, GEN_PARSER,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeParser( Compiler *pd )
{
	UniqueType *parserUt = resolveTypeParserObj( pd );
	return pd->findUniqueType( TYPE_PTR, parserUt->langEl );
}

/*
 * Object-based list/map
 */
UniqueType *TypeRef::resolveTypeList2ElObj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utValue = typeRef1->resolveType( pd );	

	UniqueList2El searchKey( utValue );
	UniqueList2El *inMap = pd->uniqueList2ElMap.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueList2El( utValue );
		pd->uniqueList2ElMap.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int listId = 0;
		String name( 36, "__list2el%d", listId++ );

		GenericType *generic = new GenericType( name, GEN_LIST2EL,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeList2El( Compiler *pd )
{
	UniqueType *listUt = resolveTypeList2ElObj( pd );
	return pd->findUniqueType( TYPE_PTR, listUt->langEl );
}

UniqueType *TypeRef::resolveTypeList2Obj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utValue = typeRef1->resolveType( pd );	

	/* Find the offset of the list element. */
	int off = 0;
	bool found = false;
	FieldList *fieldList = utValue->langEl->objectDef->fieldList;
	for ( FieldList::Iter f = *fieldList; f.lte(); f++, off++ ) {
		UniqueType *fUT = f->value->typeRef->resolveType( pd );
		if ( fUT->langEl->generic != 0 &&
				fUT->langEl->generic->typeId == GEN_LIST2EL )
		{
			found = true;
			break;
		}
	}

	if ( !found )
		error( loc ) << "cound not find list element in type ref" << endp;

	UniqueList2 searchKey( utValue, off );
	UniqueList2 *inMap = pd->uniqueList2Map.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueList2( utValue, off );
		pd->uniqueList2Map.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int listId = 0;
		String name( 36, "__list2%d", listId++ );

		GenericType *generic = new GenericType( name, GEN_LIST2,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		generic->elOffset = off;
		nspace->genericList.append( generic );
		generic->declare( pd, nspace );
		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeList2( Compiler *pd )
{
	UniqueType *listUt = resolveTypeList2Obj( pd );
	return pd->findUniqueType( TYPE_PTR, listUt->langEl );
}

UniqueType *TypeRef::resolveTypeMap2ElObj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utValue = typeRef1->resolveType( pd );	

	UniqueMap2El searchKey( utValue );
	UniqueMap2El *inMap = pd->uniqueMap2ElMap.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueMap2El( utValue );
		pd->uniqueMap2ElMap.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int listId = 0;
		String name( 36, "__map2el%d", listId++ );

		GenericType *generic = new GenericType( name, GEN_MAP2EL,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeMap2El( Compiler *pd )
{
	UniqueType *listUt = resolveTypeMap2ElObj( pd );
	return pd->findUniqueType( TYPE_PTR, listUt->langEl );
}

UniqueType *TypeRef::resolveTypeMap2Obj( Compiler *pd )
{
	nspace = pd->rootNamespace;

	UniqueType *utValue = typeRef1->resolveType( pd );	

	UniqueMap2 searchKey( utValue );
	UniqueMap2 *inMap = pd->uniqueMap2Map.find( &searchKey );
	if ( inMap == 0 ) {
		inMap = new UniqueMap2( utValue );
		pd->uniqueMap2Map.insert( inMap );

		/* FIXME: Need uniqe name allocator for types. */
		static int listId = 0;
		String name( 36, "__map2%d", listId++ );

		GenericType *generic = new GenericType( name, GEN_MAP2,
				pd->nextGenericId++, 0/*langEl*/, typeRef1 );

		nspace->genericList.append( generic );

		generic->declare( pd, nspace );

		inMap->generic = generic;
	}

	generic = inMap->generic;
	return pd->findUniqueType( TYPE_TREE, inMap->generic->langEl );
}

UniqueType *TypeRef::resolveTypeMap2( Compiler *pd )
{
	UniqueType *listUt = resolveTypeMap2Obj( pd );
	return pd->findUniqueType( TYPE_PTR, listUt->langEl );
}

/*
 * End object based list/map
 */

UniqueType *TypeRef::resolveTypePtr( Compiler *pd )
{
	typeRef1->resolveType( pd );
	return pd->findUniqueType( TYPE_PTR, typeRef1->uniqueType->langEl );
}

UniqueType *TypeRef::resolveTypeRef( Compiler *pd )
{
	typeRef1->resolveType( pd );
	return pd->findUniqueType( TYPE_REF, typeRef1->uniqueType->langEl );
}

void TypeRef::resolveRepeat( Compiler *pd )
{
	if ( uniqueType->typeId != TYPE_TREE )
		error(loc) << "cannot repeat non-tree type" << endp;

	UniqueRepeat searchKey( repeatType, uniqueType->langEl );
	UniqueRepeat *uniqueRepeat = pd->uniqeRepeatMap.find( &searchKey );
	if ( uniqueRepeat == 0 ) {
		uniqueRepeat = new UniqueRepeat( repeatType, uniqueType->langEl );
		pd->uniqeRepeatMap.insert( uniqueRepeat );

		LangEl *declLangEl = 0;
	
		switch ( repeatType ) {
			case RepeatRepeat: {
				/* If the factor is a repeat, create the repeat element and link the
				 * factor to it. */
				String repeatName( 128, "_repeat_%s", typeName.data );
				declLangEl = pd->makeRepeatProd( loc, nspace, repeatName, uniqueType );
				break;
			}
			case RepeatList: {
				/* If the factor is a repeat, create the repeat element and link the
				 * factor to it. */
				String listName( 128, "_list_%s", typeName.data );
				declLangEl = pd->makeListProd( loc, nspace, listName, uniqueType );
				break;
			}
			case RepeatOpt: {
				/* If the factor is an opt, create the opt element and link the factor
				 * to it. */
				String optName( 128, "_opt_%s", typeName.data );
				declLangEl = pd->makeOptProd( loc, nspace, optName, uniqueType );
				break;
			}

			case RepeatNone:
				break;
		}

		uniqueRepeat->declLangEl = declLangEl;
		declLangEl->repeatOf = uniqueRepeat->langEl;
	}

	uniqueType = pd->findUniqueType( TYPE_TREE, uniqueRepeat->declLangEl );
}

UniqueType *TypeRef::resolveIterator( Compiler *pd )
{
	UniqueType *searchUT = searchTypeRef->resolveType( pd );

	/* Lookup the iterator call. Make sure it is an iterator. */
	VarRefLookup lookup = iterCall->langTerm->varRef->lookupMethod( pd );
	if ( lookup.objMethod->iterDef == 0 ) {
		error(loc) << "attempt to iterate using something "
				"that is not an iterator" << endp;
	}

	/* Now that we have done the iterator call lookup we can make the type
	 * reference for the object field. */
	UniqueType *iterUniqueType = pd->findUniqueType( TYPE_ITER, lookup.objMethod->iterDef );

	iterDef = lookup.objMethod->iterDef;
	searchUniqueType = searchUT;

	return iterUniqueType;
}


UniqueType *TypeRef::resolveType( Compiler *pd )
{
	if ( uniqueType != 0 )
		return uniqueType;

	/* Not an iterator. May be a reference. */
	switch ( type ) {
		case Name:
			uniqueType = resolveTypeName( pd );
			break;
		case Literal:
			uniqueType = resolveTypeLiteral( pd );
			break;
		case Map:
			uniqueType = resolveTypeMap( pd );
			break;
		case List:
			uniqueType = resolveTypeList( pd );
			break;
		case Parser:
			uniqueType = resolveTypeParser( pd );
			break;
		case Ptr:
			uniqueType = resolveTypePtr( pd );
			break;
		case Ref:
			uniqueType = resolveTypeRef( pd );
			break;
		case Iterator:
			uniqueType = resolveIterator( pd );
			break;
		case List2El:
			uniqueType = resolveTypeList2El( pd );
			break;
		case List2:
			uniqueType = resolveTypeList2( pd );
			break;
		case Map2El:
			uniqueType = resolveTypeMap2El( pd );
			break;
		case Map2:
			uniqueType = resolveTypeMap2( pd );
			break;
		case Unspecified:
			/* No lookup needed, unique type(s) set when constructed. */
			break;
	}

	if ( repeatType != RepeatNone )
		resolveRepeat( pd );

	return uniqueType;
}

void Compiler::resolveProdEl( ProdEl *prodEl )
{
	prodEl->typeRef->resolveType( this );
	prodEl->langEl = prodEl->typeRef->uniqueType->langEl;
}

void LangTerm::resolve( Compiler *pd )
{
	switch ( type ) {
		case ConstructType:
			typeRef->resolveType( pd );

			/* Initialization expressions. */
			if ( fieldInitArgs != 0 ) {
				for ( FieldInitVect::Iter pi = *fieldInitArgs; pi.lte(); pi++ )
					(*pi)->expr->resolve( pd );
			}

			/* Types in constructor. */
			for ( ConsItemList::Iter item = *constructor->list; item.lte(); item++ ) {
				switch ( item->type ) {
				case ConsItem::LiteralType:
					/* Use pdaFactor reference resolving. */
					pd->resolveProdEl( item->prodEl );
					break;
				case ConsItem::InputText:
				case ConsItem::ExprType:
					break;
				}
			}
			break;
		case VarRefType:
			break;

		case MakeTreeType:
		case MakeTokenType:
		case MethodCallType:
			if ( args != 0 ) {
				for ( CallArgVect::Iter pe = *args; pe.lte(); pe++ )
					(*pe)->expr->resolve( pd );
			}
			break;

		case NumberType:
		case StringType:
			break;

		case MatchType:
			for ( PatternItemList::Iter item = *pattern->list; item.lte(); item++ ) {
				switch ( item->form ) {
				case PatternItem::TypeRefForm:
					/* Use pdaFactor reference resolving. */
					pd->resolveProdEl( item->prodEl );
					break;
				case PatternItem::InputTextForm:
					/* Nothing to do here. */
					break;
				}
			}

			break;
		case NewType:
			typeRef->resolveType( pd );
			break;
		case TypeIdType:
			typeRef->resolveType( pd );
			break;
		case SearchType:
			typeRef->resolveType( pd );
			break;
		case NilType:
		case TrueType:
		case FalseType:
			break;

		case ParseType:
		case ParseTreeType:
		case ParseStopType:
			typeRef->resolveType( pd );
			/* Evaluate the initialization expressions. */
			if ( fieldInitArgs != 0 ) {
				for ( FieldInitVect::Iter pi = *fieldInitArgs; pi.lte(); pi++ )
					(*pi)->expr->resolve( pd );
			}

			for ( ConsItemList::Iter item = *parserText->list; item.lte(); item++ ) {
				switch ( item->type ) {
				case ConsItem::LiteralType:
					pd->resolveProdEl( item->prodEl );
					break;
				case ConsItem::InputText:
				case ConsItem::ExprType:
					break;
				}
			}
			break;

		case SendType:
		case SendTreeType:
		case EmbedStringType:
			break;

		case CastType:
			typeRef->resolveType( pd );
			expr->resolve( pd );
			break;
	}
}

void LangVarRef::resolve( Compiler *pd ) const
{
}

void LangExpr::resolve( Compiler *pd ) const
{
	switch ( type ) {
		case BinaryType: {
			left->resolve( pd );
			right->resolve( pd );
			break;
		}
		case UnaryType: {
			right->resolve( pd );
			break;
		}
		case TermType: {
			term->resolve( pd );
			break;
		}
	}
}

void IterCall::resolve( Compiler *pd ) const
{
	switch ( form ) {
		case Call:
			langTerm->resolve( pd );
			break;
		case Expr:
			langExpr->resolve( pd );
			break;
	}
}

void LangStmt::resolveForIter( Compiler *pd ) const
{
	iterCall->resolve( pd );

	/* Search type ref. */
	typeRef->resolveType( pd );

	/* Iterator type ref. */
	objField->typeRef->resolveType( pd );

	/* Resolve the statements. */
	for ( StmtList::Iter stmt = *stmtList; stmt.lte(); stmt++ )
		stmt->resolve( pd );
}

void LangStmt::resolve( Compiler *pd ) const
{
	switch ( type ) {
		case PrintType: 
		case PrintXMLACType:
		case PrintXMLType:
		case PrintStreamType: {
			/* Push the args backwards. */
			for ( CallArgVect::Iter pex = exprPtrVect->last(); pex.gtb(); pex-- )
				(*pex)->expr->resolve( pd );
			break;
		}
		case PrintAccum:
			break;
		case ExprType: {
			/* Evaluate the exrepssion, then pop it immediately. */
			expr->resolve( pd );
			break;
		}
		case IfType: {
			/* Evaluate the test. */
			expr->resolve( pd );

			/* Analyze the if true branch. */
			for ( StmtList::Iter stmt = *stmtList; stmt.lte(); stmt++ )
				stmt->resolve( pd );

			if ( elsePart != 0 )
				elsePart->resolve( pd );

			break;
		}
		case ElseType: {
			for ( StmtList::Iter stmt = *stmtList; stmt.lte(); stmt++ )
				stmt->resolve( pd );
			break;
		}
		case RejectType:
			break;
		case WhileType: {
			expr->resolve( pd );

			/* Compute the while block. */
			for ( StmtList::Iter stmt = *stmtList; stmt.lte(); stmt++ )
				stmt->resolve( pd );
			break;
		}
		case AssignType: {
			/* Evaluate the exrepssion. */
			expr->resolve( pd );
			break;
		}
		case ForIterType: {
			resolveForIter( pd );
			break;
		}
		case ReturnType: {
			/* Evaluate the exrepssion. */
			expr->resolve( pd );
			break;
		}
		case BreakType: {
			break;
		}
		case YieldType: {
			/* take a reference and yield it. Immediately reset the referece. */
			varRef->resolve( pd );
			break;
		}
	}
}

void ObjectDef::resolve( Compiler *pd )
{
	for ( FieldList::Iter fli = *fieldList; fli.lte(); fli++ ) {
		ObjectField *field = fli->value;

		if ( field->typeRef != 0 )
			field->typeRef->resolveType( pd );
	}
}

void CodeBlock::resolve( Compiler *pd ) const
{
	if ( localFrame != 0 ) {
		localFrame->resolve( pd );
	}

	for ( StmtList::Iter stmt = *stmtList; stmt.lte(); stmt++ )
		stmt->resolve( pd );
}

void Compiler::resolveFunction( Function *func )
{
	if ( func->typeRef != 0 ) 
		func->typeRef->resolveType( this );

	for ( ParameterList::Iter param = *func->paramList; param.lte(); param++ )
		param->typeRef->resolveType( this );

	CodeBlock *block = func->codeBlock;
	block->resolve( this );
}

void Compiler::resolvePreEof( TokenRegion *region )
{
	CodeBlock *block = region->preEofBlock;
	block->resolve( this );
}

void Compiler::resolveRootBlock()
{
	CodeBlock *block = rootCodeBlock;
	block->resolve( this );
}

void Compiler::resolveTranslateBlock( LangEl *langEl )
{
	CodeBlock *block = langEl->transBlock;
	block->resolve( this );
}

void Compiler::resolveReductionCode( Production *prod )
{
	CodeBlock *block = prod->redBlock;
	block->resolve( this );
}

void Compiler::resolveParseTree()
{
	/* Compile functions. */
	for ( FunctionList::Iter f = functionList; f.lte(); f++ )
		resolveFunction( f );

	/* Compile the reduction code. */
	for ( DefList::Iter prod = prodList; prod.lte(); prod++ ) {
		if ( prod->redBlock != 0 )
			resolveReductionCode( prod );
	}

	/* Compile the token translation code. */
	for ( LelList::Iter lel = langEls; lel.lte(); lel++ ) {
		if ( lel->transBlock != 0 )
			resolveTranslateBlock( lel );
	}

	/* Compile preeof blocks. */
	for ( RegionList::Iter r = regionList; r.lte(); r++ ) {
		if ( r->preEofBlock != 0 )
			resolvePreEof( r );
	}

	/* Compile the init code */
	resolveRootBlock( );

	rootLocalFrame->resolve( this );

	/* Init all user object fields (need consistent size). */
	for ( LelList::Iter lel = langEls; lel.lte(); lel++ ) {
		ObjectDef *objDef = lel->objectDef;
		if ( objDef != 0 ) {
			/* Init all fields of the object. */
			for ( FieldList::Iter f = *objDef->fieldList; f.lte(); f++ ) {
				f->value->typeRef->resolveType( this );
			}
		}
	}

	/* Init all fields of the global object. */
	for ( FieldList::Iter f = *globalObjectDef->fieldList; f.lte(); f++ ) {
		f->value->typeRef->resolveType( this );
	}

}

/* Resolves production els and computes the precedence of each prod. */
void Compiler::resolveProductionEls()
{
	/* NOTE: as we process this list it may be growing! */
	for ( DefList::Iter prod = prodList; prod.lte(); prod++ ) {
		/* First resolve. */
		for ( ProdElList::Iter prodEl = *prod->prodElList; prodEl.lte(); prodEl++ )
			resolveProdEl( prodEl );

		/* If there is no explicit precdence ... */
		if ( prod->predOf == 0 )  {
			/* Compute the precedence of the productions. */
			for ( ProdElList::Iter prodEl = prod->prodElList->last(); prodEl.gtb(); prodEl-- ) {
				/* Production inherits the precedence of the last terminal with
				 * precedence. */
				if ( prodEl->langEl->predType != PredNone ) {
					prod->predOf = prodEl->langEl;
					break;
				}
			}
		}
	}
}

void Compiler::resolveGenericTypes()
{
	for ( NamespaceList::Iter ns = namespaceList; ns.lte(); ns++ ) {
		for ( GenericList::Iter gen = ns->genericList; gen.lte(); gen++ ) {
			gen->utArg = gen->typeArg->resolveType( this );

			if ( gen->typeId == GEN_MAP )
				gen->keyUT = gen->keyTypeArg->resolveType( this );
		}
	}
}

void Compiler::makeTerminalWrappers()
{
	/* Make terminal language elements corresponding to each nonterminal in
	 * the grammar. */
	for ( LelList::Iter lel = langEls; lel.lte(); lel++ ) {
		if ( lel->type == LangEl::NonTerm ) {
			String name( lel->name.length() + 5, "_T_%s", lel->name.data );
			LangEl *termDup = new LangEl( lel->nspace, name, LangEl::Term );

			/* Give the dup the attributes of the nonterminal. This ensures
			 * that the attributes are allocated when patterns and
			 * constructors are parsed. */
			termDup->objectDef = lel->objectDef;

			langEls.append( termDup );
			lel->termDup = termDup;
			termDup->termDup = lel;
		}
	}
}

void Compiler::makeEofElements()
{
	/* Make eof language elements for each user terminal. This is a bit excessive and
	 * need to be reduced to the ones that we need parsers for, but we don't know that yet.
	 * Another pass before this one is needed. */
	for ( LelList::Iter lel = langEls; lel.lte(); lel++ ) {
		if ( lel->eofLel == 0 &&
				lel != eofLangEl &&
				lel != errorLangEl &&
				lel != noTokenLangEl /* &&
				!( lel->tokenInstance == 0 || lel->tokenInstance->dupOf == 0 ) */ )
		{
			String name( lel->name.length() + 5, "_eof_%s", lel->name.data );
			LangEl *eofLel = new LangEl( lel->nspace, name, LangEl::Term );

			langEls.append( eofLel );
			lel->eofLel = eofLel;
			eofLel->eofLel = lel;
			eofLel->isEOF = true;
		}
	}
}

void Compiler::resolvePrecedence()
{
	for ( PredDeclList::Iter predDecl = predDeclList; predDecl != 0; predDecl++ ) {
		predDecl->typeRef->resolveType( this );

		LangEl *langEl = predDecl->typeRef->uniqueType->langEl;
		langEl->predType = predDecl->predType;
		langEl->predValue = predDecl->predValue;
	}
}


void Compiler::resolvePass()
{
	/*
	 * Type Resolving.
	 */

	resolvePrecedence();

	resolveParseTree();

	resolveGenericTypes();

	argvTypeRef->resolveType( this );

	/* We must do this as the last step in the type resolution process because
	 * all type resolves can cause new language elments with associated
	 * productions. They get tacked onto the end of the list of productions.
	 * Doing it at the end results processing a growing list. */
	resolveProductionEls();
}
