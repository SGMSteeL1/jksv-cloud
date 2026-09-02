# JKSV Cloud notices

JKSV Cloud is an independent modified fork of JKSV. The original JKSV project
is developed by J-D-K and its contributors:

- Original source: https://github.com/J-D-K/JKSV
- Original contributors: https://github.com/J-D-K/JKSV/graphs/contributors
- License: GNU General Public License v3.0; see `LICENSE`.

The JKSV Cloud modifications are maintained by Steel/SGMSteeL1 at
https://github.com/SGMSteeL1/jksv-cloud. This fork is not an official JKSV
release and is not affiliated with or endorsed by the original project.
Fork-specific support requests must be directed to the fork rather than to the
original JKSV maintainers.

The console-bound sealed-storage implementation in
`source/security/DeviceSeal.cpp` is derived from Checkpoint's
`device_seal` design and implementation:

- Checkpoint copyright (C) 2017-2026 Bernardo Giordano, FlagBrew.
- Source: https://github.com/FlagBrew/Checkpoint
- License: GNU General Public License v3.0 or later, with the attribution and
  modified-origin terms stated in Checkpoint's source file preserved here.

This modified implementation uses its own JKSV-specific magic values and key
domain-separation labels and does not claim to be the original Checkpoint code.

JKSV Cloud checks public releases only from
https://github.com/SGMSteeL1/jksv-cloud. No GitHub account token or private
credential is embedded in the executable.

The CA certificate bundle in `romfs/certs/cacert.pem` was extracted from
Mozilla's root certificate program and obtained from curl's CA Extract service:
https://curl.se/docs/caextract.html. Its own notice is embedded in the file.
