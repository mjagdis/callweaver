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

" Unknown sections contain mailboxes
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwVMMailbox,cwComment,cwError

syn match   cwVMMailbox     "^\s*\d\+\s*=>\?" contained contains=cwInt,cwMapTo,cwError skipwhite nextgroup=cwVMMBPassword

syn match   cwVMMBPassword  "\d\+" contained contains=cwInt,cwError skipwhite nextgroup=cwVMMBFullName
syn match   cwVMMBFullName  ",[^,;]*" contained contains=cwComma,cwVMMBLit,cwError skipwhite nextgroup=cwVMMBEmail
syn match   cwVMMBEmail     ",[^,;]*" contained contains=cwComma,cwVMMBLit,cwError skipwhite nextgroup=cwVMMBPager
syn match   cwVMMBPager     ",[^,;]*" contained contains=cwComma,cwVMMBLit,cwError skipwhite nextgroup=cwVMMBOption

syn match   cwVMMBLit       "[^,;]\+" contained

syn region  cwVMMBOption    matchgroup=cwDelimiter start="," end="\ze\%([,;]\|$\)" oneline keepend contained contains=cwVMMBOptKV,cwError nextgroup=cwVMMBOption

syn keyword cwVMMBOptKeys   contained
 \ attach
 \ callback
 \ delete
 \ dialout
 \ envelope
 \ exitcontext
 \ forcegreetings
 \ forcename
 \ language
 \ maxmsg
 \ operator
 \ review
 \ saycid
 \ sayduration
 \ saydurationm
 \ sendvoicemail
 \ tz

syn match   cwVMMBOptKV     "\%(attach\|delete\|envelope\|forcename\|forcegreetings\|saycid\|sayduration\|sendvoicemail\|operator\|review\)=" contained contains=cwVMMBOptKeys,cwMapTo,cwError nextgroup=cwBoolean
syn match   cwVMMBOptKV     "\%(maxmsg\|saydurationm\)=" contained contains=cwVMMBOptKeys,cwMapTo,cwError nextgroup=cwInt
syn match   cwVMMBOptKV     "\%(callback\|dialout\|exitcontext\|language\|tz\)=" contained contains=cwVMMBOptKeys,cwMapTo,cwError nextgroup=cwVMMBLit


" [general] contains general options
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[general\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwVMGeneral,cwComment,cwError

syn keyword cwVMGeneralKeys contained
 \ adsifdn
 \ adsisec
 \ adsiver
 \ attach
 \ callback
 \ charset
 \ cidinternalcontexts
 \ dialout
 \ emailbody
 \ emaildateformat
 \ emailsubject
 \ emailtitle
 \ envelope
 \ exitcontext
 \ externnotify
 \ externpass
 \ forcegreetings
 \ forcename
 \ format
 \ fromstring
 \ mailcmd
 \ maxgreet
 \ maxlogins
 \ maxmessage
 \ maxmsg
 \ maxsilence
 \ minmessage
 \ nextaftercmd
 \ odbcstorage
 \ odbctable
 \ operator
 \ pagerbody
 \ pagerfromstring
 \ pagersubject
 \ pbxskip
 \ review
 \ saycid
 \ sayduration
 \ saydurationm
 \ sendvoicemail
 \ serveremail
 \ silencethreshold
 \ skipms
 \ usedirectory

syn match   cwVMGeneral     "^\s*\%(attach\|envelope\|forcegreetings\|forcename\|nextaftercmd\|operator\|pbxskip\|review\|saycid\|sayduration\|sendvoicemail\|usedirectory\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwBoolean
syn match   cwVMGeneral     "^\s*\%(adsiver\|maxgreet\|maxlogins\|maxmessage\|maxmsg\|maxsilence\|minmessage\|saydurationm\|silencethreshold\|skipms\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwInt
syn match   cwVMGeneral     "^\s*\%(callback\|charset\|cidinternalcontexts\|dialout\|emaildateformat\|emailtitle\|exitcontext\|externnotify\|externpass\|fromstring\|mailcmd\|odbcstorage\|odbctable\|pagerfromstring\|serveremail\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwLitText
syn match   cwVMGeneral     "^\s*\%(emailbody\|emailsubject\|pagerbody\|pagersubject\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwSubstText
syn match   cwVMGeneral     "^\s*\%(format\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwFormatList

syn match   cwVMGeneral     "^\s*adsi\%(fdn\|sec\)\s*=>\?" contained contains=cwVMGeneralKeys,cwMapTo,cwError skipwhite nextgroup=cwVMADSIKey
syn match   cwVMADSIKey     "\x{8}" contained


" [zonemessages] contains timezone data
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[zonemessages\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwKeyLitValue,cwComment,cwError


" Highlighting Settings
" =====================

hi def link cwVMMBOptKeys    cwKeyword
hi def link cwVMMBLit        cwLiteral
hi def link cwVMGeneralKeys  cwKeyword
hi def link cwVMADSIKey      cwHex


let b:current_syntax = "cw-voicemail"
