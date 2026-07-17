# PAW remaining work

1. **B03 parser state machine**: clause/verb inheritance, exact word
   alphabet, adjective slot closure, pronoun update rules, quoted PARSE
   cursor and failure side effects, flag-58 process matching context.
   (`$01`/`$FF` wildcards are now matched at runtime; the load-time
   rewrite is gone so dumps stay byte-faithful.)
2. **128K conformance beyond the SDB corpus**: SDB preserves and loads all
   PAWS pages, and text/location/graphics lookups resolve their logical page.
   Add fixtures for any later games that exercise other banked record types.
3. **Condact conformance sweep**: table-driven checks of all 108 opcodes
   against `PAWS/docs/condact-runtime.md` (known deviations: flag-38
   assignment validation, CHANCE/RANDOM distribution, PAUSE 0, RESET,
   container weight, PUTIN/TAKEOUT validation, object reference flag
   caching, DROPALL worn objects).
4. **BEEP/BORDER output**, sharing the planned AY work.
5. **GOSUB depth**: B03 allows ten nested calls; ADP rejects at ten shared
   with the top level (effective nine).
6. **PC PAW `.PDB`** as a separate loader/dialect (32-byte header; shares
   the version number but not the runtime), or explicitly document it as
   unsupported.
7. **ROM-pixel fidelity** for DRAW ties and the two-pass span fill, only
   after the above; classify as a named fidelity level with corpus
   screenshots.

## Done: adpc PAWS compilation (phase 1, July 2026)

`adpc --version paws` compiles a PAW-dialect `.SCE` into a 48K `.SDB`:

    adpc --version paws --graphics DONOR.SDB [--no-compression] game.sce

- `--graphics` names a donor SDB providing what the source cannot: UDGs,
  shade patterns, fonts, the compression dictionary and the part B
  drawstring area (donor location count must match). Without a donor the
  build gets the ROM font, no dictionary and empty drawstrings.
- Text is compressed with the donor dictionary using PAW's own algorithm
  (tokens claimed in table order, object names never compressed).
- `ddb dump --raw-tokens` keeps dictionary references as `{n}` codes;
  recompiling that dump with `--no-compression` reproduces Supervivencia
  and Inicio byte for byte. A normal dump plus recompression differs only
  where the originals carry edit-history compression artifacts, and the
  scripted Firfurcio playthrough is transcript-identical either way.

PC PAWS sources compile directly: the bundled START/TEWK/TICKET .SCE
files build into playable Spectrum SDBs, including PAWCOMP's `/LNK`
file-chaining directive. `BELL` is accepted as an alias that assembles
to Spectrum `BEEP 0 0`; non-ASCII source text needs `--tr` mappings to
font positions.

`_` noun substitution follows the real interpreters (A17C vs B03
disassembly): the English printer strips the object text's first word
(the article) and following spaces, the Spanish one lower-cases the
first capital of the first word (`OR $20`, which also maps `\`/Ñ to
`|`/ñ), and both stop at a full stop. The database language is detected
at load time from system message 30, the yes-answer letter ('Y' English,
'S' Spanish).

Not yet covered: 128K layout/paging, PDB output, and a graphics source
format (donor-only for now).
