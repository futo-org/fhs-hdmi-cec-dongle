#!/usr/bin/env bash
# Probes which CEC commands a TV honors. Run over `ssh -t` so prompts work.
set -euo pipefail

DEVICE="${CEC_DEVICE:-/dev/cec-fhs}"
EDID_PATH="${EDID_PATH:-}"
POWER_TIMEOUT_S="${POWER_TIMEOUT_S:-20}"
STANDBY_SETTLE_S="${STANDBY_SETTLE_S:-10}"

readonly TV_LA=0
readonly BROADCAST_LA=15

readonly POWER_ON="on"
readonly POWER_STANDBY="standby"

readonly TX_OK="OK"
readonly TX_NACK="NACK"
readonly TX_NO_REPLY="NO_REPLY"
readonly TX_ABORTED="FEATURE_ABORT"
readonly TX_ERROR="ERROR"

readonly RESULT_PASS="PASS"
readonly RESULT_FAIL="FAIL"
readonly RESULT_SKIP="SKIP"
readonly RESULT_INFO="INFO"

declare -a TEST_ORDER=()
declare -A TEST_TX=() TEST_RESULT=() TEST_NOTE=()

LOG="$(mktemp -t cec-tv-test.XXXXXX.log)"
OWN_PA=""
INPUT_LABEL=""

cec() {
	cec-ctl -d "$DEVICE" "$@" 2>&1 | tee -a "$LOG"
}

tx_status() {
	local out="$1"
	if grep -q "Not Acknowledged" <<<"$out"; then
		echo "$TX_NACK"
	elif grep -q "Feature Abort" <<<"$out"; then
		echo "$TX_ABORTED"
	elif grep -q "Rx, Timeout" <<<"$out"; then
		echo "$TX_NO_REPLY"
	elif grep -qE "Tx, (Arbitration Lost|Low Drive|Error)" <<<"$out"; then
		echo "$TX_ERROR"
	else
		echo "$TX_OK"
	fi
}

first_reply_field() {
	awk '/Received from/ { found = 1; next } found && /: / { sub(/^[ \t]+/, ""); print; exit }' <<<"$1"
}

record() {
	local name="$1" tx="$2" result="$3" note="${4:-}"
	TEST_ORDER+=("$name")
	TEST_TX["$name"]="$tx"
	TEST_RESULT["$name"]="$result"
	TEST_NOTE["$name"]="$note"
	printf '  -> %-6s tx=%-13s %s\n' "$result" "$tx" "$note"
}

ask() {
	local question="$1" reply
	while true; do
		read -r -p "  ? $question [y/n/s=skip] " reply </dev/tty
		case "$reply" in
		y | Y) echo "$RESULT_PASS"; return ;;
		n | N) echo "$RESULT_FAIL"; return ;;
		s | S) echo "$RESULT_SKIP"; return ;;
		esac
	done
}

pause_for() {
	read -r -p "  ! $1 Press Enter to continue. " _ </dev/tty
}

power_state() {
	cec-ctl -d "$DEVICE" --to "$TV_LA" --give-device-power-status 2>&1 |
		sed -n 's/.*pwr-state: \(.*\) (0x.*/\1/p'
}

wait_for_power() {
	local target="$1" deadline=$((SECONDS + POWER_TIMEOUT_S))
	while ((SECONDS < deadline)); do
		[[ "$(power_state)" == "$target" ]] && return 0
		sleep 1
	done
	return 1
}

ensure_on() {
	[[ "$(power_state)" == "$POWER_ON" ]] && return
	cec --to "$TV_LA" --image-view-on >/dev/null
	wait_for_power "$POWER_ON" && return
	pause_for "Turn the TV on with its remote."
}

ensure_standby() {
	if [[ "$(power_state)" != "$POWER_STANDBY" ]]; then
		cec --to "$TV_LA" --standby >/dev/null
		wait_for_power "$POWER_STANDBY" || pause_for "Turn the TV off with its remote."
	fi
	sleep "$STANDBY_SETTLE_S"
}

ensure_other_input() {
	ensure_on
	pause_for "Switch the TV to an input other than $INPUT_LABEL."
}

find_edid() {
	local connector
	for connector in /sys/class/drm/card*-HDMI-A-*; do
		[[ "$(cat "$connector/status" 2>/dev/null)" == "connected" ]] || continue
		# sysfs reports size 0 for edid, so -s can't be used
		(($(wc -c <"$connector/edid") > 0)) || continue
		echo "$connector/edid"
		return
	done
	echo "No connected HDMI connector with an EDID found. Set EDID_PATH." >&2
	exit 1
}

enumerate() {
	echo "== Enumerate from EDID"
	[[ -n "$EDID_PATH" ]] || EDID_PATH="$(find_edid)"
	echo "  EDID: $EDID_PATH"
	if command -v edid-decode >/dev/null; then
		edid-decode "$EDID_PATH" 2>/dev/null |
			grep -E "Manufacturer:|Display Product Name|Source physical address" | sed 's/^ */  /'
	fi
	# Passthrough turns the TV's power key into the host's power button
	cec --playback --no-rc-passthrough --phys-addr-from-edid "$EDID_PATH" >/dev/null
	OWN_PA="$(cec-ctl -d "$DEVICE" | sed -n 's/.*Physical Address *: //p')"
	INPUT_LABEL="HDMI ${OWN_PA%%.*}"
	echo "  Our physical address: $OWN_PA ($INPUT_LABEL)"
	echo "== Topology"
	cec -S | sed -n '/Topology:/,$p'
}

test_queries() {
	echo "== Queries to TV"
	local option out
	for option in give-physical-addr give-osd-name give-device-vendor-id get-cec-version \
		give-device-power-status get-menu-language give-features; do
		echo "-- $option"
		out="$(cec --to "$TV_LA" --"$option")"
		record "query:$option" "$(tx_status "$out")" "$RESULT_INFO" "$(first_reply_field "$out")"
	done
}

test_broadcast() {
	echo "-- broadcast report-physical-addr"
	local out
	out="$(cec --to "$BROADCAST_LA" --report-physical-addr "phys-addr=$OWN_PA,prim-devtype=playback")"
	local tx
	tx="$(tx_status "$out")"
	if [[ "$tx" == "$TX_OK" ]]; then
		record "broadcast" "$tx" "$RESULT_PASS"
	else
		record "broadcast" "$tx" "$RESULT_FAIL" "another device is rejecting broadcasts"
	fi
}

run_wake() {
	local name="$1"
	shift
	echo "-- wake: $name"
	ensure_standby
	local out tx
	out="$("$@")"
	tx="$(tx_status "$out")"
	if wait_for_power "$POWER_ON"; then
		record "wake:$name" "$tx" "$RESULT_PASS"
	else
		record "wake:$name" "$tx" "$RESULT_FAIL" "power state: $(power_state)"
	fi
}

press() {
	cec --to "$TV_LA" --user-control-pressed "ui-cmd=$1"
	cec --to "$TV_LA" --user-control-released
}

test_wake() {
	echo "== Wake from standby"
	run_wake image-view-on cec --to "$TV_LA" --image-view-on
	run_wake text-view-on cec --to "$TV_LA" --text-view-on
	run_wake ui-power-on-function press power-on-function
	run_wake ui-power press power
	run_wake ui-power-toggle-function press power-toggle-function
}

run_standby() {
	local name="$1"
	shift
	echo "-- standby: $name"
	ensure_on
	local out tx
	out="$("$@")"
	tx="$(tx_status "$out")"
	if wait_for_power "$POWER_STANDBY"; then
		record "standby:$name" "$tx" "$RESULT_PASS"
	else
		record "standby:$name" "$tx" "$RESULT_FAIL" "power state: $(power_state)"
	fi
}

test_standby() {
	echo "== Standby"
	run_standby directed cec --to "$TV_LA" --standby
	run_standby broadcast cec --to "$BROADCAST_LA" --standby
	run_standby ui-power-off-function press power-off-function
}

run_switch() {
	local name="$1"
	shift
	echo "-- switch: $name"
	ensure_other_input
	local out
	out="$("$@")"
	record "switch:$name" "$(tx_status "$out")" "$(ask "Is the TV showing $INPUT_LABEL?")"
}

one_touch_play() {
	cec --to "$TV_LA" --image-view-on
	cec --to "$BROADCAST_LA" --active-source "phys-addr=$OWN_PA"
}

test_switch() {
	echo "== Input switching"
	run_switch active-source cec --to "$BROADCAST_LA" --active-source "phys-addr=$OWN_PA"
	run_switch set-stream-path cec --to "$BROADCAST_LA" --set-stream-path "phys-addr=$OWN_PA"
	run_switch one-touch-play one_touch_play

	echo "-- switch: one-touch-play from standby"
	ensure_standby
	local out
	out="$(one_touch_play)"
	wait_for_power "$POWER_ON" || true
	record "switch:one-touch-play-from-standby" "$(tx_status "$out")" "$(ask "Is the TV on and showing $INPUT_LABEL?")"
}

test_remote() {
	echo "== Remote control passthrough"
	ensure_on
	local key out
	for key in volume-up volume-down mute up down select back; do
		echo "-- ui: $key"
		out="$(press "$key")"
		record "ui:$key" "$(tx_status "$out")" "$(ask "Did the TV react to '$key'?")"
	done
}

print_summary() {
	local name
	echo
	printf '%-40s %-14s %-6s %s\n' TEST TX RESULT NOTE
	for name in "${TEST_ORDER[@]}"; do
		printf '%-40s %-14s %-6s %s\n' "$name" "${TEST_TX[$name]}" "${TEST_RESULT[$name]}" "${TEST_NOTE[$name]}"
	done
}

main() {
	[[ -r "$DEVICE" && -w "$DEVICE" ]] || { echo "Cannot open $DEVICE (add yourself to the video group)" >&2; exit 1; }
	enumerate
	test_queries
	test_broadcast
	test_standby
	test_wake
	test_switch
	test_remote

	local report
	report="cec-report-$(date +%Y%m%d-%H%M%S).txt"
	print_summary | tee "$report"
	echo
	echo "Summary: $report"
	echo "Raw cec-ctl log: $LOG"
}

main "$@"
