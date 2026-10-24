#!/bin/sh
# make_certs.sh: a test certificate chain shaped like api.notion.com's, for
# tools/lab_server.py and the Speed Test's "Test Server" rows. Same key types
# and signatures, so the Mac does the same crypto:
#
#   r4       P-384, self-signed                (like GTS Root R4, which the
#                                               apps trust)
#   r4cross  the same R4, signed by an RSA     (like GTS Root R4 cross-signed by
#            root (anchor)                      GlobalSign: sent, never checked)
#   we1      P-256, signed by r4, SHA-384      (like WE1, which the apps pin)
#   leaf     P-256, signed by we1, SHA-256     (like notion.com), speedtest.lab
#
# The server sends leaf, we1 and r4cross, as Notion's does. Writes the keys
# and certificates here, and ../../src/lab_certs.h with r4 and we1 for the
# app. Test keys only: they're trusted by nothing else.
set -e
cd "$(dirname "$0")"
DAYS=7300
ext() { printf '%s\n' "$@" > ext.tmp; }

openssl genrsa -out anchor.key 2048 2>/dev/null
openssl req -x509 -new -key anchor.key -sha256 -days $DAYS -subj "/C=NL/O=Speed Test/CN=Speed Test Root CA" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out anchor.pem

openssl ecparam -name secp384r1 -genkey -noout -out r4.key
openssl req -x509 -new -key r4.key -sha384 -days $DAYS -subj "/C=NL/O=Speed Test/CN=Speed Test Root R4" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out r4.pem
openssl req -new -key r4.key -subj "/C=NL/O=Speed Test/CN=Speed Test Root R4" -out r4.csr
ext "basicConstraints=critical,CA:TRUE" "keyUsage=critical,keyCertSign,cRLSign"
openssl x509 -req -in r4.csr -CA anchor.pem -CAkey anchor.key -CAcreateserial -sha256 -days $DAYS -extfile ext.tmp -out r4cross.pem 2>/dev/null

openssl ecparam -name prime256v1 -genkey -noout -out we1.key
openssl req -new -key we1.key -subj "/C=NL/O=Speed Test/CN=Speed Test WE1" -out we1.csr
ext "basicConstraints=critical,CA:TRUE,pathlen:0" "keyUsage=critical,digitalSignature,keyCertSign,cRLSign"
openssl x509 -req -in we1.csr -CA r4.pem -CAkey r4.key -CAcreateserial -sha384 -days $DAYS -extfile ext.tmp -out we1.pem 2>/dev/null

openssl ecparam -name prime256v1 -genkey -noout -out leaf.key
openssl req -new -key leaf.key -subj "/CN=speedtest.lab" -out leaf.csr
ext "basicConstraints=critical,CA:FALSE" "keyUsage=critical,digitalSignature" "extendedKeyUsage=serverAuth" "subjectAltName=DNS:speedtest.lab"
openssl x509 -req -in leaf.csr -CA we1.pem -CAkey we1.key -CAcreateserial -sha256 -days $DAYS -extfile ext.tmp -out leaf.pem 2>/dev/null

cat leaf.pem we1.pem r4cross.pem > chain.pem
rm -f ext.tmp *.csr *.srl
openssl verify -CAfile r4.pem -untrusted we1.pem leaf.pem
openssl verify -CAfile anchor.pem -untrusted we1.pem -untrusted r4cross.pem leaf.pem

python3 -I - <<'PY'
import subprocess
def der(name):
    return subprocess.run(["openssl", "x509", "-in", name, "-outform", "DER"], capture_output=True, check=True).stdout
def arr(name, data):
    rows = ",\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 12]) for i in range(0, len(data), 12))
    return f"static const unsigned char {name}[{len(data)}] = {{\n{rows}\n}};\n"
with open("../../src/lab_certs.h", "w") as f:
    f.write("/* lab_certs.h: the test server's root (a self-signed P-384 R4, as the apps\n"
            "   trust GTS Root R4) and its WE1, made by tools/lab/make_certs.sh.\n"
            "   Test certificates only. */\n\n")
    f.write(arr("kLabAnchor_DER", der("r4.pem")) + "\n" + arr("kLabWE1_DER", der("we1.pem")))
for n in ("leaf.pem", "we1.pem", "r4cross.pem", "r4.pem", "anchor.pem"):
    print(n, len(der(n)), "bytes")
PY
