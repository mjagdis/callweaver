" Vim syntax file
" Language:	CallWeaver extensions.conf file
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


" In general sections are dialplan contexts containing extension, procs, etc.
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\s*$" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\s*$" keepend contained contains=cwPreProc,cwExten,cwComment,cwError

syn keyword cwExtenWords    contained eswitch exten ignorepat include lswitch same switch
syn match   cwExten         "^\s*exten\s*=>\?" contained contains=cwExtenWords,cwMapTo skipwhite nextgroup=cwPattern
syn match   cwExten         "^\s*same\s*=>\?"  contained contains=cwExtenWords,cwMapTo skipwhite nextgroup=cwPriority
syn match   cwExten         "^\s*\%(ignorepat\|include\|switch\|lswitch\|eswitch\)\s*=>\?" contained contains=cwExtenWords,cwMapTo skipwhite nextgroup=cwSubstText

syn match   cwPatternRange  "\[\d\+\-\d\+\]" contained
syn match   cwPatternText   "(\$[^{[]\|[^\"'[]" contained
syn match   cwPatternMatch  "[NXZ.~!]" contained
syn match   cwPatternIgnore "[\s-]" contained
syn cluster cwPatternData   contains=cwSubst,cwExpr,cwString,cwLiteral,cwPatternRange,cwPatternText,cwPatternMatch,cwPatternIgnore
syn match   cwPattern       "_[^\.~!,]*[\.~!]\?" contained contains=@cwPatternData skipwhite nextgroup=cwPatternTail
syn match   cwPattern       "[^_][^,]*" contained contains=cwSubst,cwExpr,cwString,cwLiteral skipwhite nextgroup=cwPatternTail
syn match   cwPatternTail   "," contained skipwhite nextgroup=cwPriority

syn keyword cwPriorityWords contained hint n[ext] s[ame]
syn match   cwPriorityLabel "([_[:alpha:]][_\-[:alnum:]]*)" contained contains=cwIdent
syn match   cwPriority      "\%(\%(n\%(ext\)\?\)\|\%(s\%(ame\)\?\)\|[[:digit:]]\+\%(\s*+\s*[[:digit:]]\+\)\?\)[^,]*" contained contains=cwPriorityWords,cwInt,cwPriorityLabel skipwhite nextgroup=cwPriorityTail
syn match   cwPriorityTail  "," contained skipwhite nextgroup=@cwApp
syn match   cwPriority      "hint[^,]*" contained contains=cwPriorityWords,cwPriorityLabel skipwhite nextgroup=cwHintTail
syn match   cwHintTail      "," contained skipwhite nextgroup=cwSubstText

syn cluster cwApp           contains=cwFunc,cwBareFunc


" [general] contains configuration settings
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[general\]\%(([^)]*)\)\?\s*$" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\s*$" contained contains=cwPreProc,cwExtConfig,cwComment,cwError

syn keyword cwExtConfigWords contained autofallthrough clearglobalvars priorityjumping static writeprotect
syn match   cwExtConfig     "^\s*\%(autofallthrough\|clearglobalvars\|priorityjumping\|static\|writeprotect\)\s*=>\?" contained contains=cwExtConfigWords,cwMapTo skipwhite nextgroup=cwBoolean


" [globals] contains a list of global variables as name=value tuples
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[globals\]\%(([^)]*)\)\?\s*$" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\s*$" contained contains=cwPreProc,cwKeySubstValue,cwComment,cwError


" ============================================================================
" Highlighting Settings
" ============================================================================

hi def link cwExtConfigWords cwKeyword
hi def link cwExten          Statement
hi def link cwExtenWords     cwKeyword
hi def link cwPatternRange   cwPatternMatch
hi def link cwPatternText    cwLiteral
hi def link cwPatternMatch   PreProc
" Logically this should be "Ignore" but that makes it invisible :-(
hi def link cwPatternIgnore  NonText
hi def link cwPattern        cwLiteral
hi def link cwPriorityWords  Keyword
hi def link cwPriorityLabel  Label
hi def link cwBareFunc       cwFunc


let b:current_syntax = "cw-extensions"
