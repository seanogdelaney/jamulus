# F10 routing and source metadata

Selecting **Poly-in/Stereo-out** in the existing audio dropdown opens the previous Poly-in four-column popup: stock instrument icon, fader tag suffix, Ch1 and optional Ch2. The implementation adapts that existing popup and reuses the accepted SourceProducer routes, stock quality profiles, sound callback, settings storage and ordinary receiver mixer. No extra client session, socket, clock, mixer or receiver message is introduced.

Configure while disconnected. Blank Ch2 selects mono; a selected Ch2 selects an independent stereo source. Inputs cannot be reused across rows. At least two rows are required, with unique nonempty tags of at most 48 UTF-8 bytes. The existing native F09 cap is 20 physical lanes and 20 logical sources; a stereo row consumes two physical lanes. No synthetic/mixed input selectors are offered. Unavailable saved inputs remain visibly unavailable and are rejected. Sound-device, buffer and quality controls retain the stock lifecycle. The ordinary L/R input selectors are disabled while the explicit table owns input selection; output selectors remain stock.

The physical source zero must match the existing physical transport profile. A mono first row therefore uses stock mono-in/stereo-out transport and the existing stereo producer route with the same input on both sides. Other mono rows use the existing mono route. Every source uses the matching existing stock quality profile. There is still one stock stereo return per physical connection.

Tags are suffixes on the stock alias; producer descriptors are bounded to 64 bytes. The unchanged server source-identity mechanism appends its existing source number and truncates the ordinary mixer name to its stock 16-byte limit. Long combined aliases/suffixes can therefore be shortened on the receiver.

## Producer configuration version 2

This is the user-authorized extension of the existing reliable producer-only CONFIG body. Version 1 remains accepted and remains the emitted form for count-only producers. Version 2 adds a little-endian uint16 instrument ID before each descriptor name length; all other descriptor fields and epoch semantics are unchanged. The parser bounds IDs to 0..255, then the server validates against the stock instrument catalogue before admission. Missing version-1 metadata inherits the physical owner's stock icon. Responses remain version 1. The server publishes accepted icons through its existing ordinary connected-client list. Stock receivers require no changes.

A server that understands only CONFIG version 1 can reject the new opt-in; the existing refused/timeout opt-in fallback governs ordinary audio. This extension does not claim icon support on an older Poly-in server.

Settings retain the table independently of whether Poly-in is selected. Explicit CLI --polyin/--polyin-sources overrides the saved mode; ordinary startup preserves the GUI choice.

## Evidence boundaries

run_capture_routes.py exercises actual bounded route validation, version-1/2 metadata, malformed/truncated messages, stock profile mapping and bit-exact fragmented mixed mono/stereo RAW64/128. run_native_capture.py extracts the actual CoreAudio/ASIO loops and checks explicit two-lane physical capture separately from stock selected-pair capture. These are component tests. Native CoreAudio and unchanged receiver results belong in the external qualification report; Windows/ASIO runtime is not inferred from these tests.
