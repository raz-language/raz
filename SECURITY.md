# Security policy

Raz is pre-1.0 software and should not yet be treated as a hardened compiler/runtime for hostile inputs without independent review.

If you discover a security-sensitive issue, avoid publishing exploit details in a public issue until the maintainer has had a reasonable opportunity to investigate. Use the repository owner's private contact or GitHub private vulnerability reporting when it is enabled for the repository.

Security-relevant areas include parser robustness, unsafe/raw-pointer validation, ownership/lifetime enforcement, native runtime boundaries, package/path handling, object emission, linking, and vendored Forge backend behavior.
