# Security Policy

## Supported Versions

The following versions of our project are currently receiving security updates:

| Version | Supported          | End of Support |
| ------- | ------------------ | -------------- |
| 5.1.x   | :white_check_mark: | Active         |
| 5.0.x   | :x:                | 2024-12-31     |
| 4.0.x   | :white_check_mark: | 2025-06-30     |
| < 4.0   | :x:                | 2023-12-31     |

**Note**: We strongly recommend using the latest stable version (5.1.x) for the best security and feature support.

## Reporting a Vulnerability

We take the security of our project seriously. If you discover a security vulnerability, please report it responsibly by following these guidelines:

### How to Report

1. **DO NOT** create a public GitHub issue for security vulnerabilities
2. Email your findings to **security@example.com**
3. Include the following information:
   - Type of vulnerability
   - Full paths of source file(s) related to the vulnerability
   - Location of the affected source code (tag/branch/commit or direct URL)
   - Step-by-step instructions to reproduce the issue
   - Proof-of-concept or exploit code (if possible)
   - Impact of the issue, including how an attacker might exploit it

### Response Timeline

- **Initial Response**: Within 48 hours
- **Status Update**: Within 5 business days
- **Resolution Timeline**: Depends on severity
  - Critical: 7-14 days
  - High: 14-30 days
  - Medium: 30-60 days
  - Low: 60-90 days

### What to Expect

After reporting a vulnerability:

1. You'll receive an acknowledgment of your report
2. We'll investigate and validate the issue
3. We'll work on a fix and coordinate a release
4. We'll publicly acknowledge your contribution (unless you prefer to remain anonymous)

### Security Advisories

Security advisories will be published through:
- GitHub Security Advisories
- Our mailing list
- Project blog/website

## Security Best Practices

When contributing to this project, please follow these security best practices:

1. **Dependencies**: Keep all dependencies up to date
2. **Secrets**: Never commit secrets, credentials, or API keys
3. **Input Validation**: Always validate and sanitize user input
4. **Authentication**: Use strong authentication mechanisms
5. **Encryption**: Use encryption for sensitive data in transit and at rest

## Disclosure Policy

- We follow a coordinated disclosure policy
- Security issues will be disclosed publicly after patches are available
- We aim to disclose vulnerabilities within 90 days of the initial report
- Credit will be given to security researchers who report valid issues

## Comments

If you have suggestions for improving this policy, please submit a pull request or open an issue for discussion.
