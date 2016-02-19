" Vim syntax file
" Language:	CallWeaver callweaver.conf file
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


" [general], [files] and [directories] are all equivalent
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[\%(general\|files\|directories\)\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwCWGeneral,cwComment,cwError

syn keyword cwCWGeneralKeys contained
 \ cwconfigdir
 \ cwctl
 \ cwctlgroup
 \ cwctlowner
 \ cwctlpermissions
 \ cwdb
 \ cwdbdir
 \ cwetcdir
 \ cwkeydir
 \ cwlogdir
 \ cwmoddir
 \ cwmonitordir
 \ cwogidir
 \ cwpid
 \ cwrundir
 \ cwrungroup
 \ cwrunuser
 \ cwsocket
 \ cwsoundsdir
 \ cwspooldir
 \ cwvardir
 \ cwvarlibdir
 \ systemname

syn match   cwCWGeneral     "^\s*\w\+\s*=>\?" contained contains=cwCWGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwLitText

syn match   cwCWGeneral     "^\s*\%(cwctlgroup\|cwctlowner\|cwrungroup\|cwrunuser\)\s*=>\?" contained contains=cwCWGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwIdent

syn match   cwCWGeneral     "^\s*\%(cwctlpermissions\)\s*=>\?" contained contains=cwCWGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwOctal


" [options] contains global options
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[options\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwCWOptions,cwComment,cwError

syn keyword cwCWOptionKeys contained
 \ cache_record_files
 \ console
 \ debug
 \ dontwarn
 \ dumpcore
 \ enableunsafeunload
 \ execincludes
 \ highpriority
 \ initcrypto
 \ maxcalls
 \ maxload
 \ nofork
 \ quiet
 \ record_cache_dir
 \ systemname
 \ transcode_via_slin
 \ verbose

syn match   cwCWOptions     "^\s*\%(cache_record_files\|console\|dontwarn\|dumpcore\|enableunsafeunload\|execincludes\|highpriority\|initcrypto\|nofork\|quiet\|transcode_via_slin\)\s*=>\?" contained contains=cwCWOptionKeys,cwMapTo,cwError skipwhite nextgroup=cwBoolean

syn match   cwCWOptions     "^\s*\%(debug\)\s*=>\?" contained contains=cwCWOptionKeys,cwMapTo,cwError skipwhite nextgroup=cwBoolean,cwInt

syn match   cwCWOptions     "^\s*\%(maxcalls\|verbose\)\s*=>\?" contained contains=cwCWOptionKeys,cwMapTo,cwError skipwhite nextgroup=cwInt

syn match   cwCWOptions     "^\s*\%(maxload\)\s*=>\?" contained contains=cwCWOptionKeys,cwMapTo,cwError skipwhite nextgroup=cwNumber

syn match   cwCWOptions     "^\s*\%(record_cache_dir\|systemname\)\s*=>\?" contained contains=cwCWOptionKeys,cwMapTo,cwError skipwhite nextgroup=cwLitText


" Highlighting Settings
" =====================

hi def link cwCWGeneralKeys  cwKeyword
hi def link cwCWOptionKeys   cwKeyword


let b:current_syntax = "cw-sip"
