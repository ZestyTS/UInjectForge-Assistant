# Remembered Pairing

Development change, not included in the September 21 public release. Requires
the matching UIF 1.0.6 client and updated Assistant. PIN pairing stays
compatible with existing UIF versions.

## Behavior

- Enter the console PIN in UIF and select **Remember device** once.
- UIF generates a separate random 128-bit credential using its platform's
  cryptographic random generator. It stores that credential in device secure
  storage, scoped to the Assistant scheme, host and port, and remembers the
  endpoint in ordinary settings. UIF does not store PINs in desktop settings.
- On its first successful boot, Assistant saves a generated six-digit string
  as `pairingCode` in `sd:/wiiu/apps/uinjectforge-assistant/config.json` and reuses
  it on later launches. Settings saves preserve it, including leading zeros.
  Failed pairing attempts keep the PIN and retain capped retry backoff.
  Malformed/unwritable configuration is left intact; pairing startup fails
  closed rather than silently switching to an unsaved, temporary PIN.
  Treat the SD configuration and its `.bak` file as private credentials.
- Assistant stores only SHA-256 credential hashes in
  `sd:/uinjectforge/assistant/paired-devices.bin`, with at most eight devices.
  A checksum detects accidental corruption; it is not a signature or protection
  against someone who can modify the SD card. Missing/corrupt files fail closed.
- Successful remembering invalidates the temporary session credential it used.
  The saved PIN remains unchanged across launches; remembered devices continue
  to use their separate credentials without typing it.
- **Clear Pairing** in UIF deletes the local saved credential and attempts
  console-side revocation. If offline, UIF explains that revocation on the
  console is still needed.
- On Assistant, **Diagnostics > Devices > Forget paired devices** requires confirmation,
  clears all saved credentials and the current temporary session, and changes
  the PIN and saves the replacement to SD. Transfers/installations must finish
  first. If PIN publication fails, devices are still revoked and the previous
  PIN is retained with a visible error; no unsaved replacement is accepted.
- Pairing changes do not enable automatic installation or alter install policy.
- Transport remains unencrypted HTTP on a trusted LAN. Remembering a device
  does not authenticate a network endpoint or make public/shared networks safe.
  Re-select a changed address and pair again. Never forward this port publicly.

## Protocol

Status advertises `remembered-pairing-v1`. Existing `POST /api/v1/pair` still
accepts the saved PIN and returns a temporary session bearer token.

- `POST /api/v1/pair/remember`: authenticate with the session or remembered bearer;
  body is exactly 32 ASCII hexadecimal characters representing the client-created
  random credential. Persist its hash before acknowledging success. Repeating
  the same remembered credential is idempotent.
- `GET /api/v1/pair`: authenticated credential check, no PIN or token disclosure.
- `DELETE /api/v1/pair`: revoke the authenticated remembered/current credential.

Management responses use `success` and `message`. Invalid auth returns 401;
busy transfers or a full trust store return 409; SD publication failure returns
500 and does not change in-memory trust. File updates use temporary and backup
paths for FAT rename semantics; the paired-device store never restores a backup
automatically (configuration recovery is separate).

## Acceptance

Local native persistence/auth/UI regression tests and Wii U cross-compilation
pass. Still required before release: real-console restart/reconnect, two-device
pairing, single/all-device revocation, read-only/removed SD behavior, and native
mobile secure-store checks. No hardware acceptance is implied by host tests.
