#!/usr/bin/env bash
# aws_setup.sh - fully-automated AWS IoT Core resource provisioning for the bl602 telemetry client.
#
# Creates (idempotently) in the caller's AWS account, via aws-cli:
#   - an IoT policy  (Connect as any client, Publish/Subscribe/Receive on bl602/*)
#   - a keys+certificate pair (device cert + private key), set ACTIVE
#   - a Thing, with the certificate attached as its principal
# Then fetches the Amazon Root CA 1 and converts all three creds (CA / device cert / private key)
# to DER + base64 - the exact form the device's `aws ca|cert|key <b64>` console command expects
# (base64 of DER, decoded on-device to a LittleFS file / in-RAM key).
#
# Outputs land in $OUT (default project/bouffalo/build_bl602/aws, which is gitignored - the private
# key MUST NOT be committed). Re-running reuses the policy/thing but mints a NEW certificate.
#
# Usage:  project/bouffalo/tools/aws_setup.sh [region]
# Then:   the printed `endpoint` + the *.b64 files feed project/bouffalo/aws_provision.ps1.
set -euo pipefail

REGION="${1:-$(aws configure get region 2>/dev/null || echo ap-southeast-1)}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${AWS_OUT:-$HERE/../build_bl602/aws}"
POLICY="bl602-telemetry-policy"
THING="bl602-telemetry-demo"
CA_URL="https://www.amazontrust.com/repository/AmazonRootCA1.pem"

mkdir -p "$OUT"
ACCOUNT="$(aws sts get-caller-identity --query Account --output text)"
echo "== AWS IoT setup :: account $ACCOUNT / region $REGION =="

# 1) ATS data endpoint (region-specific; this is what the device connects to on :8883).
EP="$(aws iot describe-endpoint --endpoint-type iot:Data-ATS --region "$REGION" \
        --query endpointAddress --output text)"
echo "$EP" > "$OUT/endpoint.txt"
echo "endpoint: $EP"

# 2) IoT policy - scope the demo to bl602/* topics; any client id (device uses its WiFi MAC).
cat > "$OUT/policy.json" <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    { "Effect": "Allow", "Action": "iot:Connect",   "Resource": "arn:aws:iot:$REGION:$ACCOUNT:client/*" },
    { "Effect": "Allow", "Action": ["iot:Publish","iot:Receive"], "Resource": "arn:aws:iot:$REGION:$ACCOUNT:topic/bl602/*" },
    { "Effect": "Allow", "Action": "iot:Subscribe", "Resource": "arn:aws:iot:$REGION:$ACCOUNT:topicfilter/bl602/*" }
  ]
}
JSON
if aws iot create-policy --policy-name "$POLICY" --policy-document "file://$OUT/policy.json" \
        --region "$REGION" >/dev/null 2>&1; then
    echo "policy: created $POLICY"
else
    echo "policy: $POLICY already exists (reusing)"
fi

# 3) Device certificate + private key, set ACTIVE.
CERT_ARN="$(aws iot create-keys-and-certificate --set-as-active \
        --certificate-pem-outfile "$OUT/cert.pem" \
        --private-key-outfile "$OUT/key.pem" \
        --public-key-outfile "$OUT/pub.pem" \
        --region "$REGION" --query certificateArn --output text)"
echo "$CERT_ARN" > "$OUT/cert_arn.txt"
echo "cert: $CERT_ARN"

# 4) Bind the policy to the cert, and the cert to a Thing.
aws iot attach-policy --policy-name "$POLICY" --target "$CERT_ARN" --region "$REGION"
aws iot create-thing --thing-name "$THING" --region "$REGION" >/dev/null 2>&1 || true
aws iot attach-thing-principal --thing-name "$THING" --principal "$CERT_ARN" --region "$REGION"
echo "thing: $THING <- cert attached"

# 5) Amazon Root CA 1 (server trust anchor).
curl -sS -o "$OUT/ca.pem" "$CA_URL"

# 6) PEM -> DER -> base64 (bare body, one line) for `aws ca|cert|key <b64>`.
openssl x509 -in "$OUT/ca.pem"   -outform der -out "$OUT/ca.der"
openssl x509 -in "$OUT/cert.pem" -outform der -out "$OUT/cert.der"
# create-keys-and-certificate returns an RSA-2048 key; `openssl pkey` handles RSA or EC transparently.
openssl pkey  -in "$OUT/key.pem" -outform der -out "$OUT/key.der"
for f in ca cert key; do base64 -w0 "$OUT/$f.der" > "$OUT/$f.b64"; done

echo "== done =="
printf '  %-10s %s\n' endpoint "$EP"
printf '  %-10s %s bytes -> %s b64 chars\n' ca.der   "$(wc -c < "$OUT/ca.der")"   "$(wc -c < "$OUT/ca.b64")"
printf '  %-10s %s bytes -> %s b64 chars\n' cert.der "$(wc -c < "$OUT/cert.der")" "$(wc -c < "$OUT/cert.b64")"
printf '  %-10s %s bytes -> %s b64 chars\n' key.der  "$(wc -c < "$OUT/key.der")"  "$(wc -c < "$OUT/key.b64")"
echo "  creds dir: $OUT   (gitignored - key.pem/key.der are SECRET)"
