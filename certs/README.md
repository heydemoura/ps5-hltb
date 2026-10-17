The payload trusts the public GlobalSign Root R46 for HowLongToBeat HTTPS.
Certificate verification remains enabled, including hostname and validity checks.

Downloaded from https://secure.globalsign.com/cacert/rootr46.crt and converted
from DER to PEM with OpenSSL. Official listing:
https://support.globalsign.com/ca-certificates/root-certificates/globalsign-root-certificates

DER SHA-256 fingerprint:
`4FA3126D8D3A11D1C4855A4F807CBAD6CF919D3A5A88B03BEA2C6372D93C40C9`

`src/root_ca.h` embeds the PEM certificate so the payload needs no extra file.
