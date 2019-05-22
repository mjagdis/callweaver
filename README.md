Callweaver is/was an old fork of Asterisk 1.2 from the days when
the newer Asterisk 1.4 was still pursuing stability. A number
of fixes and improvements were made by a small community of
coders.

Today callweaver is unused even by me. I've put it here simply
because, to the best of my knowledge, it doesn't exist anywhere
else and there is some potentially interesting code to be
found in it.

* It contains example use of Steve Underwood's SpanDSP library
  to implement native FAX send and receive over both RTP
  (network) and TDM (fixed line telephony) circuits.

* I undertook a major rewrite of the dialplan handling to remove
  many issues.

* I added auto-backoff and blacklisting to the SIP channel to
  mitigate the impact of the continuous script-kiddie probing
  that every exposed SIP server experiences.

* I added IPv6 support to the SIP channel - along with many
  other changes to make it (slightly?) more conformant to
  the RFCs.

* I extended printf with format specifiers for network addresses
  using both the glibc extension hooks and a transparent workaround
  for other environments.

* I added a C implementation of dynamic strings in
  include/callweaver/dynstr.h and corelib/dynstr.c
  (including support for sprinf'ing into them).

* I (ab)used the preprocessor to implement a variant of printf that
  allows complex formats to be expressed in a user friendly format
  but which collects the components together into a single format
  string at compile time. This makes much of the message handling
  **much** clearer.
  e.g.
  ```
      cw_dynstr_tprintf(ds_p, 3,
          cw_fmtval("-- General --\n"),
          cw_fmtval("Name: %s\n",           chan->name),
          cw_fmtval("Type: %s\n",           chan->type));
  ```
  which compiles as
  ```
  cw_dynstr_printf("-- General --\nName: %s\nType: %s\n", chan->name, chan->type);
  ```

* I added a C implementation of dynamic arrays in
  include/callweaver/dynarray.h that is templated via preprocessor
  defines so that you can use dynamic arrays of whatever type
  you choose.

* I added a C implementation of reference counted objects (entities
  rather than Objects in the modern OOP sense - it's C not C++!)
  in include/callweaver/object.h that uses atomic ops. This is
  heavily used throughout callweaver to avoid locking issues
  arising from the old code design.

I have been known to dust it off occasionally and use it for
testing various ideas but since I have neither used SIP nor
owned a PC that would take my old TDM cards for many years
you should not expect callweaver to be stable or even usable
anymore!
