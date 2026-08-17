# Security policy

Raz 1.0 is a stable release line. Security-relevant fixes are delivered in 1.x releases; run a current 1.x toolchain to receive them.

The compiler and runtime have not undergone an independent third-party security audit. Deployments that compile or execute untrusted input should apply their own review in addition to the guarantees the language provides.

If you discover a security-sensitive issue, avoid publishing exploit details in a public issue until the maintainer has had a reasonable opportunity to investigate. Use the repository owner's private contact or GitHub private vulnerability reporting when it is enabled for the repository.

Security-relevant areas include parser robustness, unsafe/raw-pointer validation, ownership/lifetime enforcement, native runtime boundaries, package/path handling, object emission, linking, and vendored Forge backend behavior.
