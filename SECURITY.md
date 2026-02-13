# Security Policy

## Reporting Security Vulnerabilities

The security of WallaBMC is important to us. If you discover a security vulnerability, we appreciate your help in disclosing it to us in a responsible manner.

**⚠️ Please do NOT report security vulnerabilities through public GitHub issues.**

### Preferred Method: Private Vulnerability Reporting

We encourage the use of GitHub's private vulnerability reporting feature:

1. Navigate to the [Security tab](https://github.com/tenstorrent/wallabmc/security) of the WallaBMC repository
2. Click "Report a vulnerability"
3. Fill out the vulnerability report form with as much detail as possible

This method allows us to work with you privately to understand, validate, and fix the issue before public disclosure.

### Alternative Method: Email

If you prefer not to use GitHub's private reporting feature, you can email security reports to:

**ospo@tenstorrent.com**

Please include the following information in your report:

- A description of the vulnerability
- Steps to reproduce the issue
- Potential impact of the vulnerability
- Any suggested fixes or mitigations (if available)
- Your contact information for follow-up questions

## Response Timeline

We are committed to responding to security reports in a timely manner:

- **Initial Response**: We will acknowledge receipt of your vulnerability report within 48 hours
- **Status Updates**: We will provide status updates at least every 7 days until the issue is resolved
- **Resolution Timeline**: We aim to resolve critical vulnerabilities within 30 days, though the timeline may vary based on the complexity of the issue

## Scope

This security policy applies to:

- The WallaBMC application code in this repository
- Build and deployment configurations included in this repository
- Dependencies explicitly bundled with WallaBMC

### Out of Scope

The following are generally out of scope for this security policy:

- Vulnerabilities in upstream Zephyr RTOS (report these to the Zephyr Project)
- Issues requiring physical access to secured hardware
- Social engineering attacks
- Vulnerabilities in third-party services not controlled by Tenstorrent

## Security Best Practices

When deploying WallaBMC in production environments:

1. **Change Default Credentials**: Always change default passwords and credentials
2. **Use HTTPS**: Configure and use HTTPS/TLS for web interfaces in production
3. **Replace Development Certificates**: The certificates in `src/certs/` are examples only; generate and use your own certificates for production
4. **Network Security**: Deploy appropriate network security measures (firewalls, network segmentation, etc.)
5. **Keep Updated**: Regularly update WallaBMC and its dependencies to receive security patches
6. **Follow Zephyr Security Guidelines**: Review and follow the [Zephyr Project Security Guidelines](https://docs.zephyrproject.org/latest/security/index.html)

## Supported Versions

We provide security updates for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| main    | :white_check_mark: |
| < 1.0   | :x:                |

Since this is a pre-release project, we currently only support the latest code on the `main` branch. Once we release version 1.0, we will establish a more formal support policy.

## Disclosure Policy

When we receive a security report:

1. We will confirm the vulnerability and determine its impact
2. We will develop and test a fix
3. We will prepare a security advisory
4. We will coordinate the disclosure timeline with the reporter
5. We will release the fix and publish the security advisory
6. We will credit the reporter (unless they prefer to remain anonymous)

We follow the principle of responsible disclosure and will work with reporters to ensure issues are addressed before public disclosure.

## Security Updates

Security updates and advisories will be published:

- In the [GitHub Security Advisories](https://github.com/tenstorrent/wallabmc/security/advisories) section
- In release notes for new versions
- Via GitHub notifications for repository watchers

## Questions

If you have questions about this security policy or the security of WallaBMC, please contact ospo@tenstorrent.com.

---

Thank you for helping keep WallaBMC and its users safe!
