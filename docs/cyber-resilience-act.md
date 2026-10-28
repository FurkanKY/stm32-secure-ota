# Secure updates and the EU Cyber Resilience Act

The EU Cyber Resilience Act (Regulation (EU) 2024/2847, "CRA") sets cybersecurity
requirements for products with digital elements sold in the EU. It entered into force on
10 December 2024; the obligation to report actively exploited vulnerabilities applies
from 11 September 2026, and the main requirements from 11 December 2027.

A large share of those requirements comes down to one capability: **fixing a product in
the field, safely, for its whole support period.** That is what this bootloader is about,
so this note maps its features to the CRA's essential requirements (Annex I) and is
explicit about what it does not cover.

> A bootloader is a component, not a product. Conformity is a property of the whole
> product and of the manufacturer's processes. This is an engineering note, not legal
> advice.

## Annex I, Part I: properties of the product

| Requirement (paraphrased) | What this project provides | Gaps on the way to a product |
|---|---|---|
| **(2)(c)** Vulnerabilities can be addressed through security updates | Field updates over UART and CAN; images for either slot; recovery mode when nothing boots | Update delivery from a backend, user notification and automatic-update policy are application concerns |
| **(2)(f)** Integrity of data, programs and configuration against unauthorised modification, and reporting of corruption | Ed25519-signed images, verified at install and on every boot; CRC-protected boot state; per-slot error codes on the console and in HELLO | Debug port and option-byte protection must be configured per product |
| **(2)(h)** Availability of essential functions, resilience | A/B slots, the running image is never overwritten, trial boot with watchdog rollback, power-loss-safe journal (every flash operation of an update cycle is cut in a test), timeout back to normal boot | Application-level watchdog strategy |
| **(2)(k)** Limit the impact of incidents with exploitation mitigation | Anti-rollback counter: once a fix ships, the vulnerable release cannot be reinstalled, even though it is genuinely signed | Hardware-backed monotonic counter (here it lives in the journal, protected by the bootloader's own code) |
| **(2)(d)** Protection from unauthorised access | Nothing unsigned can be installed or booted | The update channel itself is not authenticated |
| **(2)(e)** Confidentiality of stored and transmitted data | Not provided: images are signed, not encrypted | Encrypted images with per-device keys |
| **(2)(j)** Limit attack surfaces | Bootloader has two small, documented protocol surfaces; CAN is only listened to in recovery mode | Read-out protection, disabling unused interfaces |
| **(2)(l)** Security-related information by recording internal activity | Boot decisions, verification results and reset cause are printed on every boot | No persistent security log |

## Annex I, Part II: vulnerability handling by the manufacturer

| Requirement (paraphrased) | Relevance here |
|---|---|
| **(1)** Identify components, including a software bill of materials | Third-party code is vendored with versions and licences in `third_party/` (Monocypher 4.0.2, Unity 2.6.0 for tests). An SBOM file (SPDX or CycloneDX) generated in CI would be the next step |
| **(2)** Remediate without delay; separate security updates from feature updates where feasible | The header separates `version` (features) from `security_counter` (security fixes): a security fix raises the counter, a feature release does not, so feature downgrades stay possible while vulnerable builds are locked out |
| **(3)** Regular testing of security | Host unit tests with sanitizers, a power-cut sweep and end-to-end simulation tests run in CI on every push |
| **(7)** Mechanisms to distribute updates securely | Signed images make the distribution channel untrusted by design: a compromised server or link cannot get unsigned code onto the device |
| **(8)** Updates disseminated without delay | Outside the device: a release and delivery process |

## What a product team would still have to do

- Protect the signing key in an HSM or cloud KMS, sign from a protected release pipeline,
  and plan key rotation (`key_id` is reserved in the header).
- Lock the device: RDP level and write protection of the bootloader and journal sectors.
- Define the support period and the update delivery path (app, gateway, backend).
- Run coordinated vulnerability disclosure and the 24-hour/72-hour reporting to ENISA and
  national CSIRTs required for actively exploited vulnerabilities.
