# Claude Code Notes

## Important Guidelines

### Git Commits
- Do NOT use Claude as co-author or committer in git commits
- Remove the "Co-Authored-By: Claude" lines from commit messages
- Keep commit messages professional and focused on the technical changes

### Version Management
- Always increment version numbers when making significant changes
- Use +0.01 increment pattern for bug fixes and improvements
- Update version in appropriate files when committing changes

## Project Context

This is the DTNEx (DTN Exchange) project for ION-DTN network information exchange.

## Recent Work
- Fixed 64-bit IPN number handling in CBOR integer decoding (v2.42)
- Increased metadata length limits for better graph visualization (v2.42)
- Fixed critical network loop prevention by preserving nonces during forwarding (v2.43)

## Important Technical Findings

### Network Loop Issue (Fixed in v2.43)
- **Problem**: Forwarding was generating new nonces, breaking duplicate detection
- **Root Cause**: `generateNonce(newNonce)` in forwarding functions created fresh nonces
- **Impact**: Messages could loop through network as each hop had different nonce
- **Solution**: Preserve original nonce during forwarding using `originalNonce` parameter
- **Key Insight**: Nonce should remain unchanged for proper duplicate detection across hops