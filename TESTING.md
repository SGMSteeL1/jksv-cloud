# JKSV Cloud 0.3.5

This is a modified JKSV Cloud build. Use a non-critical save for the first test
and keep an independent local backup.

## Install

1. Copy `switch/JKSV-Cloud` from the release ZIP to the root of the SD card.
2. Start the Homebrew Menu normally. The login uses the Switch keyboard only to
   enter the server URL and never opens the browser applet.
3. Open **JKSV Cloud**.

## Connect

1. Open the **Extras** menu.
2. Choose **Conectar ao Nextcloud**.
3. Enter the complete HTTPS URL of the desired Nextcloud server.
4. Scan the QR code shown by JKSV Cloud with your phone.
5. Sign in and authorize JKSV Cloud on the phone. The Switch polls the server
   automatically and finishes the connection without opening a browser.

JKSV uses Nextcloud Login Flow v2. There is no shared developer credential and
the account's normal password is never given to JKSV. Nextcloud creates a
revocable app password for this console.

The credential vault is stored at `sdmc:/config/JKSV Cloud/nextcloud.vault`, sealed
with AES-256-GCM and a console-derived key. It cannot simply be moved to another
console. As with any Switch homebrew, this is not a security boundary against a
malicious homebrew running on the same console.

## Backup and restore test

- Open a game in JKSV and create a normal local backup.
- Highlight that local backup and press **ZR (Enviar)**. The uploaded entry
  appears with the `[NC]` prefix.
- To restore, highlight a `[NC]` entry and press **Y**. Restoring overwrites the
  current save, so only do this after keeping a separate known-good backup.
- Alternatively, enable JKSV's automatic upload setting for future backups.

The files are stored below the `JKSV Cloud` folder in the user's Nextcloud Files.

## Disconnect

Choose **Desconectar do Nextcloud** in Extras. JKSV tries to revoke the app
password on the server and then removes the local vault. If the server is
offline, the local vault is still removed; the token can also be revoked later
from Nextcloud's Security settings.

## Current scope

- HTTPS with a certificate trusted by the bundled Mozilla CA store is required.
- Self-signed/private CA certificates are intentionally not accepted by this
  first test build.
- This integrates cloud storage into JKSV's backup operations; it does not watch
  game saves or upload changes while a game is running.
- The updater only consults published full releases from
  `SGMSteeL1/jksv-cloud` and only accepts the exact asset `JKSV-Cloud.nro`.

## Update test

Version 0.3.5 is the first release with the JKSV Cloud updater. It should not
offer itself when `v0.3.5` is the latest release. To test a later update:

1. Bump `APP_VERSION` in `Makefile`, commit and publish a matching tag such as
   `v0.3.6`.
2. Start version 0.3.5 while connected to the internet.
3. Confirm that the prompt shows the newer version.
4. Choose **No** once and verify that no file changes.
5. Restart, choose **Yes**, wait for JKSV Cloud to close and open it again.
6. Confirm the new version in Homebrew Menu and check that
   `sdmc:/switch/JKSV-Cloud/boot.nro.bak` contains the previous executable.

The updater downloads to `boot.nro.update`, verifies the exact release size and
the `NRO0` header, then replaces the executable. A failed download is discarded
without truncating the installed NRO.

If a connection fails, reproduce it once and collect the JKSV log from the SD
card. Do not share `nextcloud.vault`; while sealed, it is still account material.
