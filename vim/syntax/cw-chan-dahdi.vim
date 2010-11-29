" Vim syntax file
" Language:	CallWeaver chan_dahdi.conf file
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


syn case ignore

syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[channels\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwGenericJB,cwChanDAHDIChans,cwComment,cwError

syn keyword cwChanDAHDIChansKeys contained
 \ accountcode
 \ adsi
 \ amaflags
 \ answeronpolarityswitch
 \ busycount
 \ busydetect
 \ busypattern
 \ cadence
 \ callerid
 \ callgroup
 \ callprogress
 \ callreturn
 \ callwaiting
 \ callwaitingcallerid
 \ cancallforward
 \ canpark
 \ channel
 \ cid_rxgain
 \ cidsignalling
 \ cidstart
 \ context
 \ crv
 \ debounce
 \ defaultcic
 \ defaultozz
 \ dring1cadence
 \ dring2cadence
 \ dring3cadence
 \ dring1context
 \ dring2context
 \ dring3context
 \ dring1exten
 \ dring2exten
 \ dring3exten
 \ distinctiveringaftercid
 \ echocancel
 \ echocancelwhenbridged
 \ echotraining
 \ exten
 \ facilityenable
 \ faxdetect
 \ flash
 \ group
 \ hanguponpolarityswitch
 \ hidecallerid
 \ idledial
 \ idleext
 \ immediate
 \ internationalprefix
 \ jitterbuffers
 \ language
 \ localprefix
 \ mailbox
 \ minused
 \ musiconhold
 \ nationalprefix
 \ nsf
 \ overlapdial
 \ pickupgroup
 \ polarityevents
 \ polarityonanswerdelay
 \ preflash
 \ prewink
 \ pridialplan
 \ priexclusive
 \ priindication
 \ prilocaldialplan
 \ pritimer
 \ privateprefix
 \ progzone
 \ pulsedial
 \ relaxdtmf
 \ resetinterval
 \ restrictcid
 \ ringtimeout
 \ rxflash
 \ rxgain
 \ rxwink
 \ sendcalleridafter
 \ signalling
 \ start
 \ stripmsd
 \ switchtype
 \ threewaycalling
 \ toneduration
 \ tonezone
 \ transfer
 \ transfertobusy
 \ txgain
 \ unknownprefix
 \ usecallerid
 \ usecallingpres
 \ usedistinctiveringdetection
 \ useincomingcalleridondahditransfer
 \ wink

syn match   cwChanDAHDIChans    "^\s*\%(adsi\|answeronpolarityswitch\|busydetect\|callprogress\|callreturn\|callwaiting\|callwaitingcallerid\|cancallforward\|canpark\|distinctiveringaftercid\|echocancelwhenbridged\|facilityenable\|hanguponpolarityswitch\|hidecallerid\|immediate\|overlapdial\|priexclusive\|polarityevents\|pulsedial\|relaxdtmf\|restrictcid\|threewaycalling\|transfer\|transfertobusy\|usecallerid\|usecallingpres\|usedistinctiveringdetection\|useincomingcalleridondahditransfer\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwBoolean

syn match   cwChanDAHDIChans    "^\s*\%(accountcode\|callerid\|context\|defaultcic\|defaultozz\|dring[123]context\|dring[123]exten\|exten\|idledial\|idleext\|internationalprefix\|language\|localprefix\|mailbox\|musiconhold\|nationalprefix\|privateprefix\|progzone\|unknownprefix\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwLitText

syn match   cwChanDAHDIChans    "^\s*\%(amaflags\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwCDRAMAFlags

syn match   cwChanDAHDIChans    "^\s*\%(busycount\|debounce\|flash\|minused\|jitterbuffers\|polarityonanswerdelay\|preflash\|prewink\|ringtimeout\|rxflash\|rxwink\|sendcalleridafter\|start\|stripmsd\|toneduration\|tonezone\|wink\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwInt

syn match   cwChanDAHDIChans    "^\s*\%(cid_rxgain\|rxgain\|txgain\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwNumber

syn match   cwChanDAHDIChans    "^\s*\%(busypattern\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIBusyPat
syn match   cwChanDAHDIBusyPat  "\d\+,\d\+" contained contains=cwInt,cwComma,cwError

syn match   cwChanDAHDIChans    "^\s*cadence\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwIntList

syn match   cwChanDAHDIChans    "^\s*\%(channel\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIChanRange
syn match   cwChanDAHDIChanRange "\d\+\%(-\s*\d\+\)\?\%(\s*,\s*\d\+\%(-\s*\d\+\)\)*" contained contains=cwInt,cwComma,cwDash,cwError

syn match   cwChanDAHDIChans    "^\s*\%(cidsignalling\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDICIDSig
syn keyword cwChanDAHDICIDSig   contained bell dtmf v23

syn match   cwChanDAHDIChans    "^\s*\%(cidstart\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDICIDStart
syn keyword cwChanDAHDICIDStart contained polarity ring

syn match   cwChanDAHDIChans    "^\s*\%(crv\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDICRV
syn match   cwChanDAHDICRV      "\d\+:" contained contains=cwInt,cwColon,cwError skipwhite nextgroup=cwChanDAHDIChanRange

syn match   cwChanDAHDIChans    "^\s*\%(dring[123]cadence\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIDRingCadence
syn match   cwChanDAHDIDRingCadence "\d\+,\d\+,\d\+" contained contains=cwInt,cwComma,cwError

syn match   cwChanDAHDIChans    "^\s*\%(echocancel\|echotraining\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=@cwIntorBoolean
syn cluster cwIntorBoolean       contains=cwInt,cwBoolean

syn match   cwChanDAHDIChans    "^\s*faxdetect\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=@cwChanDAHDIFaxDetect
syn cluster cwChanDAHDIFaxDetect contains=cwChanDAHDIFaxDetectKeys,cwBoolean
syn keyword cwChanDAHDIFaxDetectKeys contained incoming outgoing both

syn match   cwChanDAHDIChans    "^\s*\%(callgroup\|group\|pickupgroup\)\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwGroup

syn match   cwChanDAHDIChans    "^\s*signalling\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDISig
syn keyword cwChanDAHDISig      contained
				\ em em_e1 em_w em_rx em_tx em_rxtx em_txrx
				\ fxs_ls fxs_gs fxs_ks fxs_rx fxs_tx
				\ fxo_ls fxo_gs fxo_ks fxo_rx fxo_tx
				\ sf sf_w sf_featb sf_featd sf_featdmf sf_rx sf_tx sf_rxtx sf_txrx
				\ featd featdmf featdmf_ta featb
				\ e911
				\ pri_net pri_cpe gr303fxoks_net gr303fxsks_cpe

syn match   cwChanDAHDIChans    "^\s*nsf\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDINSF
syn keyword cwChanDAHDINSF      contained
				\ accunet megacom none sdn

syn match   cwChanDAHDIChans    "^\s*pri\%(local\)dialplan\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIPriDP
syn keyword cwChanDAHDIPriDP    contained
				\ dynamic international local national private unknown

syn match   cwChanDAHDIChans    "^\s*priindication\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIPriInd
syn keyword cwChanDAHDIPriInd   contained
				\ inband outofband

syn match   cwChanDAHDIChans    "^\s*pritimer\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDIPriTimer
syn match   cwChanDAHDIPriTimer "[^,]," contained contains=cwChanDAHDIPriTimerKey,cwComma,cwError skipwhite nextgroup=cwInt
syn keyword cwChanDAHDIPriTimerKey contained
				\ N200 N201 N202 K
				\ T200 T202 T203
				\ T300 T301 T302 T303 T304 T305 T306 T307 T308 T309 T310
				\ T313 T314 T316 T317 T318 T319 T320 T321 T322

syn match   cwChanDAHDIChans    "^\s*resetinterval\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=@cwChanDAHDIRstInt
syn cluster cwChanDAHDIRstInt   contains=cwChanDAHDIRstIntKey,cwInt
syn keyword cwChanDAHDIRstIntKey contained never

syn match   cwChanDAHDIChans    "^\s*switchtype\s*=>\?" contained contains=cwChanDAHDIChansKeys,cwMapTo skipwhite nextgroup=cwChanDAHDISwitch
syn keyword cwChanDAHDISwitch   contained
				\ 4ess 5ess dms100 euroisdn national ni1 qsig


syn region  cwSection       matchgroup=cwSectionName start="^\s*\%(;\s*\)\?\[trunkgroups\]\%(([^)]*)\)\?" end="^\s*\%(;\s*\)\?\[.\{-}\]\%(([^)]*)\)\?" keepend contained contains=cwPreProc,cwChanDAHDITGrp,cwComment,cwError

syn keyword cwChanDAHDITGrpKeys contained
 \ spanmap
 \ trunkgroup

syn match   cwChanDAHDITGrp      "^\s*\%(spanmap\)\s*=>\?" contained contains=cwChanDAHDITGrpKeys,cwMapTo skipwhite nextgroup=cwChanDAHDISpanMap
syn match   cwChanDAHDITSpanMap  "\d\+\s*,\s*\d\+\%(\s*,\s*\d\+\)\?" contained contains=cwInt,cwComma,cwError

syn match   cwChanDAHDITGrp      "^\s*\%(trunkgroup\)\s*=>\?" contained contains=cwChanDAHDITGrpKeys,cwMapTo skipwhite nextgroup=cwChanDAHDITGrpValue
syn match   cwChanDAHDITGrpValue "\d\+\s*,\s*\d\+\%(\s*,\s*\d\+\)*" contained contains=cwInt,cwComma,cwError


" ============================================================================
" Highlighting Settings
" ============================================================================

hi def link cwChanDAHDIChansKeys     cwKeyword
hi def link cwChanDAHDICIDSig        cwKeyword
hi def link cwChanDAHDICIDStart      cwKeyword
hi def link cwChanDAHDIFaxDetectKeys cwKeyword
hi def link cwChanDAHDISig           cwKeyword
hi def link cwChanDAHDIPriDP         cwKeyword
hi def link cwChanDAHDIPriInd        cwKeyword
hi def link cwChanDAHDIRstIntKey     cwKeyword
hi def link cwChanDAHDISwitch        cwKeyword
hi def link cwChanDAHDINSF           cwKeyword


let b:current_syntax = "cw-chan_dahdi"
