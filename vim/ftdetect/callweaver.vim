" Function used for patterns that end in a star: don't set the filetype if the
" file name matches ft_ignore_pat.
func! s:StarSetf(ft)
  if expand("<amatch>") !~ g:ft_ignore_pat
      exe 'setf ' . a:ft
  endif
endfunc

au BufNewFile,BufRead *callweaver/callweaver.conf* call s:StarSetf('cw-callweaver')
au BufNewFile,BufRead *callweaver/cdr_custom.conf* call s:StarSetf('cw-cdr-custom')
au BufNewFile,BufRead *callweaver/cdr_pgsql_custom.conf* call s:StarSetf('cw-cdr-pgsql-custom')
au BufNewFile,BufRead *callweaver/chan_dahdi.conf* call s:StarSetf('cw-chan-dahdi')
au BufNewFile,BufRead *callweaver/extensions.conf* call s:StarSetf('cw-extensions')
au BufNewFile,BufRead *callweaver/sip.conf* call s:StarSetf('cw-sip')
au BufNewFile,BufRead *callweaver/voicemail.conf* call s:StarSetf('cw-voicemail')
au BufNewFile,BufRead *callweaver/*.conf* call s:StarSetf('cw-generic')
