# libmocha Source Pin

Upstream build inputs from wiiu-env/libmocha commit
`75f31e0a3e2415a3619f732eab95e65f73d8d99b` (2022-09-03).
https://github.com/wiiu-env/libmocha/tree/75f31e0a3e2415a3619f732eab95e65f73d8d99b

This pin builds with Assistant's existing 2022-09-07 SDK. The current upstream
header requires newer SDK launch-argument types; the SDK has not been upgraded.
Only library initialization, dedicated FSA-client permission, and cleanup APIs
are called by Assistant. No memory, OTP, key, logging, or launch APIs are exposed.

License: LGPL-3.0, retained in LICENSE and Assistant's public licenses directory.
Upstream source/include/Makefile/LICENSE/README.md are retained. Builds copy these
inputs into the disposable build directory; builds do not modify vendored inputs.

Local modification, 2026-10-16: backport the upstream `iosuhaxHandle = -1`
assignment after `IOS_Close` in `Mocha_DeInitLibrary`. Without it, a subsequent
initialization reuses the stale handle value and deinitialization can close a
new FSA client that received the recycled handle. No other library code changed.
Reference: https://github.com/wiiu-env/libmocha/blob/main/source/utils.cpp
The host lifecycle test compiles these actual vendored functions and reproduces
the original second-check failure before verifying repeated handle reuse.

Distributions linking this library must include the corresponding library source
and a usable Assistant relinking route. The public release remains gated on the
updated source/relink package and notice verification.
