# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in potluck, please report it responsibly.

### How to Report

1. **Do NOT open a public issue** for security vulnerabilities
2. Send an email to the maintainers with:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Any suggested fixes (optional)

### What to Expect

- Acknowledgment within 48 hours
- Regular updates on the progress
- Credit in the security advisory (if desired)

### Scope

This security policy covers:
- The potluck sysmodule
- The relay server in `server/` and the image built from it
- Network protocol implementation
- Configuration handling

### Out of Scope

- Vulnerabilities in Nintendo Switch firmware
- Issues in devkitPro toolchain
- Vulnerabilities in Atmosphere

## Security Considerations

### Network Security

potluck communicates over the network. Users should:
- Only connect to trusted potluck servers
- Use the sysmodule on private networks when possible
- Keep the sysmodule updated

### Console Security

This sysmodule requires:
- Custom firmware (Atmosphere)
- Proper installation in `/atmosphere/contents/`

The sysmodule does not:
- Modify system files
- Store sensitive data
- Require elevated privileges beyond LDN access
