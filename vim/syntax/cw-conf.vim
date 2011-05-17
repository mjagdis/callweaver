" Vim syntax file
" Language:	CallWeaver config files
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

if version < 600
  syntax clear
elseif exists("b:current_syntax")
  finish
endif

if !exists('g:cw_no_syntax_folding')
  setlocal foldmethod=syntax
endif

syn sync fromstart

" Spell checking is off for everything except comments.
" There is, possibly, an argument to be made for turning it on in strings?
syn spell notoplevel

setlocal iskeyword+=.,-


" Anything not otherwise recognised is an error
syn match   cwError         "\S"


if !exists('g:cw_no_bare_strings')
  syn match   cwBareString    "\S[[:alnum:]]*" contained
else
  syn match   cwBareString    "[_[:alpha:]][[:alnum:]_.-]*" contained
endif
syn match   cwBareString    "\\." contained contains=cwBackslash
syn match   cwBackslash     "\\" contained

syn match   cwEscChar       "\\[nrt\\]" contained


syn match   cwAt            "@" contained
syn match   cwColon         ":" contained
syn match   cwComma         "," contained
syn match   cwDash          "-" contained
syn match   cwMapTo         "=>\?" contained


syn match   cwIdent         "\K\k*" contained
syn match   cwKeyword       "\K\k*" contained
syn match   cwBoolean       "\c\%(y\%(es\)\?\|t\%(rue\)\?\|1\|on\|no\?\|f\%(alse\)\?\|0\|off\)" contained
syn match   cwNumber        "-\?\d\+\%(\.\d\+\)\?\%([eE]-\?\d\+\)\?\ze\%([^.eE]\|$\)" contained
syn match   cwInt           "-\?\d\+\ze\%([^.eE]\|$\)" contained
syn match   cwUInt16        "\d\{1,5}\ze\%([^.eE\d]\|$\)" contained
syn match   cwHex           "0[xX][0-9a-fA-F]\+" contained
syn match   cwOctal         "0[0-7]\+" contained


syn region  cwString        matchgroup=cwError start=+"+ skip=+\\"+ end=+$+ oneline keepend contained contains=cwSubst,cwExpr
syn region  cwString        start=+"+ skip=+\\"+ end=+"+ oneline contained contains=cwSubst,cwExpr
syn region  cwLiteral       matchgroup=cwError start=+'+ end=+$+ oneline keepend contained contains=NONE
syn region  cwLiteral       start=+'+ end=+'+ oneline contained contains=NONE


syn match   cwIntList       "-\?\d\+\%(,-\?\d\+\)*" contained contains=cwInt,cwComma,cwError

syn cluster cwIPPort        contains=cwUInt16,cwLiteral

syn match   cwFQDN          "[[:alnum:]]\+\%(\.[[:alnum:]]\+\)\{-}\.[[:alpha:]]\+\ze\%([^.[:alnum:]]\|$\)" contained
syn match   cwFQDNPort      "[[:alnum:]]\+\%(\.[[:alnum:]]\+\)\{-}\.[[:alpha:]]\+:" contained nextgroup=@cwIPPort

syn match   cwIPv4          "\d\{1,3}\%(\.\d\{1,3}\)\{3}\ze\%([^.\d]\|$\)" contained
syn match   cwIPv4Port      "\d\{1,3}\%(\.\d\{1,3}\)\{3}:" contained nextgroup=@cwIPPort

syn match   cwIPv6          "[0-9a-fA-F]*\%(:[0-9a-fA-F]*\)\{1,7}\%(\%(\.\d\{1,3}\)\{3}\ze\%([^.\d]\|$\)\)\?" contained
syn match   cwIPv6          "\[[0-9a-fA-F]*\%(:[0-9a-fA-F]*\)\{1,7}\%(\%(\.\d\{1,3}\)\{3}\)\?\]" contained
syn match   cwIPv6Port      "\[[0-9a-fA-F]*\%(:[0-9a-fA-F]*\)\{1,7}\%(\%(\.\d\{1,3}\)\{3}\)\?\]:" contained nextgroup=@cwIPPort

syn match   cwSubnet        "\d\{1,3}\%(\.\d\{1,3}\)\{3}/" contained nextgroup=@cwIPv4Mask
syn cluster cwIPv4Mask      contains=cwIPv4,cwInt

syn cluster cwNetAddr       contains=cwFQDN,cwIPv4,cwIPv6
syn cluster cwNetAddrPort   contains=cwFQDNPort,cwIPv4Port,cwIPv6Port
syn cluster cwNetAddrOptPort contains=cwFQDNPort,cwIPv4Port,cwIPv4,cwIPv6Port,cwIPv6


syn keyword cwCIDPresentation contained
				\ allowed_not_screened allowed_passed_screen allowed_failed_screen allowed
				\ prohib_not_screened prohib_passed_screen prohib_failed_screen prohib
				\ unavailable

syn keyword cwCDRAMAFlags   contained billing default documentation omit

syn keyword cwCodec         contained
				\ all slinear g723.1 g711u g711a
				\ g723 gsm ulaw alaw g726 dvi slin lpc10 g729 speex ilbc oki g722
				\ jpeg png
				\ h261 h263 h263p h264
syn match   cwCodecList     "\k\+\%(,\k\+\)*" contained contains=cwCodec,cwComma,cwError


syn keyword cwFormat         contained
				\ al alaw au g723 g723.1 g726-16 g726-24 g726-32 g726-40 g729 gsm
				\ h263 mu ogg pcm raw sln ul ulaw WAV wav wav49
syn match   cwFormatList    "\k\+\%(,\k\+\)*" contained contains=cwFormat,cwComma,cwError

syn match   cwGroup         "\d\+\%(-\d\+\)\?\%(,\d\+\%(-\d\+\)\?\)*" contained contains=cwInt,cwComma

syn keyword cwTOSKey         contained lowdelay mincost none reliability throughput
syn match   cwTOS            "\k\+" contained contains=cwHex,cwOctal,cwInt,cwTOSKey,cwError


syn keyword cwPreProcWords  transparent contained include exec
syn match   cwPreProc       "^#\s*\%(include\|exec\)\s\+.*$" contains=cwPreProcWords,cwComment

syn match   cwComment       ";.*$" contains=cwTodo,@Spell
syn keyword cwTodo          contained FIXME TODO XXX


if !exists('g:cw_no_syntax_folding')
  syn region  cwSectionBlock  start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" end="\ze\_^\s*\%(\%(\r\?\n\s*\)*;!\|\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\)" keepend contains=cwSection fold
else
  syn region  cwSectionBlock  start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" end="\ze\_^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contains=cwSection
endif


" Unknown sections are errors. The more specific syntax files are
" responsible for overriding this as appropriate.
syn region  cwSection       matchgroup=cwError start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwComment,cwError


syn cluster cwData          contains=cwSubst,cwExpr,cwString,cwLiteral,cwNumber,cwIPv4,cwFQDN,cwGroupingError,cwBareString,cwEscCharcwError

syn match   cwBareFunc      "\K\k*\ze\s*\%(;\|$\)" contained contains=cwString,cwLiteral

syn match  cwFunc           "\K\k*\ze\s*(" contained contains=cwString,cwLiteral,cwSubst,cwExpr nextgroup=cwArgs
syn region cwArgs           matchgroup=cwError start="(" end="$" oneline keepend contained contains=@cwArgsList
syn region cwArgs           matchgroup=cwDelimiter start="(" end=")" oneline contained contains=@cwArgsList
syn cluster cwArgsList      contains=@cwData,cwComma
syn region  cwDataParen     matchgroup=Delimiter start="(" end=")" oneline contained contains=@cwArgsList

" Special handling for Dial({ X, Y, ... }, ...)
syn match  cwFunc           "Dial\ze\s*(" contained nextgroup=cwArgsDialChan
syn region cwArgsDialChan   matchgroup=cwError start="(" end="$" oneline keepend contained contains=@cwData,cwDataSet
syn region cwArgsDialChan   matchgroup=cwDelimiter start="(" end=")" oneline contained contains=@cwData,cwDataSet
syn region cwArgsDialChan   matchgroup=cwDelimiter start="(" end="\ze," oneline contained contains=@cwData,cwDataSet nextgroup=cwArgsDialOpts
syn region  cwDataSet       matchgroup=Delimiter start="{" end="}" oneline contained contains=@cwArgsList
syn region cwArgsDialOpts   matchgroup=cwError start="," end="$" oneline keepend contained contains=@cwArgsList,cwDataParen
syn region cwArgsDialOpts   matchgroup=cwDelimiter start="," end=")" oneline contained contains=@cwArgsList,cwDataParen

" Special handling for Gosub(...(arg, ...))
syn match  cwFunc           "Gosub\ze\s*(" contained nextgroup=cwArgsGosub
syn region cwArgsGosub      matchgroup=cwError start="(" end="$" oneline keepend contained contains=@cwArgsList,cwDataParen
syn region cwArgsGosub      matchgroup=cwDelimiter start="(" end=")" oneline contained contains=@cwArgsList,cwDataParen

" Special handling for Set(X=Y)
" X is ident text rather than bare string, but may contain substitution and
" expression expansions.
syn match  cwFunc           "Set\ze(\s*" contained nextgroup=cwArgsSetName
syn region cwArgsSetName    matchgroup=cwError start="(" end="$" oneline keepend contained contains=cwIdent,cwFunc,@cwData
syn region cwArgsSetName    matchgroup=cwDelimiter start="(" end="\ze=" oneline contained contains=cwIdent,cwFunc,@cwData nextgroup=cwArgsSetValue
syn region cwArgsSetValue   matchgroup=cwError start="=" end="$" oneline keepend contained contains=@cwData,cwComma
syn region cwArgsSetValue   matchgroup=cwDelimiter start="=" end=")" oneline contained contains=@cwData,cwComma

syn region cwSubst          matchgroup=cwDelimiter start="\${" skip="\\\$" end="}" oneline contained contains=cwFunc,cwIdent,cwString,cwLiteral,cwColon,cwInt,cwSubst,cwExpr,cwError

syn cluster cwExprStuff     contains=@cwData,cwFunc,cwExprOp,cwExprParen,cwError
syn match   cwExprOp        "||\|&&\|==\|=\~\|>=\|<=\|!=\|::\|[|&=<>!?:+\-*/%]" contained
syn region  cwExprParen     matchgroup=cwDelimiter start="(" end=")" oneline contained contains=@cwExprStuff
syn region  cwExpr          matchgroup=cwDelimiter start="\$\[" skip="\\\$" end="]" oneline contained contains=@cwExprStuff

syn match   cwGroupingError "[)}\]]"

syn match   cwKeySubstValue "^\s*\K\k*\s*=>\?" contained contains=cwKeyword,cwMapTo skipwhite nextgroup=cwSubstText
syn region  cwSubstText     start="[^;]" end="\ze\s*\%(;\|$\)" oneline contained contains=cwSubst,cwExpr,cwString,cwLiteral,cwEscChar,cwBackslash

syn match   cwKeySplitValue "^\s*\K\k*\s*=>\?" contained contains=cwKeyword,cwMapTo skipwhite nextgroup=cwSplitText
syn region  cwSplitText     start="[^;]" end="\ze\s*\%(;\|$\)" oneline contained contains=cwString,cwLiteral,cwEscChar,cwBackslash,cwComma

syn match   cwKeyLitValue   "^\s*\K\k*\s*=>\?" contained contains=cwKeyword,cwMapTo skipwhite nextgroup=cwLitText
syn match   cwLitText       ".\{-}\ze\s*\%(;\|$\)" contained


" ============================================================================
" GENERIC JITTERBUFFER OPTIONS
" ============================================================================

syn keyword cwGenericJBKeys  contained
 \ jb-enable
 \ jb-force
 \ jb-impl
 \ jb-log
 \ jb-max-size
 \ jb-min-size
 \ jb-resynch-threshold
 \ jb-timing-compensation

syn match   cwGenericJB      "^\s*\%(jb-\%(enable\|force\|log\)\)\s*=>\?" contained contains=cwGenericJBKeys,cwMapTo skipwhite nextgroup=cwBoolean

syn match   cwGenericJB      "^\s*\%(jb-\%(\%(max\|min\)-size\|resynch-threshold\|timing-compensation\)\)\s*=>\?" contained contains=cwGenericJBKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwGenericJB      "^\s*jb-impl\s*=>\?" contained contains=cwGenericJBKeys,cwMapTo skipwhite nextgroup=cwLitText


" ============================================================================
" Highlighting Settings
" ============================================================================

if !exists('g:cw_no_bare_strings')
  hi def link cwBareString    NONE
else
  hi def link cwBareString    cwIdent
endif
hi def link cwBackslash     Special
hi def link cwBareFunc      cwFunc
hi def link cwBoolean       Boolean
hi def link cwCDRAMAFlags   cwKeyword
hi def link cwCodec         cwKeyword
hi def link cwEscChar       Special
hi def link cwFormat        cwKeyword
hi def link cwFQDN          Constant
hi def link cwFunc          Type
hi def link cwHex           Number
hi def link cwIdent         Identifier
hi def link cwInt           Number
hi def link cwIPv4          Constant
hi def link cwIPv4Port      Constant
hi def link cwIPv6          Constant
hi def link cwIPv6Port      Constant
hi def link cwKeyword       Type
hi def link cwLiteral       Constant
hi def link cwNumber        Float
hi def link cwOctal         Number
hi def link cwString        String
hi def link cwTOSKey        cwKeyword
hi def link cwUInt16        Number

hi def link cwError         Error
hi def link cwPreProc       PreProc

hi def link cwComment	    Comment
hi def link cwTodo          Todo

hi def link cwSectionName   Folded

hi def link cwDelimiter     Delimiter
hi def link cwExprOp        Operator

hi def link cwGroupingError cwError

hi def link cwSubstText     NONE
hi def link cwSplitText     Constant
hi def link cwLitText       Constant

hi def link cwGenericJBKeys cwKeyword


let b:current_syntax = "cw-conf"
