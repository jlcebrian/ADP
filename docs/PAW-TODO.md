# PAW remaining work

1. **B03 parser state machine**: clause/verb inheritance, exact word
   alphabet, adjective slot closure, pronoun update rules, quoted PARSE
   cursor and failure side effects, `$01`/`$FF` wildcards without the
   load-time rewrite, flag-58 process matching context.
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
