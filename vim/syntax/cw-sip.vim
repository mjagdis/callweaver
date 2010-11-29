" Vim syntax file
" Language:	CallWeaver sip.conf file
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


syn cluster cwSipQualify     contains=cwBoolean,cwInt


" Unidentified sections are considered to be "type = friend"
" User and peer sections are only identified if the "type =" is the very
" first thing after the section header.
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwSipCommon,cwSipFriend,cwSipPeer,cwSipUser,cwComment,cwError

syn keyword cwSipFriendKeys contained
 \ accountcode
 \ amaflags
 \ call-limit
 \ callgroup
 \ deny
 \ incominglimit
 \ md5secret
 \ permit
 \ pickupgroup
 \ secret
 \ setvar
 \ type

syn match   cwSipFriend     "^\s*\%(type\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwSipSectType
syn keyword cwSipSectType   contained friend peer user

syn match   cwSipFriend     "^\s*\%(accountcode\|md5secret\|secret\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwLitText

syn match   cwSipFriend      "^\s*\%(permit\|deny\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwSubnet

syn match   cwSipFriend      "^\s*\%(call-limit\|incominglimit\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwSipFriend      "^\s*amaflags\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwCDRAMAFlags

syn match   cwSipFriend      "^\s*\%(callgroup\|pickupgroup\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwGroup

syn match   cwSipFriend      "^\s*\%(setvar\)\s*=>\?" contained contains=cwSipFriendKeys,cwMapTo skipwhite nextgroup=cwKeyLitValue


" Peer sections have "type = peer" as the very first thing after the section
" header
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\ze\s*\%(;.*\)\?\r\?\n\s*type\s*=>\?peer\s*\%(;\|$\)" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwSipCommon,cwSipFriend,cwSipPeer,cwSipAuthentication,cwComment,cwError

syn keyword cwSipPeerKeys   contained
 \ callerid
 \ callingpres
 \ context
 \ defaultip
 \ fromdomain
 \ fromuser
 \ fullcontact
 \ host
 \ ipaddr
 \ mailbox
 \ name
 \ outboundproxy
 \ port
 \ qualify
 \ regexten
 \ regseconds
 \ rtpholdtimeout
 \ rtpkeepalive
 \ rtptimeout
 \ subscribecontext
 \ usereqphone
 \ username
 \ vmexten

syn match   cwSipPeer        "^\s*\%(callerid\|context\|fromdomain\|fromuser\|fullcontact\|mailbox\|name\|regexten\|subscribecontext\|username\|vmexten\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwLitText

syn match   cwSipPeer        "^\s*\%(usereqphone\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwBoolean

syn match   cwSipPeer        "^\s*\%(regseconds\|rtpholdtimeout\|rtpkeepalive\|rtptimeout\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwSipPeer        "^\s*\%(ipaddr\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwIPv4

syn match   cwSipPeer        "^\s*\%(host\|outboundproxy\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=@cwSipPeerHost
syn cluster cwSipPeerHost    contains=@cwNetAddr,cwSipPeerDynamic
syn keyword cwSipPeerDynamic contained dynamic

syn match   cwSipPeer        "^\s*\%(defaultip\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=@cwNetAddr

syn match   cwSipPeer        "^\s*\%(port\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwIPPort

syn match   cwSipPeer        "^\s*callingpres\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=cwCIDPresentation

syn match   cwSipPeer        "^\s*\%(qualify\)\s*=>\?" contained contains=cwSipPeerKeys,cwMapTo skipwhite nextgroup=@cwSipQualify


" User sections have "type = user" as the very first thing after the section
" header
syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?\ze\s*\%(;.*\)\?\r\?\n\s*type\s*=>\?user\s*\%(;\|$\)" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwSipCommon,cwSipFriend,cwSipUser,cwComment,cwError

syn keyword cwSipUserKeys   contained
 \ callerpres

syn match   cwSipUser        "^\s*callerpres\s*=>\?" contained contains=cwSipUserKeys,cwMapTo skipwhite nextgroup=cwCIDPresentation


" [authentication] contains authentication settings
syn region cwSection        matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[authentication\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwSipAuthentication,cwComment,cwError

syn keyword cwSipAuthKeys   contained
 \ auth

syn match cwSipAuthentication "^\s*\%(auth\)\s*=>\?" contained contains=cwSipAuthKeys,cwMapTo skipwhite nextgroup=cwSipAuth
syn match cwSipAuth           "[^:@;]\+:[^@;]\+@[^;]\+" contained contains=cwIdent,cwBareString,cwColon,cwAt,cwFQDN


" [general] contains configuration settings
syn region cwSection        matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[general\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwSipGeneral,cwSipCommon,cwGenericJB,cwComment,cwError

syn keyword cwSipGeneralKeys contained
 \ allowexternaldomains
 \ alwaysauthreject
 \ autocreatepeer
 \ autodomain
 \ bindaddr
 \ bindport
 \ callerid
 \ callevents
 \ checkmwi
 \ compactheaders
 \ context
 \ defaultexpirey
 \ defaultexpiry
 \ domain
 \ dumphistory
 \ externhost
 \ externip
 \ externrefresh
 \ fromdomain
 \ ignoreregexpire
 \ localnet
 \ maxexpirey
 \ maxexpiry
 \ maxinvitetries
 \ notifymimetype
 \ notifyringing
 \ outboundproxy
 \ outboundproxyport
 \ pedantic
 \ qualify
 \ realm
 \ recordhistory
 \ regcontext
 \ register
 \ registerattempts
 \ registertimeout
 \ relaxdtmf
 \ rtautoclear
 \ rtcachefriends
 \ rtpholdtimeout
 \ rtpkeepalive
 \ rtptimeout
 \ rtupdate
 \ sipdebug
 \ srvlookup
 \ stunserver_host
 \ stunserver_port
 \ t38rtpsupport
 \ t38tcpsupport
 \ t38udptlsupport
 \ tos
 \ useragent
 \ usereqphone
 \ videosupport
 \ vmexten

syn match   cwSipGeneral     "^\s*\%(callerid\|context\|fromdomain\|notifymimetype\|realm\|regcontext\|useragent\|vmexten\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwLitText

syn match   cwSipGeneral     "^\s*\%(checkmwi\|defaultexpirey\|defaultexpiry\|externrefresh\|maxexpirey\|maxexpiry\|maxinvitetries\|registerattempts\|registertimeout\|rtpholdtimeout\|rtpkeepalive\|rtptimeout\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwSipGeneral     "^\s*\%(allowexternaldomains\|alwaysauthreject\|autocreatepeer\|autodomain\|callevents\|compactheaders\|dumphistory\|ignoreregexpire\|notifyringing\|pedantic\|recordhistory\|relaxdtmf\|rtcachefriends\|rtupdate\|sipdebug\|srvlookup\|t38rtpsupport\|t38tcpsupport\|t38udptlsupport\|usereqphone\|videosupport\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwBoolean

syn match   cwSipGeneral     "^\s*\%(rtautoclear\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=@cwSipRTAutoClear
syn cluster cwSipRTAutoClear contains=cwInt,cwBoolean

syn match   cwSipGeneral     "^\s*\%(bindaddr\|externhost\|externip\|outboundproxy\|stunserver_host\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=@cwNetAddr

syn match   cwSipGeneral     "^\s*\%(outboundproxyport\|stunserver_port\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwIPPort

syn match   cwSipGeneral     "^\s*\%(localnet\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwSubnet

syn match   cwSipGeneral     "^\s*\%(domain\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwSipDomain
syn match   cwSipDomain     ".\{-}\ze\s*\%(;\|$\)" contained contains=@cwNetAddr,cwComma,cwIdent

syn match   cwSipGeneral     "^\s*\%(register\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwSipRegisterU
" user[:secret[:authuser]]@host[:port][/extension]
syn match   cwSipRegisterU   "[^:@]\+" contained contains=cwIdent,cwBareString nextgroup=cwSipRegisterS
syn match   cwSipRegisterS   "\%(:[^:@]\+\)\?" contained contains=cwColon,cwIdent,cwBareString nextgroup=cwSipRegisterA
syn match   cwSipRegisterA   "\%(:[^:@]\+\)\?" contained contains=cwColon,cwIdent,cwBareString nextgroup=cwSipRegisterH
syn match   cwSipRegisterH   "@[^:/;]\+" contained contains=cwAt,@cwNetAddr,cwError nextgroup=cwSipRegisterP
syn match   cwSipRegisterP   "\%(:\d\{1,5}\)\?" contained contains=cwColon,cwIPPort,cwError nextgroup=cwSipRegisterE
syn match   cwSipRegisterE  "/" contained nextgroup=cwLitText
" syn match   cwSipRegister    "[^:@]\+\%(:[^:@]\+\%(:[^:@]\+\)\?\)\?@[^:/;]\+\%(:\d\{1,5}\)\?\%(.\{-}\ze\s*\%(;\|$\)\)\?" contained contains=cwIdent,@cwNetAddr,cwIPPort,cwBareString,cwError

syn match   cwSipGeneral     "^\s*\%(tos\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwTOS

syn match   cwSipGeneral     "^\s*\%(bindport\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=cwIPPort

syn match   cwSipGeneral     "^\s*\%(qualify\)\s*=>\?" contained contains=cwSipGeneralKeys,cwMapTo skipwhite nextgroup=@cwSipQualify


" ============================================================================
" COMMON OPTIONS
" ============================================================================

syn keyword cwSipCommonKeys  contained
 \ allow
 \ allowguest
 \ canreinvite
 \ disallow
 \ dtmfmode
 \ insecure
 \ language
 \ musicclass
 \ musiconhold
 \ nat
 \ ospauth
 \ progressinband
 \ promiscredir
 \ rtt
 \ sendrpid
 \ timer_t1
 \ timer_t2
 \ trustrpid
 \ useclientcode

syn match   cwSipCommon      "^\s*\%(language\|musicclass\|musiconhold\)\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=cwLitText

syn match   cwSipCommon      "^\s*\%(promiscredir\|sendrpid\|trustrpid\|useclientcode\)\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=cwBoolean

syn match   cwSipCommon      "^\s*\%(rtt\|timer_t1\|timer_t2\)\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwSipCommon      "^\s*allowguest\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipAllowGuest
syn cluster cwSipAllowGuest  contains=cwSipAllowGuestW,cwBoolean
syn keyword cwSipAllowGuestW contained osp

syn match   cwSipCommon      "^\s*canreinvite\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipCanReinv
syn cluster cwSipCanReinv    contains=cwSipCanReinvW,cwBoolean
syn keyword cwSipCanReinvW   contained update

syn match   cwSipCommon      "^\s*dtmfmode\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=cwDTMFMode
syn keyword cwDTMFMode       contained auto inband info rfc2833

syn match   cwSipCommon      "^\s*insecure\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipInsecure
syn cluster cwSipInsecure    contains=cwSipInsecureW,cwBoolean
syn match   cwSipInsecureW   "very\|\%(port\|invite\)\%(,\%(port\|invite\)\)\?" contained

syn match   cwSipCommon      "^\s*nat\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipNAT
syn cluster cwSipNAT         contains=cwSipNATWords,cwBoolean
syn keyword cwSipNATWords    contained never route

syn match   cwSipCommon      "^\s*ospauth\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipOSPAuth
syn keyword cwSipOSPAuth     contained proxy gateway exclusive

syn match   cwSipCommon      "^\s*progressinband\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=@cwSipProgInband
syn cluster cwSipProgInband  contains=cwSipProgInbandW,cwBoolean
syn keyword cwSipProgInbandW contained never

syn match   cwSipCommon      "^\s*\%(allow\|disallow\)\s*=>\?" contained contains=cwSipCommonKeys,cwMapTo skipwhite nextgroup=cwCodecList


" ============================================================================
" Highlighting Settings
" ============================================================================

hi def link cwSipSectType    cwKeyword
hi def link cwSipAuthKeys    cwKeyword
hi def link cwSipFriendKeys  cwKeyword
hi def link cwSipPeerKeys    cwKeyword
hi def link cwSipPeerDynamic cwKeyword
hi def link cwSipUserKeys    cwKeyword
hi def link cwSipGeneralKeys cwKeyword
hi def link cwSipCommonKeys  cwKeyword
hi def link cwDTMFMode       cwKeyword
hi def link cwSipNATWords    cwKeyword
hi def link cwSipCanReinvW   cwKeyword
hi def link cwSipInsecureW   cwKeyword
hi def link cwSipProgInbandW cwKeyword
hi def link cwSipAllowGuestW cwKeyword
hi def link cwSipOSPAuth     cwKeyword


let b:current_syntax = "cw-sip"
