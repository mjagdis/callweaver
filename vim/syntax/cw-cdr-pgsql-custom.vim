" Vim syntax file
" Language:	CallWeaver cdr_pgsql_custom.conf file
" Maintainer:	Mike Jagdis <mjagdis@eris-associates.co.uk>
" Last Change:	May 24, 2010
" Version: 2.0
"
" Syntax folding is on by default but can be turned off by:
"
"     let g:cw_no_syntax_folding = 1
"
" before the syntax file gets loaded (e.g. in ~/.vimrc)
"
" Folds start at section headings ("[...]") so comments for sections should come
" _after_ the heading and not before.
"
" Historically CallWeaver has treated all kinds of sequences of characters as
" strings. If you are rigorous about enclosing strings in quotes (and you
" SHOULD be if you want to avoid parsing problems) you may want to add:
"
"     let g:cw_no_bare_strings = 1
"
" This will limit the sort of things that are recognised outside of quotes and
" will treat such things as identifiers rather than strings.

runtime! syntax/cw-conf.vim
unlet b:current_syntax


syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[global\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwKeyLitValue,cwComment,cwError


syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[master\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwCDRPgSQLCust,cwComment,cwError

syn keyword cwCDRPgSQLCustKeys contained
 \ columns
 \ table
 \ values

syn match   cwCDRPgSQLCust  "^\s*\%(columns\)\s*=>\?" contained contains=cwCDRPgSQLCustKeys,cwMapTo skipwhite nextgroup=cwSplitText
syn match   cwCDRPgSQLCust  "^\s*\%(table\)\s*=>\?" contained contains=cwCDRPgSQLCustKeys,cwMapTo skipwhite nextgroup=cwLitText
syn match   cwCDRPgSQLCust  "^\s*\%(values\)\s*=>\?" contained contains=cwCDRPgSQLCustKeys,cwMapTo skipwhite nextgroup=cwSubstText


" ============================================================================
" Highlighting Settings
" ============================================================================

hi def link cwCDRPgSQLCustKeys cwKeyword


let b:current_syntax = "cw-cdr-pgsql-custom"
