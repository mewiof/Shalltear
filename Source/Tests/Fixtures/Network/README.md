# Network fixtures

`localhost.p12` is a self-signed certificate for `localhost` and `127.0.0.1` with an RSA 2048 key, valid until 2126. PKCS #12 under the password `shalltear`, encrypted with 3DES/SHA-1 (readable by both Windows and OpenSSL 3).

Tests serve TLS on loopback using this file. Clients must pin the certificate hash (system trust store validation should fail).

```
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 36500 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
openssl pkcs12 -export -inkey key.pem -in cert.pem -out localhost.p12 -passout pass:shalltear -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1
```

Certificate SHA-256: `2be6c3fd62f6b8a5be59a15d2e6035bdec4222f8dce11bd273704beaba323cc5`.
