#!/bin/bash
# Local TLS fixture for realistic HTTPS load tests (DIAG ONLY; keys are throwaway).
# Output: .tls-fixture/{ca.pem,server.pem,server.key}. Chain: RSA-2048 test CA -> ECDSA P-256 leaf, CN=10.0.2.100.
set -e
cd "$(dirname "$0")/../.."
mkdir -p .tls-fixture && cd .tls-fixture
[ -f server.pem ] && exit 0
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 3650 -subj "/CN=Bursztyn Test CA" -sha256 2>/dev/null
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -key server.key -subj "/CN=10.0.2.100" -out server.csr
printf "basicConstraints=CA:FALSE\nsubjectAltName=DNS:10.0.2.100,IP:10.0.2.100\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n" > ext.cnf
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out server.pem -days 365 -sha256 -extfile ext.cnf 2>/dev/null
