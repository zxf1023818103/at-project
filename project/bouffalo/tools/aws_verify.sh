#!/usr/bin/env bash
# aws_verify.sh - cloud-side proof that the bl602 telemetry actually lands in AWS IoT Core.
# Creates (idempotently) an IoT Topic Rule that routes bl602/+/telemetry -> a CloudWatch Logs group,
# waits, then prints the messages CloudWatch received. Fully aws-cli; no local MQTT client needed.
set -euo pipefail
REGION="${1:-$(aws configure get region 2>/dev/null || echo ap-southeast-1)}"
ACCOUNT="$(aws sts get-caller-identity --query Account --output text)"
ROLE="bl602-telemetry-cwl-role"
RULE="bl602_telemetry_to_cwl"
LOG_GROUP="/aws/iot/bl602-telemetry"
TMP="${CLAUDE_JOB_DIR:-/tmp}/tmp"; mkdir -p "$TMP"

echo "== verify setup :: account $ACCOUNT / region $REGION =="

# 1) CloudWatch Logs group.
aws logs create-log-group --log-group-name "$LOG_GROUP" --region "$REGION" 2>/dev/null || echo "log-group exists"
aws logs put-retention-policy --log-group-name "$LOG_GROUP" --retention-in-days 1 --region "$REGION" 2>/dev/null || true

# 2) IAM role IoT assumes to write to CloudWatch Logs.
cat > "$TMP/trust.json" <<JSON
{ "Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"iot.amazonaws.com"},"Action":"sts:AssumeRole"}] }
JSON
aws iam create-role --role-name "$ROLE" --assume-role-policy-document "file://$TMP/trust.json" 2>/dev/null \
    && echo "role created" || echo "role exists"
cat > "$TMP/perm.json" <<JSON
{ "Version":"2012-10-17","Statement":[{"Effect":"Allow",
  "Action":["logs:CreateLogStream","logs:PutLogEvents","logs:DescribeLogStreams"],
  "Resource":"arn:aws:logs:$REGION:$ACCOUNT:log-group:$LOG_GROUP:*"}] }
JSON
aws iam put-role-policy --role-name "$ROLE" --policy-name cwl-put --policy-document "file://$TMP/perm.json"
ROLE_ARN="arn:aws:iam::$ACCOUNT:role/$ROLE"
echo "role: $ROLE_ARN"
sleep 8   # let the new role propagate before IoT validates it

# 3) IoT topic rule: bl602/+/telemetry -> CloudWatch Logs.
cat > "$TMP/rule.json" <<JSON
{ "sql":"SELECT * FROM 'bl602/+/telemetry'","awsIotSqlVersion":"2016-03-23","ruleDisabled":false,
  "actions":[{"cloudwatchLogs":{"roleArn":"$ROLE_ARN","logGroupName":"$LOG_GROUP"}}] }
JSON
# replace-topic-rule fails if absent; create-topic-rule fails if present -> try both.
aws iot create-topic-rule --rule-name "$RULE" --topic-rule-payload "file://$TMP/rule.json" --region "$REGION" 2>/dev/null \
    && echo "rule created" \
    || { aws iot replace-topic-rule --rule-name "$RULE" --topic-rule-payload "file://$TMP/rule.json" --region "$REGION"; echo "rule updated"; }

# 4) Wait for a few device publishes (period ~11 s) then read what CloudWatch captured.
echo "== waiting 40 s for live telemetry to arrive in CloudWatch =="
sleep 40
SINCE=$(( ( $(date +%s) - 300 ) * 1000 ))
echo "== messages in $LOG_GROUP (last 5 min) =="
aws logs filter-log-events --log-group-name "$LOG_GROUP" --start-time "$SINCE" --region "$REGION" \
    --query 'events[].message' --output text | tr '\t' '\n' | tail -20
