# ATV names: broadcast identity versus teletext text

The original receiver's automatic channel naming uses a built-in network-ID
table. It does not copy an arbitrary packet8/30 status-display string into the
TV channel database. In the original image:

* `80453D50` obtains VPS / packet8/30 format1 / format2 identifiers, then calls
  the original lookup `805B248C` and name copier `805B27FC`.
* The name table is at `806B2BD0`, with 549 records of 28 bytes. Format1 count
  and ID-list pointer are at record offsets12 and16. Names have five bytes.
* `80402254..804022A8` tries this lookup and falls back through `80402118` to
  a frequency-channel label such as C-03. The later retry at `804022B0` follows
  the same path. The current name buffer is `806BDEE0`.
* `804530F8` reverses each received NI byte and places the first in bits15:8.
  Station input now accepts `MHz|teletext-name|text|network-id`; for example
  `55.25|TVP1|TEST|0x4801` transmits the original TVP1 identity in packet8/30.

`tools/atv_identity_extract.py` validates the original firmware and exports the
supported format1 identities to `site/live/station-identities.json`. The site
offers these as **TV channel identity**, separately from the custom teletext
station name. Unknown IDs remain unknown to the receiver. The custom text
field does not rewrite the TV's stored names. Arbitrary TV-name editing is not
verified; the observed Programme Edit page offers Delete/Move/Skip/Favourite.

The related [MStar tuning source](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/api/msAPI_Tuning.c)
and [teletext API source](https://github.com/Toysoft/mstar/blob/master/Chakra3_017a0d6_20170731/core/middleware/teletext/msAPI_TTX.c)
helped identify the routines; the addresses and table above were checked in
the unmodified Manta image. Related source is supporting evidence, not a
replacement implementation.

## Validation so far

`station_name_probe` runs original lookup/copier instructions against the
hash-verified runtime with isolated zero-initialized scratch RAM. Input4801
returns table index512 and TVP1. This is explicitly an isolated result.

A connected native run uses actual remote keys: TV, MENU, DOWN twice, OK,
DOWN three times, RIGHT (manual search). With the station at55.25MHz, the
firmware calls `805B248C` with a1=4801, selects index200h, and calls `805B27FC`
to write `TVP1 ` into its own name buffer. At3B dispatches RAM806BDEE0 is
`54 56 50 31 20 00 00 00`. No host channel-name RAM writes or firmware patches
were introduced. A subsequent real RED Save, MENU, DOWN and OK sequence opens
Programme Edit, which shows the selected **1 TVP1 / ATV** entry (distinct from
the snapshot's pre-existing digital TVP1 entry). This verifies the stored ATV
name in the firmware's own UI. Power-cycle persistence remains unverified.
