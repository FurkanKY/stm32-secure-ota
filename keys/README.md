# Keys

`dev_ed25519.pem` is a **development key that is public on purpose**. It lets anyone
clone the repository, build, sign and run the simulation tests without setup, and CI
uses it to sign test images.

Anything signed with it is untrusted by definition. A real product would:

- generate its signing key inside an HSM or cloud KMS and never export it,
- sign release images from a protected CI job, not from a laptop,
- build the bootloader with only the production public key (`otatool pubkey --c`),
- reserve a second `key_id` slot so a compromised key can be rotated out.
